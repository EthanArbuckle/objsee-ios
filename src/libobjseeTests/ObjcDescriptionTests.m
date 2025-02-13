//
//  ObjcDescriptionTests.m
//  libobjseeTests
//
//  Created by Ethan Arbuckle on 2/9/25.
//

#import <XCTest/XCTest.h>
#import "tracer_internal.h"
#import "objc_arg_description.h"

@interface TestObject : NSObject
@property (nonatomic, copy) NSString *customDescription;
@end

@implementation TestObject
- (NSString *)description {
    return self.customDescription ?: [super description];
}
@end

@interface ObjcDescriptionTests : XCTestCase {
    TestObject *_testObject;
}
@end

@implementation ObjcDescriptionTests

- (void)setUp {
    [super setUp];
    _testObject = [[TestObject alloc] init];
}

- (void)tearDown {
    _testObject = nil;
    [super tearDown];
}

- (void)testNullHandling {
    const char *result = lookup_description_for_address(NULL, NULL);
    XCTAssertEqual(result, NULL);
    
    result = lookup_description_for_address((__bridge void *)_testObject, NULL);
    XCTAssertEqual(result, NULL);
    
    result = lookup_description_for_address(NULL, [TestObject class]);
    XCTAssertEqual(result, NULL);
}

- (void)testBasicDescriptionLookup {
    _testObject.customDescription = @"TestDescription";
    NSString *objcDesc = [_testObject description];
    XCTAssertEqualObjects(objcDesc, @"TestDescription");
    
    const char *utf8Str = [objcDesc UTF8String];
    XCTAssertNotEqual(utf8Str, NULL);
    
    const char *result = lookup_description_for_address((__bridge void *)_testObject, [TestObject class]);
    XCTAssertNotEqual(result, NULL);
    if (result) {
        XCTAssertEqual(strcmp(result, "TestDescription"), 0);
    }
}

- (void)testDescriptionCaching {
    _testObject.customDescription = @"CacheTest";
    
    const char *first = lookup_description_for_address((__bridge void *)_testObject, [TestObject class]);
    XCTAssertNotEqual(first, NULL);
    
    if (first) {
        XCTAssertEqual(strcmp(first, "CacheTest"), 0);
        
        const char *second = lookup_description_for_address((__bridge void *)_testObject, [TestObject class]);
        XCTAssertEqual(first, second);
    }
}

- (void)testMultipleObjects {
    TestObject *obj1 = [[TestObject alloc] init];
    TestObject *obj2 = [[TestObject alloc] init];
    
    obj1.customDescription = @"Description1";
    obj2.customDescription = @"Description2";
    
    const char *result1 = lookup_description_for_address((__bridge void *)obj1, [TestObject class]);
    const char *result2 = lookup_description_for_address((__bridge void *)obj2, [TestObject class]);
    
    XCTAssertNotEqual(result1, NULL);
    XCTAssertNotEqual(result2, NULL);
    XCTAssertNotEqual(result1, result2);
    XCTAssertEqual(strcmp(result1, "Description1"), 0);
    XCTAssertEqual(strcmp(result2, "Description2"), 0);
}

- (void)testConcurrentAccess {
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
    dispatch_group_t group = dispatch_group_create();
    
    TestObject *obj = [[TestObject alloc] init];
    obj.customDescription = @"ConcurrentTest";
    
    const char *initialResult = lookup_description_for_address((__bridge void *)obj, [TestObject class]);
    XCTAssertNotEqual(initialResult, NULL);
    
    for (int i = 0; i < 100; i++) {
        dispatch_group_async(group, queue, ^{
            const char *result = lookup_description_for_address((__bridge void *)obj, [TestObject class]);
            XCTAssertNotEqual(result, NULL);
            if (result != NULL) {
                XCTAssertEqual(strncmp(result, "ConcurrentTest", strlen("ConcurrentTest")), 0);
            }
        });
    }
    
    XCTAssertEqual(dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)), 0);
}

- (void)testIMPCacheConsistency {
    Class testClass = [TestObject class];
    
    for (int i = 0; i < 100; i++) {
        TestObject *obj = [[TestObject alloc] init];
        obj.customDescription = [NSString stringWithFormat:@"IMPTest%d", i];
        const char *result = lookup_description_for_address((__bridge void *)obj, testClass);
        XCTAssertNotEqual(result, NULL);
        XCTAssertTrue(strstr(result, "IMPTest") != NULL);
    }
}

@end
