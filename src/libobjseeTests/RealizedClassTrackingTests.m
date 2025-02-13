//
//  RealizedClassTrackingTests.m
//  libobjseeTests
//
//  Created by Ethan Arbuckle on 2/9/25.
//

#import <XCTest/XCTest.h>
#import <objc/runtime.h>
#import "realized_class_tracking.h"

@interface RealizedClassTrackingTests : XCTestCase {
    Class _testClass;
}
@end

@implementation RealizedClassTrackingTests

- (void)setUp {
    [super setUp];
    _testClass = [NSObject class];
}

- (void)testNullClassHandling {
    kern_return_t result = record_class_encounter(NULL);
    XCTAssertEqual(result, KERN_FAILURE);
    
    result = has_seen_class(NULL);
    XCTAssertEqual(result, KERN_FAILURE);
}

- (void)testBasicClassRecording {
    kern_return_t record_result = record_class_encounter(_testClass);
    XCTAssertEqual(record_result, KERN_SUCCESS);
    
    kern_return_t seen_result = has_seen_class(_testClass);
    XCTAssertEqual(seen_result, KERN_SUCCESS);
}

- (void)testUnseenClass {
    Class unseenClass = NSClassFromString(@"SomeNonexistentClass");
    kern_return_t result = has_seen_class(unseenClass);
    XCTAssertEqual(result, KERN_FAILURE);
}

- (void)testMultipleClasses {
    Class classes[] = {
        [NSObject class],
        [NSString class],
        [NSArray class],
        [NSDictionary class],
        [NSNumber class]
    };
    
    for (size_t i = 0; i < sizeof(classes) / sizeof(Class); i++) {
        kern_return_t result = record_class_encounter(classes[i]);
        XCTAssertEqual(result, KERN_SUCCESS);
    }
    
    for (size_t i = 0; i < sizeof(classes) / sizeof(Class); i++) {
        kern_return_t result = has_seen_class(classes[i]);
        XCTAssertEqual(result, KERN_SUCCESS);
    }
}

- (void)testConcurrentClassRecording {
    dispatch_queue_t concurrentQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
    dispatch_group_t group = dispatch_group_create();
    
    NSArray *testClasses = @[
        [NSObject class],
        [NSString class],
        [NSArray class],
        [NSDictionary class],
        [NSNumber class]
    ];
    
    [testClasses enumerateObjectsUsingBlock:^(Class cls, NSUInteger idx, BOOL *stop) {
        dispatch_group_async(group, concurrentQueue, ^{
            kern_return_t result = record_class_encounter(cls);
            XCTAssertEqual(result, KERN_SUCCESS);
        });
    }];
    
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    
    for (Class cls in testClasses) {
        kern_return_t result = has_seen_class(cls);
        XCTAssertEqual(result, KERN_SUCCESS);
    }
}

- (void)testCapacityExpansion {
    size_t initialCapacity = 1024;
    Class *testClasses = (Class *)malloc((initialCapacity + 101) * sizeof(Class));
    size_t classCount = 0;
    
    @autoreleasepool {
        for (size_t i = 0; i <= initialCapacity + 100; i++) {
            char className[32];
            snprintf(className, sizeof(className), "TestClass%zu", i);
            
            Class existingClass = objc_lookUpClass(className);
            if (existingClass != NULL) {
                objc_disposeClassPair(existingClass);
            }
            
            Class newClass = objc_allocateClassPair([NSObject class], className, 0);
            if (newClass == NULL) {
                continue;
            }
            
            objc_registerClassPair(newClass);
            kern_return_t result = record_class_encounter(newClass);
            XCTAssertEqual(result, KERN_SUCCESS);
            
            testClasses[classCount++] = newClass;
        }
        
        for (size_t i = 0; i < classCount; i++) {
            kern_return_t result = has_seen_class(testClasses[i]);
            XCTAssertEqual(result, KERN_SUCCESS);
        }
        
        for (size_t i = 0; i < classCount; i++) {
            if (testClasses[i] != NULL) {
                objc_disposeClassPair(testClasses[i]);
            }
        }
    }
    
    free(testClasses);
}

- (void)testReinitialization {
    kern_return_t first_init = record_class_encounter(_testClass);
    XCTAssertEqual(first_init, KERN_SUCCESS);
    
    kern_return_t second_init = record_class_encounter(_testClass);
    XCTAssertEqual(second_init, KERN_SUCCESS);
}

- (void)testConcurrentLookups {
    kern_return_t record_result = record_class_encounter(_testClass);
    XCTAssertEqual(record_result, KERN_SUCCESS);
    
    dispatch_queue_t concurrentQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
    dispatch_group_t group = dispatch_group_create();
    
    for (int i = 0; i < 1000; i++) {
        dispatch_group_async(group, concurrentQueue, ^{
            kern_return_t result = has_seen_class(self->_testClass);
            XCTAssertEqual(result, KERN_SUCCESS);
        });
    }
    
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
}

@end
