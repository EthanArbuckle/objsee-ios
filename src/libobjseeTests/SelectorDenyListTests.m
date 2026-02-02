//
//  SelectorDenyListTests.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/9/25.
//

#import <XCTest/XCTest.h>
#import "selector_deny_list.h"

@interface SelectorDenyListTests : XCTestCase
@end

@implementation SelectorDenyListTests

- (void)testNullSelector {
    bool result = should_skip_selector_name(NULL);
    XCTAssertFalse(result);
}

- (void)testDotPrefixSelector {
    SEL dotSelector = NSSelectorFromString(@".cxx_destruct");
    bool result = should_skip_selector_name(dotSelector);
    XCTAssertTrue(result);
}

- (void)testSPrefixSelector {
    SEL sSelector = NSSelectorFromString(@"someMethod");
    bool result = should_skip_selector_name(sSelector);
    XCTAssertFalse(result);
}

- (void)testKnownDenylistedSelectors {
    SEL denylistedSelectors[] = {
        sel_registerName("isKindOfClass:"),
        sel_registerName("zone"),
        sel_registerName("release"),
        sel_registerName("allocWithZone:"),
        sel_registerName("_tryRetain"),
        sel_registerName("retainCount"),
        sel_registerName("retain"),
        sel_registerName("class"),
        sel_registerName("_xref_dispose"),
        sel_registerName(".cxx_destruct"),
        sel_registerName("alloc"),
        sel_registerName("autorelease"),
        sel_registerName("dealloc"),
        sel_registerName("_isDeallocating"),
    };

    for (size_t i = 0; i < sizeof(denylistedSelectors) / sizeof(SEL); i++) {
        bool result = should_skip_selector_name(denylistedSelectors[i]);
        XCTAssertTrue(result, @"Selector %@ should be denylisted", NSStringFromSelector(denylistedSelectors[i]));
    }
}

- (void)testCommonNonDenylistedSelectors {
    SEL allowedSelectors[] = {
        sel_registerName("description"),
        sel_registerName("init"),
        sel_registerName("setObject:forKey:"),
        sel_registerName("count"),
        sel_registerName("length")
    };

    for (size_t i = 0; i < sizeof(allowedSelectors) / sizeof(SEL); i++) {
        bool result = should_skip_selector_name(allowedSelectors[i]);
        XCTAssertFalse(result, @"Selector %@ should not be denylisted", NSStringFromSelector(allowedSelectors[i]));
    }
}

- (void)testBinarySearchEdgeCases {
    SEL firstInList = sel_registerName("isKindOfClass:");
    bool result = should_skip_selector_name(firstInList);
    XCTAssertTrue(result);
    
    SEL lastInList = sel_registerName("_isDeallocating");
    result = should_skip_selector_name(lastInList);
    XCTAssertTrue(result);
}

- (void)testHashCollisionHandling {
    char selectorBuffer[256];
    for (uint32_t i = 0; i < 1000; i++) {
        snprintf(selectorBuffer, sizeof(selectorBuffer), "test_method_%u", i);
        SEL testSelector = sel_registerName(selectorBuffer);
        should_skip_selector_name(testSelector);
    }
}

@end
