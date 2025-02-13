//
//  CoreSymbolicationTests.m
//  libobjseeTests
//
//  Created by Ethan Arbuckle on 2/9/25.
//

#import <XCTest/XCTest.h>
#import <mach/mach.h>
#import <sys/wait.h>
#import <spawn.h>
#import "symbolication.h"

@interface CoreSymbolicationTests : XCTestCase {
    pid_t _helperPid;
    task_t _helperTask;
    CSSymbolicatorRef _symbolicator;
    dispatch_source_t _exitSource;
}
@end

@implementation CoreSymbolicationTests

static void spawn_suspended_helper(pid_t *pid) {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    XCTAssertEqual(posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED), 0);
    
    extern char **environ;
    const char *self_path = NSBundle.mainBundle.executablePath.UTF8String;
    char *const argv[] = { (char *)self_path, NULL };
    XCTAssertEqual( posix_spawn(pid, self_path, NULL, &attr, argv, environ), 0);
    posix_spawnattr_destroy(&attr);
}

- (void)setUp {
    spawn_suspended_helper(&_helperPid);
    XCTAssertEqual(task_for_pid(mach_task_self(), _helperPid, &_helperTask), KERN_SUCCESS);
    XCTAssertNotEqual(_helperTask, TASK_NULL);
    
    _symbolicator = create_symbolicator_with_task(_helperTask);
    XCTAssertFalse(cs_isnull(_symbolicator));
    
    _exitSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, _helperPid, DISPATCH_PROC_EXIT, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_exitSource, ^{
        kill(self->_helperPid, SIGKILL);
        self->_helperPid = -1;
    });
    dispatch_resume(_exitSource);
}

- (void)tearDown {
    if (_helperPid > 0) {
        kill(_helperPid, SIGKILL);
        int status;
        waitpid(_helperPid, &status, 0);
    }
    
    if (_exitSource) {
        dispatch_source_cancel(_exitSource);
        _exitSource = NULL;
    }
    
    if (_helperTask != TASK_NULL) {
        mach_port_deallocate(mach_task_self(), _helperTask);
        _helperTask = TASK_NULL;
    }
    
    _symbolicator = (CSSymbolicatorRef){0};
}

- (void)testSymbolicateDyldInHelper {
    CSSymbolOwnerRef dyld = get_symbol_owner_for_name(_symbolicator, "dyld");
    XCTAssertFalse(cs_isnull(dyld));
    
    CSSymbolRef dyldSymbol = get_symbol_from_owner_with_name(dyld, "dyld_all_image_infos");
    XCTAssertFalse(cs_isnull(dyldSymbol));
    
    CSRange symbolRange = get_range_for_symbol(dyldSymbol);
    XCTAssertGreaterThan(symbolRange.length, 0);
    
    const char *symbolName = get_name_for_symbol(dyldSymbol);
    XCTAssertNotEqual(symbolName, NULL);
    XCTAssertEqual(strcmp(symbolName, "dyld_all_image_infos"), 0);
}

- (void)testSymbolicateAllDyldSymbols {
    CSSymbolOwnerRef dyld = get_symbol_owner_for_name(_symbolicator, "dyld");
    XCTAssertFalse(cs_isnull(dyld));
    
    __block int symbolCount = 0;
    __block BOOL foundSymbol = NO;
    
    for_each_symbol(_symbolicator, ^(CSSymbolRef symbol) {
        symbolCount++;
        const char *name = get_name_for_symbol(symbol);
        if (name && strcmp(name, "dyld_all_image_infos") == 0) {
            foundSymbol = YES;
        }
    });
    
    XCTAssertGreaterThan(symbolCount, 0);
    XCTAssertTrue(foundSymbol);
}

- (void)testSymbolicateAddress {
    CSSymbolOwnerRef dyld = get_symbol_owner_for_name(_symbolicator, "dyld");
    XCTAssertFalse(cs_isnull(dyld));
    
    CSSymbolRef dyldSymbol = get_symbol_from_owner_with_name(dyld, "task_register_dyld_shared_cache_image_info");
    XCTAssertFalse(cs_isnull(dyldSymbol));
    
    CSRange symbolRange = get_range_for_symbol(dyldSymbol);
    const char *symbolName = get_name_for_symbol_at_address(_symbolicator, symbolRange.location);
    XCTAssertNotEqual(symbolName, NULL);
    if (symbolName) {
        XCTAssertEqual(strcmp(symbolName, "task_register_dyld_shared_cache_image_info"), 0);
    }
}

- (void)testChildProcess {
    pid_t secondPid;
    spawn_suspended_helper(&secondPid);
    
    task_t secondTask;
    XCTAssertEqual(task_for_pid(mach_task_self(), secondPid, &secondTask), KERN_SUCCESS);
    
    CSSymbolicatorRef secondSymbolicator = create_symbolicator_with_task(secondTask);
    XCTAssertFalse(cs_isnull(secondSymbolicator));
    
    CSSymbolOwnerRef dyld = get_symbol_owner_for_name(secondSymbolicator, "dyld");
    XCTAssertFalse(cs_isnull(dyld));
    
    kill(secondPid, SIGKILL);
    int status;
    waitpid(secondPid, &status, 0);
    mach_port_deallocate(mach_task_self(), secondTask);
}

- (void)testSymbolOwnerEnumeration {
    __block BOOL foundDyld = NO;
    for_each_symbol_owner(_symbolicator, ^(CSSymbolOwnerRef owner) {
        const char *path = get_image_path_for_symbol_owner(owner);
        if (path && strstr(path, "dyld") != NULL) {
            foundDyld = YES;
        }
    });
    XCTAssertTrue(foundDyld);
}

@end
