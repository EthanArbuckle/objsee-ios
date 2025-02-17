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
    XCTAssertEqualObjects(@"CGPoint { double, double}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGPoint=dd}")]);
    XCTAssertEqualObjects(@"CGSize { double, double}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGSize=dd}")]);
    XCTAssertEqualObjects(@"struct { int, char}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{?=ic}")]);
}

- (void)testComplexStructs {
    XCTAssertEqualObjects(@"ComplexStruct { int, char *, double}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{ComplexStruct=i*d}")]);
    XCTAssertEqualObjects(@"OuterStruct { int, struct { double, double}, char}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{OuterStruct=i{?=dd}c}")]);
    XCTAssertEqualObjects(@"ConstStruct { const int, double}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{ConstStruct=rid}")]);
    XCTAssertEqualObjects(@"CGRect { CGPoint { double, double}, CGSize { double, double}}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGRect={CGPoint=dd}{CGSize=dd}}")]);
    XCTAssertEqualObjects(@"struct { long long, long long, double, long long, long long, long long, long long, long long, id, CGSize { double, double}, long long, long long, long long}", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("{?=qqdqqqqq@{CGSize=dd}qqq}")]);
}

- (void)testEdgeCases {
    XCTAssertEqualObjects(@"invalid_encoding", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("")]);
    XCTAssertEqualObjects(@"invalid_encoding", [NSString stringWithUTF8String:get_struct_description_from_type_encoding(NULL)]);
    XCTAssertEqualObjects(@"unknown_type", [NSString stringWithUTF8String:get_struct_description_from_type_encoding("x")]);
    XCTAssertNotNil([NSString stringWithUTF8String:get_struct_description_from_type_encoding("{CGPoint=d")]);
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

@end
