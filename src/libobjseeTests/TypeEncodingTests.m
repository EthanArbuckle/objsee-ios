//
//  TypeEncodingTests.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/13/25.
//

#import <XCTest/XCTest.h>
#import <objc/runtime.h>
#import "encoding_size.h"

@interface TypeEncodingTests : XCTestCase
@end

@implementation TypeEncodingTests

- (void)testBasicTypes {
    XCTAssertEqual(get_size_of_type_from_type_encoding("c"), sizeof(char));
    XCTAssertEqual(get_size_of_type_from_type_encoding("i"), sizeof(int));
    XCTAssertEqual(get_size_of_type_from_type_encoding("s"), sizeof(short));
    XCTAssertEqual(get_size_of_type_from_type_encoding("l"), sizeof(long));
    XCTAssertEqual(get_size_of_type_from_type_encoding("q"), sizeof(long long));
    XCTAssertEqual(get_size_of_type_from_type_encoding("C"), sizeof(unsigned char));
    XCTAssertEqual(get_size_of_type_from_type_encoding("I"), sizeof(unsigned int));
    XCTAssertEqual(get_size_of_type_from_type_encoding("S"), sizeof(unsigned short));
    XCTAssertEqual(get_size_of_type_from_type_encoding("L"), sizeof(unsigned long));
    XCTAssertEqual(get_size_of_type_from_type_encoding("Q"), sizeof(unsigned long long));
    XCTAssertEqual(get_size_of_type_from_type_encoding("f"), sizeof(float));
    XCTAssertEqual(get_size_of_type_from_type_encoding("d"), sizeof(double));
    XCTAssertEqual(get_size_of_type_from_type_encoding("B"), sizeof(bool));
}

- (void)testPointerTypes {
    XCTAssertEqual(get_size_of_type_from_type_encoding("*"), sizeof(char *));
    XCTAssertEqual(get_size_of_type_from_type_encoding("@"), sizeof(id));
    XCTAssertEqual(get_size_of_type_from_type_encoding("#"), sizeof(Class));
    XCTAssertEqual(get_size_of_type_from_type_encoding(":"), sizeof(SEL));
    XCTAssertEqual(get_size_of_type_from_type_encoding("^v"), sizeof(void *));
    XCTAssertEqual(get_size_of_type_from_type_encoding("^c"), sizeof(char *));
}

- (void)testConstTypes {
    XCTAssertEqual(get_size_of_type_from_type_encoding("rc"), sizeof(char));
    XCTAssertEqual(get_size_of_type_from_type_encoding("ri"), sizeof(int));
    XCTAssertEqual(get_size_of_type_from_type_encoding("r*"), sizeof(char *));
    XCTAssertEqual(get_size_of_type_from_type_encoding("r@"), sizeof(id));
}

- (void)testSimpleStructs {
    typedef struct { double x, y; } Point;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{Point=dd}"), sizeof(Point));
    
    typedef struct { long long a; double b; } MixedStruct;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{Mixed=qd}"), sizeof(MixedStruct));
}

- (void)testNestedStructs {
    typedef struct {
        struct { double x, y; } point;
        struct { double width, height; } size;
    } Rectangle;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{Rectangle={Point=dd}{Size=dd}}"), sizeof(Rectangle));
    
    typedef struct {
        long long a;
        double b;
        struct { double x, y; } point;
    } ComplexStruct;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{Complex=qd{Point=dd}}"), sizeof(ComplexStruct));
}

- (void)testComplexStructs {
    typedef struct {
        long long a, b;
        double c;
        long long d, e, f, g;
        id obj;
        struct { double width, height; } size;
        long long h, i, j, k;
    } VeryComplexStruct;

    XCTAssertEqual(get_size_of_type_from_type_encoding("{?=qqdqqqqq@{CGSize=dd}qqq}"), sizeof(VeryComplexStruct));
}

- (void)testEdgeCases {
    XCTAssertEqual(get_size_of_type_from_type_encoding(NULL), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding(""), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding("{"), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding("{abc"), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding("{abc="), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding("v"), 0);
    XCTAssertEqual(get_size_of_type_from_type_encoding("?"), 0);
}

- (void)testBasicMethodSignatures {
    size_t offsets[32] = {0};
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("#24@0:8q16", offsets, 3), KERN_SUCCESS);
    XCTAssertEqual(offsets[0], 0);
    XCTAssertEqual(offsets[1], 8);
    XCTAssertEqual(offsets[2], 16);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("v24@0:8@16", offsets, 3), KERN_SUCCESS);
    XCTAssertEqual(offsets[0], 0);
    XCTAssertEqual(offsets[1], 8);
    XCTAssertEqual(offsets[2], 16);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("v32@0:8@16Q24", offsets, 4), KERN_SUCCESS);
    XCTAssertEqual(offsets[0], 0);
    XCTAssertEqual(offsets[1], 8);
    XCTAssertEqual(offsets[2], 16);
    XCTAssertEqual(offsets[3], 24);
}

- (void)testComplexMethodSignatures {
    size_t offsets[32] = {0};
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("v40@0:8@16@24Q32", offsets, 5), KERN_SUCCESS);
    XCTAssertEqual(offsets[0], 0);
    XCTAssertEqual(offsets[1], 8);
    XCTAssertEqual(offsets[2], 16);
    XCTAssertEqual(offsets[3], 24);
    XCTAssertEqual(offsets[4], 32);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("@32@0:8@16@24", offsets, 4), KERN_SUCCESS);
    XCTAssertEqual(offsets[0], 0);
    XCTAssertEqual(offsets[1], 8);
    XCTAssertEqual(offsets[2], 16);
    XCTAssertEqual(offsets[3], 24);
}

- (void)testMethodSignatureErrors {
    size_t offsets[32] = {0};
    XCTAssertEqual(get_offsets_of_args_using_type_encoding(NULL, offsets, 3), KERN_FAILURE);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("v24@0:8@16", NULL, 3), KERN_FAILURE);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("invalid", offsets, 3), KERN_FAILURE);
    
    memset(offsets, 0, sizeof(offsets));
    XCTAssertEqual(get_offsets_of_args_using_type_encoding("v32@0:8@16Q24", offsets, 6), KERN_FAILURE);
}

- (CGRect)CGRectValue {
    return CGRectMake(1, 2, 3, 4);
}

- (void)testRuntimeConsistency {
    NSMethodSignature *signature = [NSObject instanceMethodSignatureForSelector:@selector(description)];
    const char *returnType = [signature methodReturnType];
    XCTAssertEqual(get_size_of_type_from_type_encoding(returnType), sizeof(id));
    
    signature = [self methodSignatureForSelector:@selector(CGRectValue)];
    returnType = [signature methodReturnType];
    XCTAssertEqual(get_size_of_type_from_type_encoding(returnType), sizeof(CGRect));
}

- (void)testAlignment {
    typedef struct { char a; double b; } AlignedStruct1;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{AS1=cd}"), sizeof(AlignedStruct1));
    
    typedef struct { char a; long long b; } AlignedStruct2;
    XCTAssertEqual(get_size_of_type_from_type_encoding("{AS2=cq}"), sizeof(AlignedStruct2));
}

@end
