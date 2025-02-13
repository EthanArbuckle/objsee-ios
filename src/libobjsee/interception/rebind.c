//
//  rebind.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_map.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include "tracer_internal.h"
#include "rebind.h"

struct symbol_rebinding_internal_t {
    uint64_t slide;
    const char *string_table;
    struct nlist_64 *symbol_table;
    uint32_t *indirect_symbol_table;
};


kern_return_t rebind_symbol(const char *symbol_to_hook, void *replacement_func, struct symbol_rebinding_internal_t *img_symbol_info, struct section_64 *symbol_section) {
    
    uint32_t *indirect_symbols = img_symbol_info->indirect_symbol_table + symbol_section->reserved1;
    void **sym_bindings = (void **)(symbol_section->addr + img_symbol_info->slide);
    for (uint i = 0; i < symbol_section->size / sizeof(void *); i++) {
        
        uint32_t idx = indirect_symbols[i];
        if (idx & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) {
            continue;
        }
        
        uint32_t string_table_offset = img_symbol_info->symbol_table[idx].n_un.n_strx;
        const char *symbol_name = img_symbol_info->string_table + string_table_offset;
        if (symbol_name == NULL || strlen(symbol_name) < 1 || strcmp(&symbol_name[1], symbol_to_hook) != 0) {
            continue;
        }
        
        if (vm_protect(mach_task_self(), (vm_address_t)sym_bindings, symbol_section->size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
            printf("Failed to update prot attrs of symbol bindings\n");
            return KERN_FAILURE;
        }
        
        sym_bindings[i] = replacement_func;
        return KERN_SUCCESS;
    }
    
    return KERN_FAILURE;
}

void for_load_command_in_mach_header_64(struct mach_header_64 *mh, void (^callback)(struct load_command *lc)) {
    
    if (mh == NULL || callback == NULL) {
        printf("Invalid args: mach_header: %p, callback: %p\n", mh, callback);
        return;
    }
    
    struct load_command *lc = (struct load_command *)((mach_vm_address_t)mh + sizeof(struct mach_header_64));
    for (int command = 0; command < mh->ncmds; command++) {
        
        callback(lc);
        lc = (struct load_command *)((mach_vm_address_t)lc + lc->cmdsize);
    }
}


kern_return_t hook_function_in_mach_header_64(const char *symbol_to_hook, void *replacement_func, struct mach_header_64 *mh) {
    
    if (mh == NULL || symbol_to_hook == NULL || replacement_func == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    Dl_info info;
    if (dladdr((void *)mh, &info) == 0) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    struct symbol_rebinding_internal_t *img_symbol_info = (struct symbol_rebinding_internal_t *)malloc(sizeof(struct symbol_rebinding_internal_t));
    
    __block struct symtab_command *symbol_table_cmd = NULL;
    __block struct dysymtab_command *dynamic_symbol_table_cmd = NULL;
    __block struct segment_command_64 *linkedit_cmd = NULL;
    __block struct segment_command_64 *text_cmd = NULL;
    for_load_command_in_mach_header_64(mh, ^(struct load_command *lc) {
        switch (lc->cmd) {
                
            case LC_DYSYMTAB:
                dynamic_symbol_table_cmd = (struct dysymtab_command *)lc;
                break;
                
            case LC_SYMTAB:
                symbol_table_cmd = (struct symtab_command *)lc;
                break;
                
            case LC_SEGMENT_64: {
                
                struct segment_command_64 *seg = (struct segment_command_64 *)lc;
                if (strcmp(seg->segname, SEG_TEXT) == 0) {
                    text_cmd = seg;
                }
                
                else if (strcmp(seg->segname, SEG_LINKEDIT) == 0) {
                    linkedit_cmd = (struct segment_command_64 *)seg;
                }
                
                break;
            }
                
            default:
                break;
        }
    });
    
    if (linkedit_cmd == NULL || symbol_table_cmd == NULL || dynamic_symbol_table_cmd == NULL || text_cmd == NULL) {
        free(img_symbol_info);
        return KERN_FAILURE;
    }
    
    if (dynamic_symbol_table_cmd->nindirectsyms == 0) {
        free(img_symbol_info);
        return KERN_FAILURE;
    }
    
    img_symbol_info->slide = (intptr_t)mh - text_cmd->vmaddr;
    img_symbol_info->string_table = (void *)linkedit_cmd->vmaddr + symbol_table_cmd->stroff - linkedit_cmd->fileoff + img_symbol_info->slide;
    img_symbol_info->symbol_table = (void *)linkedit_cmd->vmaddr + symbol_table_cmd->symoff - linkedit_cmd->fileoff + img_symbol_info->slide;
    img_symbol_info->indirect_symbol_table = (void *)linkedit_cmd->vmaddr + dynamic_symbol_table_cmd->indirectsymoff - linkedit_cmd->fileoff + img_symbol_info->slide;
    
    __block struct section_64 *lazy_symbol_section = NULL;
    __block struct section_64 *non_lazy_symbol_section = NULL;
    for_load_command_in_mach_header_64(mh, ^(struct load_command *lc) {
        
        if (lc->cmd != LC_SEGMENT_64) {
            return;
        }
        
        struct segment_command_64 *seg = (struct segment_command_64 *)lc;
        if (strcmp(seg->segname, SEG_DATA) != 0 && strcmp(seg->segname, "__DATA_CONST") != 0) {
            return;
        }
        
        for (int i = 0; i < seg->nsects; i++) {
            
            struct section_64 *sect = (struct section_64 *)((mach_vm_address_t)seg + sizeof(struct segment_command_64)) + i;
            switch (sect->flags & SECTION_TYPE) {
                    
                case S_LAZY_SYMBOL_POINTERS:
                    lazy_symbol_section = sect;
                    break;
                    
                case S_NON_LAZY_SYMBOL_POINTERS:
                    non_lazy_symbol_section = sect;
                    break;
                    
                default:
                    break;
            }
        }
    });
    
    kern_return_t ret = KERN_FAILURE;
    if (lazy_symbol_section) {
        ret = rebind_symbol(symbol_to_hook, replacement_func, img_symbol_info, lazy_symbol_section);
    }
    
    if (ret != KERN_SUCCESS && non_lazy_symbol_section) {
        ret = rebind_symbol(symbol_to_hook, replacement_func, img_symbol_info, non_lazy_symbol_section);
    }
    
    free(img_symbol_info);
    return ret;
}

struct symbol_rebinding_t * _Nullable hook_function(const char *symbol_to_hook, void *replacement_func) {
    
    if (symbol_to_hook == NULL || replacement_func == NULL) {
        return NULL;
    }

    void *function_to_hook_ptr = dlsym(RTLD_DEFAULT, symbol_to_hook);
    if (function_to_hook_ptr == NULL) {
        return NULL;
    }
    
    int hook_count = 0;
    for (int i = 0; i < _dyld_image_count(); i++) {
        struct mach_header_64 *mh = (struct mach_header_64 *)_dyld_get_image_header(i);
        if (hook_function_in_mach_header_64(symbol_to_hook, replacement_func, mh) == KERN_SUCCESS) {
            hook_count++;
        }
    }
    
    if (hook_count == 0) {
        return NULL;
    }
    
    // Inform the caller of the number of symbols rebound
    struct symbol_rebinding_t *rebinding = (struct symbol_rebinding_t *)malloc(sizeof(struct symbol_rebinding_t));
    rebinding->name = symbol_to_hook;
    rebinding->replacement = replacement_func;
    rebinding->num_symbols_rebound = hook_count;
    return rebinding;
}
