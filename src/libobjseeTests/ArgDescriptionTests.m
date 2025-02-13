//
//  ArgDescriptionTests.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/9/25.
//
#import <XCTest/XCTest.h>
#import "objc_arg_description.h"
#import "arg_description.h"
#import "tracer_internal.h"
#import "structs.h"

@interface ArgDescriptionTests : XCTestCase
@end

@implementation ArgDescriptionTests {
    char outputBuffer[256];
}

- (void)setUp {
    memset(outputBuffer, 0, sizeof(outputBuffer));
}

- (void)testNullArguments {
    tracer_argument_t arg;
    XCTAssertEqual(description_for_argument(NULL, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_INVALID_ARGUMENT);
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, NULL, sizeof(outputBuffer)), KERN_INVALID_ARGUMENT);
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, 0), KERN_INVALID_ARGUMENT);
}

- (void)testIdDescription {
    NSObject *obj = [[NSObject alloc] init];
    tracer_argument_t arg = { .type_encoding = "@", .address = &obj, .objc_class_name = "NSObject" };
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_CLASS, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "NSObject") != NULL);
}

- (void)testNilIdDescription {
    tracer_argument_t arg = { .type_encoding = "@", .address = NULL, .objc_class_name = NULL };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertEqual(strcmp(outputBuffer, "nil"), 0);
}

- (void)testSelectorDescription {
    SEL selector = @selector(description);
    tracer_argument_t arg = { .type_encoding = ":", .address = &selector };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "@selector(description)") != NULL);
}

- (void)testClassDescription {
    Class cls = [NSString class];
    tracer_argument_t arg = { .type_encoding = "#", .address = &cls };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_CLASS, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "NSString") != NULL);
}

- (void)testFloatDescription {
    float value = 3.14f;
    tracer_argument_t arg = { .type_encoding = "f", .address = &value };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "3.14") != NULL);
}

- (void)testPointerDescription {
    int x = 42;
    void *ptr = &x;
    tracer_argument_t arg = { .type_encoding = "^", .address = &ptr };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "0x") != NULL);
}

- (void)testStructDescription {
    struct { int a; float b; } myStruct = { 5, 2.5f };
    tracer_argument_t arg = { .type_encoding = "{fooBarStruct=if}", .address = &myStruct };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "{0x") != NULL);
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_DESCRIPTIVE, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "fooBarStruct { int, float}") != NULL);
}

- (void)testBooleanDescription {
    BOOL val = YES;
    tracer_argument_t arg = { .type_encoding = "B", .address = &val };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertEqual(strcmp(outputBuffer, "true"), 0);
}

- (void)testLongLongDescription {
    long long value = 9223372036854775807LL;
    tracer_argument_t arg = { .type_encoding = "q", .address = &value };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "9223372036854775807") != NULL);
}

- (void)testCharPointerDescription {
    const char *string = "foorbar";
    tracer_argument_t arg = { .type_encoding = "*", .address = &string };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_DESCRIPTIVE, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertEqual(strcmp(outputBuffer, "foorbar"), 0);
}

- (void)testUnsignedCharDescription {
    unsigned char value = 255;
    tracer_argument_t arg = { .type_encoding = "C", .address = &value };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "255") != NULL);
}

- (void)testCharDescription {
    char value = 'A';
    tracer_argument_t arg = { .type_encoding = "c", .address = &value };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "'A'") != NULL);
    
    value = '\n';
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "0x0a") != NULL);
}

- (void)testRecursiveQualifierHandling {
    int number = 42;
    tracer_argument_t arg = { .type_encoding = "rS", .address = &number };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_SUCCESS);
    XCTAssertTrue(strstr(outputBuffer, "42") != NULL);
}

- (void)testBadBufferSize {
    long long number = 42;
    tracer_argument_t arg = { .type_encoding = "q", .address = &number };
    
    char smallBuffer[1];  // Too small for full output
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, smallBuffer, sizeof(smallBuffer)), KERN_NO_SPACE);
}

- (void)testInvalidEncoding {
    tracer_argument_t arg = { .type_encoding = "X", .address = NULL };
    
    XCTAssertEqual(description_for_argument(&arg, TRACER_ARG_FORMAT_BASIC, outputBuffer, sizeof(outputBuffer)), KERN_INVALID_ARGUMENT);
}

@end
