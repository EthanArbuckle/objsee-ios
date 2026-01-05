//
//  StructDecoderTests.m
//  libobjseeTests
//
//  Created by Ethan Arbuckle on 1/11/25.
//

#import <XCTest/XCTest.h>
#import "encoding_description.h"

@interface StructDecoderTests : XCTestCase
@end

@implementation StructDecoderTests

- (void)setUp {
    [super setUp];
}

- (void)tearDown {
    [super tearDown];
}

- (void)testBasicTypes {
    XCTAssertEqualObjects(@"int", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("i")]);
    XCTAssertEqualObjects(@"char", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("c")]);
    XCTAssertEqualObjects(@"long", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("l")]);
    XCTAssertEqualObjects(@"float", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("f")]);
    XCTAssertEqualObjects(@"double", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("d")]);
    
    XCTAssertEqualObjects(@"unsigned int", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("I")]);
    XCTAssertEqualObjects(@"unsigned char", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("C")]);
    XCTAssertEqualObjects(@"unsigned long", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("L")]);
    
    XCTAssertEqualObjects(@"id", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("@")]);
    XCTAssertEqualObjects(@"Class", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("#")]);
    XCTAssertEqualObjects(@"SEL", [NSString stringWithUTF8String:get_struct_description_from_type_encoding(":")]);
}

- (void)testPointerTypes {
    XCTAssertEqualObjects(@"char *", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("*")]);
    XCTAssertEqualObjects(@"int *", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("^i")]);
    XCTAssertEqualObjects(@"double *", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("^d")]);
    XCTAssertEqualObjects(@"const int *", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("^ri")]);
}

- (void)testSimpleStructs {
    XCTAssertEqualObjects(@"CGPoint { double, double }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGPoint=dd}")]);
    XCTAssertEqualObjects(@"CGSize { double, double }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGSize=dd}")]);
    XCTAssertEqualObjects(@"struct { int, char }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{?=ic}")]);
}

- (void)testComplexStructs {
    XCTAssertEqualObjects(@"ComplexStruct { int, char *, double }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{ComplexStruct=i*d}")]);
    XCTAssertEqualObjects(@"OuterStruct { int, struct { double, double }, char }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{OuterStruct=i{?=dd}c}")]);
    XCTAssertEqualObjects(@"ConstStruct { const int, double }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{ConstStruct=rid}")]);
    XCTAssertEqualObjects(@"CGRect { CGPoint { double, double }, CGSize { double, double } }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGRect={CGPoint=dd}{CGSize=dd}}")]);
    XCTAssertEqualObjects(@"struct { long long, long long, double, long long, long long, long long, long long, long long, id, CGSize { double, double }, long long, long long, long long }", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{?=qqdqqqqq@{CGSize=dd}qqq}")]);
}

- (void)testEdgeCases {
    XCTAssertEqualObjects(@"invalid_encoding", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("")]);
    XCTAssertEqualObjects(@"invalid_encoding", [NSString stringWithUTF8String:get_struct_description_from_type_encoding(NULL)]);
    XCTAssertEqualObjects(@"unknown_type", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("x")]);
    XCTAssertNotNil([NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGPoint=d")]);
}

- (void)testCGPointWithValues {
    struct { double x; double y; } point = { 375.0, 812.0 };
    char *result = get_struct_description_with_values("{CGPoint=dd}", &point);
    XCTAssertEqualObjects(@"CGPoint { 375.0, 812.0 }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCGRectWithValues {
    struct { double x; double y; double w; double h; } rect = { 0.0, 0.0, 375.0, 812.0 };
    char *result = get_struct_description_with_values("{CGRect={CGPoint=dd}{CGSize=dd}}", &rect);
    XCTAssertEqualObjects(@"CGRect { CGPoint { 0.0, 0.0 }, CGSize { 375.0, 812.0 } }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testIntWithValue {
    int value = 42;
    char *result = get_struct_description_with_values("i", &value);
    XCTAssertEqualObjects(@"42", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatWithValue {
    float value = 3.14159f;
    char *result = get_struct_description_with_values("f", &value);
    XCTAssertTrue([[NSString stringWithUTF8String:result] hasPrefix:@"3.14"]);
    free(result);
}

- (void)testBoolWithValue {
    _Bool value = true;
    char *result = get_struct_description_with_values("B", &value);
    XCTAssertEqualObjects(@"true", [NSString stringWithUTF8String:result]);
    free(result);
    
    value = false;
    result = get_struct_description_with_values("B", &value);
    XCTAssertEqualObjects(@"false", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testIdWithNilValue {
    id value = nil;
    char *result = get_struct_description_with_values("@", &value);
    XCTAssertEqualObjects(@"nil", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCharPointerWithStringValue {
    const char *str = "hello";
    char *result = get_struct_description_with_values("*", &str);
    XCTAssertEqualObjects(@"\"hello\"", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCharPointerWithNullValue {
    const char *str = NULL;
    char *result = get_struct_description_with_values("*", &str);
    XCTAssertEqualObjects(@"NULL", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testStructWithMixedTypes {
    struct {
        int32_t a;
        double b;
        int32_t c;
    } mixed = { 10, 20.5, 30 };
    char *result = get_struct_description_with_values("{Mixed=idi}", &mixed);
    XCTAssertEqualObjects(@"Mixed { 10, 20.5, 30 }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testNullDataFallsBackToTypeDescription {
    char *result = get_struct_description_with_values("{CGPoint=dd}", NULL);
    XCTAssertEqualObjects(@"CGPoint { double, double }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testLongLongWithValue {
    int64_t value = 9223372036854775807LL;
    char *result = get_struct_description_with_values("q", &value);
    XCTAssertEqualObjects(@"9223372036854775807", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testUnsignedLongLongWithValue {
    uint64_t value = 18446744073709551615ULL;
    char *result = get_struct_description_with_values("Q", &value);
    XCTAssertEqualObjects(@"18446744073709551615", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testNestedStructWithValues {
    struct {
        struct { double x; double y; } origin;
        struct { double w; double h; } size;
    } rect = { { 10.0, 20.0 }, { 100.0, 200.0 } };
    char *result = get_struct_description_with_values("{CGRect={CGPoint=dd}{CGSize=dd}}", &rect);
    XCTAssertEqualObjects(@"CGRect { CGPoint { 10.0, 20.0 }, CGSize { 100.0, 200.0 } }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCharWithPrintableValue {
    char value = 'A';
    char *result = get_struct_description_with_values("c", &value);
    XCTAssertEqualObjects(@"'A'", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCharWithNonPrintableValue {
    char value = 1;
    char *result = get_struct_description_with_values("c", &value);
    XCTAssertEqualObjects(@"1", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testMemoryManagement {

    for (int i = 0; i < 1000; i++) {
        char *result = get_struct_description_from_type_encoding("{CGPoint=dd}");
        XCTAssertNotNil([NSString stringWithUTF8String:result]);
        free(result);
    }
    
    NSMutableString *largeStruct = [NSMutableString stringWithString:@"{LargeStruct="];
    for (int i = 0; i < 100; i++) {
        [largeStruct appendString:@"i"];
    }
    [largeStruct appendString:@"}"];
    
    char *result = get_struct_description_from_type_encoding([largeStruct UTF8String]);
    XCTAssertNotNil([NSString stringWithUTF8String:result]);
    free(result);
}


- (void)testFloatInfinity {
    float val = INFINITY;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"INFINITY", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatNegativeInfinity {
    float val = -INFINITY;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"-INFINITY", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatNAN {
    float val = NAN;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"NAN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatMax {
    float val = FLT_MAX;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"FLT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatNegativeMax {
    float val = -FLT_MAX;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"-FLT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatMin {
    float val = FLT_MIN;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"FLT_MIN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatNegativeMin {
    float val = -FLT_MIN;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"-FLT_MIN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatEpsilon {
    float val = FLT_EPSILON;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"FLT_EPSILON", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatCAInfiniteSentinel {
    uint32_t bits = 0x7f000000;
    float val;
    memcpy(&val, &bits, sizeof(float));
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testFloatCANegativeInfiniteSentinel {
    uint32_t bits = 0xff000000;
    float val;
    memcpy(&val, &bits, sizeof(float));
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"-CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleInfinity {
    double val = INFINITY;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"INFINITY", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleNegativeInfinity {
    double val = -INFINITY;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"-INFINITY", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleNAN {
    double val = NAN;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"NAN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleMax {
    double val = DBL_MAX;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleNegativeMax {
    double val = -DBL_MAX;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"-CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleMin {
    double val = DBL_MIN;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"CGFLOAT_MIN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleNegativeMin {
    double val = -DBL_MIN;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"-CGFLOAT_MIN", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleEpsilon {
    double val = DBL_EPSILON;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"DBL_EPSILON", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleCAInfiniteSentinel {
    uint64_t bits = 0x7fe0000000000000ULL;
    double val;
    memcpy(&val, &bits, sizeof(double));
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testDoubleCANegativeInfiniteSentinel {
    uint64_t bits = 0xffe0000000000000ULL;
    double val;
    memcpy(&val, &bits, sizeof(double));
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"-CGFLOAT_MAX", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCGPointWithCAInfiniteOrigin {
    struct {
        float x;
        float y;
    } point;
    uint32_t neg_sentinel = 0xff000000;
    memcpy(&point.x, &neg_sentinel, sizeof(float));
    memcpy(&point.y, &neg_sentinel, sizeof(float));
    char *result = get_struct_description_with_values("{CGPoint=ff}", &point);
    XCTAssertEqualObjects(@"CGPoint { -CGFLOAT_MAX, -CGFLOAT_MAX }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testCGRectWithCAInfiniteValues {
    struct {
        float x;
        float y;
        float w;
        float h;
    } rect;
    uint32_t neg_sentinel = 0xff000000;
    uint32_t pos_sentinel = 0x7f000000;
    memcpy(&rect.x, &neg_sentinel, sizeof(float));
    memcpy(&rect.y, &neg_sentinel, sizeof(float));
    rect.w = 0.0f;
    memcpy(&rect.h, &pos_sentinel, sizeof(float));
    char *result = get_struct_description_with_values("{CGRect={CGPoint=ff}{CGSize=ff}}", &rect);
    XCTAssertEqualObjects(@"CGRect { CGPoint { -CGFLOAT_MAX, -CGFLOAT_MAX }, CGSize { 0.0, CGFLOAT_MAX } }", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testRegularFloatNotMisidentified {
    float val = 1.5f;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"1.5", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testRegularDoubleNotMisidentified {
    double val = 3.14159;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertTrue([[NSString stringWithUTF8String:result] hasPrefix:@"3.14"]);
    free(result);
}

- (void)testLargeFloatNotMisidentified {
    float val = 1.0e+30f;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertTrue([[NSString stringWithUTF8String:result] containsString:@"e+"]);
    XCTAssertFalse([[NSString stringWithUTF8String:result] containsString:@"MAX"]);
    free(result);
}

- (void)testLargeDoubleNotMisidentified {
    double val = 1.0e+200;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertTrue([[NSString stringWithUTF8String:result] containsString:@"e+"]);
    XCTAssertFalse([[NSString stringWithUTF8String:result] containsString:@"MAX"]);
    free(result);
}

- (void)testZeroFloat {
    float val = 0.0f;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"0.0", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testNegativeZeroFloat {
    float val = -0.0f;
    char *result = get_struct_description_with_values("f", &val);
    XCTAssertEqualObjects(@"-0.0", [NSString stringWithUTF8String:result]);
    free(result);
}

- (void)testZeroDouble {
    double val = 0.0;
    char *result = get_struct_description_with_values("d", &val);
    XCTAssertEqualObjects(@"0.0", [NSString stringWithUTF8String:result]);
    free(result);
}

@end
