//
//  objc-internal.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/13/25.
//

#ifndef objc_internal_h
#define objc_internal_h

#include <CoreFoundation/CoreFoundation.h>

#define _OBJC_TAG_MASK (1UL<<63)
#define _OBJC_TAG_INDEX_SHIFT 0
#define _OBJC_TAG_SLOT_SHIFT 0
#define _OBJC_TAG_PAYLOAD_LSHIFT 1
#define _OBJC_TAG_PAYLOAD_RSHIFT 4
#define _OBJC_TAG_EXT_MASK (_OBJC_TAG_MASK | 0x7UL)
#define _OBJC_TAG_NO_OBFUSCATION_MASK ((1UL<<62) | _OBJC_TAG_EXT_MASK)
#define _OBJC_TAG_CONSTANT_POINTER_MASK ~(_OBJC_TAG_EXT_MASK | ((uintptr_t)_OBJC_TAG_EXT_SLOT_MASK << _OBJC_TAG_EXT_SLOT_SHIFT))
#define _OBJC_TAG_EXT_INDEX_SHIFT 55
#define _OBJC_TAG_EXT_SLOT_SHIFT 55
#define _OBJC_TAG_EXT_PAYLOAD_LSHIFT 9
#define _OBJC_TAG_EXT_PAYLOAD_RSHIFT 12

static inline bool _objc_isTaggedPointer(const void * _Nullable ptr) {
    return ((uintptr_t)ptr & _OBJC_TAG_MASK) == _OBJC_TAG_MASK;
}


#endif /* objc_internal_h */
