//
//  BlockDescriptionTests.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/9/25.
//

#import <XCTest/XCTest.h>
#import "blocks.h"

@interface BlockDescriptionTests : XCTestCase
@end

@implementation BlockDescriptionTests

- (void)_assertBlock:(id)block matches:(const char *)expected {
    char *decoded_block_signature = NULL;
    XCTAssertEqual(get_block_description(block, &decoded_block_signature), KERN_SUCCESS);
    XCTAssertEqualObjects(@(decoded_block_signature), @(expected));
    free(decoded_block_signature);
}

- (void)testBasicBlock {
    void (^block)(void) = ^{};
    [self _assertBlock:block matches:"(void (^)(void))"];
}

- (void)testParameterBlock {
    int (^block)(int, float) = ^(int x, float y) { return x; };
    [self _assertBlock:block matches:"(int (^)(int, float))"];
}

- (void)testObjectBlock {
    void (^block)(id, NSString *) = ^(id obj, NSString *str) {};
    [self _assertBlock:block matches:"(void (^)(id, NSString))"];
}

- (void)testPointerBlock {
    void (^block)(int *, char **) = ^(int *x, char **y) {};
    [self _assertBlock:block matches:"(void (^)(int *, char **))"];
}

- (void)testNullBlock {
    XCTAssertEqual(get_block_description(NULL, NULL), KERN_INVALID_ARGUMENT);
}

- (void)testInvalidBlock {
    char *decoded_block_signature = NULL;
    XCTAssertEqual(get_block_description((__bridge id)((void *)0x1), &decoded_block_signature), KERN_INVALID_ARGUMENT);
}

- (void)testLongTypes {
    long long (^block)(long long, long long) = ^(long long x, long long y) { return y; };
    [self _assertBlock:block matches:"(long long (^)(long long, long long))"];
}

- (void)testBOOLType {
    BOOL (^block)(BOOL) = ^(BOOL x) { return x; };
    [self _assertBlock:block matches:"(BOOL (^)(BOOL))"];
}

@end
