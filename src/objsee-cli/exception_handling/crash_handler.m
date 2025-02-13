//
//  crash_handler.c
//  cli
//
//  Created by Ethan Arbuckle on 12/30/24.
//

#include <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unwind.h>
#include <dlfcn.h>
#include <capstone/capstone.h>
#include "mach_excServer.h"
#include "symbolication.h"
#include "highlight.h"

/*
 When tracing an app using the cli tool, and the app crashes, the following details are collected:
 1. A 2s sample of every thread in the crashing process.
 2. Symbolicated callstacks for each thread.
 3. The crashing thread's threadstate.
 4. The contents of each register (in the remote processes crashing thread) are copied.
 5. The code instructions around the crash (pc of crashing thread) are disassembled with Capstone.
 
 This info goes through a syntax highlighter then is printed to the console
*/

#define MAX_FRAMES 60
#define MAX_EXCEPTIONS 1

typedef struct {
    const char *name;
    int number;
    const char *description;
    exception_type_t exception_type;
    int exception_code;
    const char *exception_type_name;
} signal_info_t;

static const signal_info_t signal_map[] = {
    {"SIGABRT", 6, "Abort trap: 6", EXC_CRASH, 0, "EXC_CRASH"},
    {"SIGBUS", 10, "Bus error: 10", EXC_BAD_ACCESS, KERN_PROTECTION_FAILURE, "EXC_BAD_ACCESS"},
    {"SIGSEGV", 11, "Segmentation fault: 11", EXC_BAD_ACCESS, KERN_INVALID_ADDRESS, "EXC_BAD_ACCESS"},
    {"SIGILL", 4, "Illegal instruction: 4", EXC_BAD_INSTRUCTION, 0, "EXC_BAD_INSTRUCTION"},
    {"SIGFPE", 8, "Floating point exception: 8", EXC_ARITHMETIC, 0, "EXC_ARITHMETIC"},
    {"SIGKILL", 9, "Killed: 9", EXC_GUARD, 0, "EXC_GUARD"},
    {"SIGTRAP", 5, "Trace/BPT trap: 5", EXC_BREAKPOINT, 0, "EXC_BREAKPOINT"},
    {"SIGPIPE", 13, "Broken pipe: 13", EXC_BAD_ACCESS, KERN_PROTECTION_FAILURE, "EXC_BAD_ACCESS"},
    {"SIGALRM", 14, "Alarm clock: 14", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGTERM", 15, "Terminated: 15", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGURG", 16, "Urgent I/O condition: 16", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGSTOP", 17, "Stopped: 17", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGTSTP", 18, "Suspended (signal): 18", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGCONT", 19, "Continued: 19", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGCHLD", 20, "Child exited: 20", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGTTIN", 21, "Stopped (tty input): 21", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGTTOU", 22, "Stopped (tty output): 22", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGIO", 23, "I/O possible: 23", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGXCPU", 24, "Cputime limit exceeded: 24", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGXFSZ", 25, "Filesize limit exceeded: 25", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGVTALRM", 26, "Virtual timer expired: 26", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGPROF", 27, "Profiling timer expired: 27", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGWINCH", 28, "Window size changes: 28", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGINFO", 29, "Information request: 29", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGUSR1", 30, "User defined signal 1: 30", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {"SIGUSR2", 31, "User defined signal 2: 31", EXC_RESOURCE, 0, "EXC_RESOURCE"},
    {NULL, 0, NULL, 0, 0, NULL}
};

static struct {
    CSSymbolicatorRef symbolicator;
    mach_port_t exception_port;
    task_t traced_app_task;
    pid_t traced_app_pid;
    int exceptions_caught;
    csh cs_handle;
} g_state = {0};

typedef struct {
    uintptr_t *frames;
    int frame_count;
    int max_frames;
} unwind_state_t;


kern_return_t catch_mach_exception_raise(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, mach_exception_data_t code, mach_msg_type_number_t codeCnt) {
    return KERN_FAILURE;
}

kern_return_t catch_mach_exception_raise_state_identity(mach_port_t exception_port, mach_port_t thread, mach_port_t task, exception_type_t exception, mach_exception_data_t code, mach_msg_type_number_t codeCnt, int *flavor, thread_state_t old_state, mach_msg_type_number_t old_stateCnt, thread_state_t new_state, mach_msg_type_number_t *new_stateCnt) {
    return KERN_FAILURE;
}

static const signal_info_t *get_signal_info(exception_type_t exception, mach_exception_data_t code) {
    for (const signal_info_t *info = signal_map; info->name != NULL; info++) {
        if (info->exception_type == exception &&
            (exception != EXC_BAD_ACCESS || info->exception_code == code[0])) {
            return info;
        }
    }
    return NULL;
}

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *context, void *arg) {
    unwind_state_t *state = (unwind_state_t *)arg;
    if (state->frame_count >= state->max_frames) {
        return _URC_END_OF_STACK;
    }
    
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        state->frames[state->frame_count] = pc;
        state->frame_count++;
    }
    
    return _URC_NO_REASON;
}

static void print_vm_region_info(vm_address_t fault_addr) {
    vm_size_t vm_size;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name;
    if (vm_region_64(g_state.traced_app_task, &fault_addr, &vm_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &info_count, &object_name) == KERN_SUCCESS) {
        printf("VM Region Info: 0x%llx is in 0x%llx-0x%llx; bytes after start: %lld bytes before end: %lld\n",
               (uint64_t)fault_addr, (uint64_t)fault_addr, (uint64_t)(fault_addr + vm_size), (uint64_t)(fault_addr - fault_addr), (uint64_t)(fault_addr + vm_size - fault_addr));
    }
}

static void print_memory_peek(task_t task, uint64_t address) {
    if (address == 0) {
        return;
    }
    
    uint8_t buffer[16] = {0};
    vm_size_t size_read = 0;
    if (vm_read_overwrite(task, (mach_vm_address_t)address, sizeof(buffer), (vm_address_t)buffer, &size_read) == KERN_SUCCESS) {
        printf("\t[");
        for (int i = 0; i < size_read && i < 16; i++) {
            printf("%02x%s", buffer[i], i < size_read - 1 ? " " : "");
        }
        printf("] \"");
        for (int i = 0; i < size_read && i < 16; i++) {
            char c = buffer[i];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        printf("\"");
    }
}

static void print_thread_state(const arm_thread_state64_t *state, int failed_register) {
    for (int i = 0; i < 29; i++) {
        if (failed_register == i) {
            printf("\033[0;31m");
        }

        printf("\nx%-2d: 0x%016llx", i, state->__x[i]);
        if (state->__x[i] != 0) {
            print_memory_peek(g_state.traced_app_task, state->__x[i]);
        }
        
        if (failed_register == i) {
            printf("\033[0m");
        }
    }
    
    printf("\n sp: 0x%016llx", state->__sp);
    print_memory_peek(g_state.traced_app_task, state->__sp);
    printf("\n fp: 0x%016llx", state->__fp);
    print_memory_peek(g_state.traced_app_task, state->__fp);
    printf("\n lr: 0x%016llx", state->__lr);
    print_memory_peek(g_state.traced_app_task, state->__lr);
    printf("\n pc: 0x%016llx", state->__pc);
    print_memory_peek(g_state.traced_app_task, state->__pc);
}


NSString *take_sample(task_t task) {
    Class _VMUSampler = objc_getClass("VMUSampler");
    id sampler = [_VMUSampler alloc];
    SEL _initWithPID = NSSelectorFromString(@"initWithPID:orTask:options:");
    sampler = ((id (*)(id, SEL, int, task_t, uint64_t))objc_msgSend)(sampler, _initWithPID, 0, task, 0x2);

    SEL _setTimeLimit = NSSelectorFromString(@"setTimeLimit:");
    ((void (*)(id, SEL, double))objc_msgSend)(sampler, _setTimeLimit, 1.0);
    SEL _start = NSSelectorFromString(@"start");
    ((void (*)(id, SEL))objc_msgSend)(sampler, _start);
    SEL _waitUntilDone = NSSelectorFromString(@"waitUntilDone");
    ((void (*)(id, SEL))objc_msgSend)(sampler, _waitUntilDone);
    
    id samples = ((id (*)(id, SEL))objc_msgSend)(sampler, NSSelectorFromString(@"samples"));

    Class _VMUCallTreeNode = objc_getClass("VMUCallTreeNode");
    SEL _rootForSamples = NSSelectorFromString(@"rootForSamples:symbolicator:sampler:options:");
    id root = ((id(*)(id, SEL, id, CSSymbolicatorRef, id, uint64_t))objc_msgSend)(_VMUCallTreeNode, _rootForSamples, samples, g_state.symbolicator, sampler, 0);

    SEL _stringFromCallTreeWithOptions = NSSelectorFromString(@"stringFromCallTreeWithOptions:");
    NSString *string = ((NSString * (*)(id, SEL, uint64_t))objc_msgSend)(root, _stringFromCallTreeWithOptions, 0x20);
    return string;
}

static void _g_task_start_peeking(task_t task) {
    static dispatch_once_t onceToken;
    static void *_task_start_peeking_ptr = NULL;
    
    dispatch_once(&onceToken, ^{
        void *symbolication = dlopen("/System/Library/PrivateFrameworks/Symbolication.framework/Symbolication", RTLD_LAZY);
        _task_start_peeking_ptr = dlsym(symbolication, "task_start_peeking");
    });
    
    if (_task_start_peeking_ptr) {
        ((void (*)(task_t))_task_start_peeking_ptr)(task);
    }
}

static void _g_task_stop_peeking(task_t task) {
    static dispatch_once_t onceToken;
    static void *_task_stop_peeking_ptr = NULL;
    
    dispatch_once(&onceToken, ^{
        void *symbolication = dlopen("/System/Library/PrivateFrameworks/Symbolication.framework/Symbolication", RTLD_LAZY);
        _task_stop_peeking_ptr = dlsym(symbolication, "task_stop_peeking");
    });
    
    if (_task_stop_peeking_ptr) {
        ((void (*)(task_t))_task_stop_peeking_ptr)(task);
    }
}


static void print_disassembly(uint64_t pc_address) {
    int instr_size = 4;
    int instrs_before = 12;
    int instrs_after = 4;
    int total_instr_size = (instrs_before + instrs_after) * instr_size;
    
    uint8_t code_buffer[1024] = {0};
    vm_size_t size_read = 0;
    uint64_t start_address = pc_address - (instrs_before * instr_size);
    if (vm_read_overwrite(g_state.traced_app_task, (mach_vm_address_t)start_address, total_instr_size, (vm_address_t)code_buffer, &size_read) != KERN_SUCCESS) {
        return;
    }
    
    CSSymbolOwnerRef symbol_owner = get_symbol_owner(g_state.symbolicator, pc_address);
    if (!cs_isnull(symbol_owner)) {
        const char *image_path = get_image_path_for_symbol_owner(symbol_owner);
        const char *function_name = get_name_for_symbol_at_address(g_state.symbolicator, pc_address);
        if (image_path) {
            const char *short_name = strrchr(image_path, '/');
            image_path = short_name ? short_name + 1 : image_path;
        }
        else {
            image_path = "???";
        }
        
        if (function_name) {
            printf("\n%s:%s\n", image_path, function_name);
        }
    }

    cs_insn *insn;
    size_t count = cs_disasm(g_state.cs_handle, code_buffer, size_read, start_address, 0, &insn);
    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            if (insn[i].address == pc_address) {
                printf("\033[0;31m");
            }
            const char *marker = (insn[i].address == pc_address) ? "> " : "  ";
            printf("%s0x%llx:\t%-8s\t%s", marker, insn[i].address, insn[i].mnemonic, insn[i].op_str);
            
            if (insn[i].detail->arm64.op_count > 0 && insn[i].detail->arm64.operands[0].type == ARM64_OP_IMM) {
                if (insn[i].mnemonic[0] == 'b') {
                    const char *symbol_name = get_name_for_symbol_at_address(g_state.symbolicator, insn[i].detail->arm64.operands[0].imm);
                    if (symbol_name) {
                        printf("\033[0;34m");
                        printf("\t; %s", symbol_name);
                        printf("\033[0m");
                    }
                }
                else {
                    print_memory_peek(g_state.traced_app_task, insn[i].detail->arm64.operands[0].imm);
                }
            }
            
            printf("\n");
            
            if (insn[i].address == pc_address) {
                printf("\033[0m");
            }
        }
        cs_free(insn, count);
    }
}

static void print_backtrace(const arm_thread_state64_t *thread_state) {
    uintptr_t frames[MAX_FRAMES] = {0};
    unwind_state_t unwind_state = {
        .frames = frames,
        .frame_count = 0,
        .max_frames = MAX_FRAMES
    };
    _Unwind_Backtrace(unwind_callback, &unwind_state);
    
    uint64_t *fp = (uint64_t *)thread_state->__fp;
    for (int i = 0; i < MAX_FRAMES; i++) {
        uint64_t frame_address = 0;
        uint64_t frame[2] = {0};
        if (fp != NULL) {
            vm_size_t size_read;
            if (vm_read_overwrite(g_state.traced_app_task, (mach_vm_address_t)fp, sizeof(frame), (vm_address_t)&frame, &size_read) == KERN_SUCCESS) {
                frame_address = frame[1];
            }
        }
        
        if (frame_address == 0) {
            frame_address = (uint64_t)unwind_state.frames[i];
        }
        
        if (frame_address == 0) {
            break;
        }
        
        CSSymbolOwnerRef symbol_owner = get_symbol_owner(g_state.symbolicator, frame_address);
        if (cs_isnull(symbol_owner)) {
            continue;
        }
        
        CSSymbolRef symbol = get_symbol_at_address(g_state.symbolicator, frame_address);
        if (cs_isnull(symbol)) {
            continue;
        }
        
        const char *symbol_name = get_name_for_symbol(symbol);
        if (symbol_name == NULL) {
            symbol_name = "???";
        }
        
        CSRange symbol_range = get_range_for_symbol(symbol);
        uint64_t symbol_offset = frame_address - symbol_range.location;
        const char *image_path = get_image_path_for_symbol_owner(symbol_owner);
        if (image_path) {
            const char *short_name = strrchr(image_path, '/');
            image_path = short_name ? short_name + 1 : image_path;
        }
        else {
            image_path = "???";
        }
        
        if (i == 0) {
            printf("\033[0;31m");
        }
        printf("%3d  %-30s 0x%llx  %s%s%lld\n", i, image_path, symbol_offset, symbol_name, symbol_offset > 0 ? " + " : "", symbol_offset);
        
        if (i == 0) {
            printf("\033[0m");
        }
    
        if (frame[0] != 0) {
            fp = (uint64_t *)frame[0];
        }
        else {
            fp = NULL;
        }
    }
}

kern_return_t catch_mach_exception_raise_state(mach_port_t exception_port, exception_type_t exception, const mach_exception_data_t code, mach_msg_type_number_t codeCnt, int *flavor, const thread_state_t old_state, mach_msg_type_number_t old_stateCnt, thread_state_t new_state, mach_msg_type_number_t *new_stateCnt) {
    memcpy(new_state, old_state, old_stateCnt * sizeof(natural_t));
    *new_stateCnt = old_stateCnt;
    arm_thread_state64_t thread_state = *(arm_thread_state64_t *)old_state;
    
    _g_task_start_peeking(g_state.traced_app_task);

    NSString *sampleCallGraphs = take_sample(g_state.traced_app_task);
    char *hl = highlight_line(sampleCallGraphs.UTF8String, NULL, 0);
    fflush(stdout);
    if (hl) {
        printf("%s\n", hl);
        highlight_free(hl);
    }
    else {
        printf("%s\n", sampleCallGraphs.UTF8String);
    }

    const signal_info_t *signal_info = get_signal_info(exception, code);
    if (signal_info) {
        printf("\nException Type:  %s (%s)\n", signal_info->exception_type_name, signal_info ? signal_info->name : "");
    }
    else {
        printf("\nException Type:  %d\n", exception);
    }
    
    int faulting_register = -1;
    if (exception == EXC_BAD_ACCESS && codeCnt > 1) {
        vm_address_t fault_addr = (vm_address_t)code[1];
        const char *reason = code[0] == KERN_PROTECTION_FAILURE ? "KERN_PROTECTION_FAILURE" : "KERN_INVALID_ADDRESS";
        printf("Exception Subtype: %s at 0x%llx\n", reason, (uint64_t)fault_addr);
        print_vm_region_info(fault_addr);
    
        for (int i = 0; i < 29; i++) {
            if (thread_state.__x[i] <= fault_addr && fault_addr < thread_state.__x[i] + 8) {
                faulting_register = i;
                printf("\033[0;31m");
                printf("Fault address %llx is in register x%d\n", (uint64_t)fault_addr, i);
                printf("\033[0m");
            }
        }
    }

    printf("\nTermination Reason: %s %d %s\n", signal_info ? "SIGNAL" : "Exception", signal_info ? signal_info->number : 0, signal_info ? signal_info->description : "");
    
    thread_act_array_t threads;
    mach_msg_type_number_t thread_count;
    int thread_index = 0;
    char thread_name[256] = {0};
    if (task_threads(g_state.traced_app_task, &threads, &thread_count) == KERN_SUCCESS) {
        thread_identifier_info_data_t identifier_info;
        mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            if (thread_info(threads[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&identifier_info, &info_count) != KERN_SUCCESS) {
                continue;
            }
            
            if (identifier_info.thread_id != thread_state.__x[0]) {
                continue;
            }
            
            thread_index = i;
            pthread_t pthread = pthread_from_mach_thread_np(threads[i]);
            if (pthread) {
                pthread_getname_np(pthread, thread_name, sizeof(thread_name));
            }
            break;
        }
        
        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)threads, sizeof(thread_act_t) * thread_count);
    }
    
    printf("Triggered by Thread: %d\n", thread_index);
    printf("\nThread %d name:   %s\n", thread_index, strlen(thread_name) > 0 ? thread_name : "Dispatch queue: com.apple.main-thread");
    printf("Thread %d Crashed:\n", thread_index);
    
    print_backtrace(&thread_state);
    print_thread_state(&thread_state, faulting_register);
    printf("\n\n");
    print_disassembly(thread_state.__pc);

    _g_task_stop_peeking(g_state.traced_app_task);

    if (++g_state.exceptions_caught >= MAX_EXCEPTIONS) {
        g_state.traced_app_pid = 0;
        exit(0);
    }

    return KERN_SUCCESS;
}

static void *exception_handler(void *unused) {
    while (1) {
        mach_msg_server(mach_exc_server, sizeof(union __RequestUnion__catch_mach_exc_subsystem), g_state.exception_port, MACH_MSG_OPTION_NONE);
    }
    return NULL;
}

void setup_exception_handler_on_process(pid_t traced_app_pid) {
    highlight_init(NULL);
    
    if (init_core_symbolication() != KERN_SUCCESS) {
        printf("Failed to locate CoreSymbolication functions\n");
        return;
    }
    
    g_state.traced_app_pid = traced_app_pid;
    if (task_for_pid(mach_task_self(), traced_app_pid, &g_state.traced_app_task) != KERN_SUCCESS) {
        printf("Failed to get task for pid %d\n", traced_app_pid);
        return;
    }
    
    if (g_state.traced_app_task == MACH_PORT_NULL) {
        printf("Received null task for pid %d\n", traced_app_pid);
        return;
    }
    
    g_state.symbolicator = create_symbolicator_with_task(g_state.traced_app_task);
    if (cs_isnull(g_state.symbolicator)) {
        printf("Failed to create symbolicator for task\n");
        return;
    }
    
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &g_state.cs_handle) != CS_ERR_OK) {
        printf("Failed to initialize Capstone\n");
        return;
    }
    cs_option(g_state.cs_handle, CS_OPT_DETAIL, CS_OPT_ON);

    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_state.exception_port);
    mach_port_insert_right(mach_task_self(), g_state.exception_port, g_state.exception_port, MACH_MSG_TYPE_MAKE_SEND);
//    task_set_exception_ports(g_state.traced_app_task, EXC_MASK_ALL, g_state.exception_port, EXCEPTION_STATE | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64);
//    
//    pthread_t exception_thread;
//    pthread_create(&exception_thread, NULL, exception_handler, NULL);
//    pthread_detach(exception_thread);
}
