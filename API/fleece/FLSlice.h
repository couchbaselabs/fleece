//
//  FLSlice.h
//  Fleece
//
//  Created by Jens Alfke on 8/13/18.
//  Copyright © 2018 Couchbase. All rights reserved.
//

#pragma once
#include "Base.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#ifdef __cplusplus
    #define FLAPI noexcept
    namespace fleece { struct alloc_slice; }
#else
    #define FLAPI
#endif


#ifdef __cplusplus
extern "C" {
#endif


/** \defgroup FLSlice    Slices
    @{ */


/** A simple reference to a block of memory. Does not imply ownership.
    (This is equivalent to the C++ class `slice`.) */
typedef struct FLSlice {
    const void *buf;
    size_t size;
} FLSlice;


/** A block of memory returned from an API call. The caller takes ownership, and must call
    FLSlice_Release (or FLSlice_Free) when done. The heap block may be shared with other users,
    so it must not be modified.
    (This is equivalent to the C++ class `alloc_slice`.) */
typedef struct FLSliceResult {
    const void *buf;
    size_t size;

#ifdef __cplusplus
    explicit operator FLSlice () const FLAPI {return {buf, size};}
#endif
} FLSliceResult;


/** A heap-allocated, reference-counted slice. This type is really just a hint in an API
    that the data can be retained instead of copied, by assigning it to an alloc_slice.
    You can just treat it like FLSlice. */
#ifdef __cplusplus
    struct FLHeapSlice : public FLSlice {
        FLHeapSlice() FLAPI                           {buf = nullptr; size = 0;}
    private:
        FLHeapSlice(const void *b, size_t s) FLAPI    {buf = b; size = s;}
        friend struct fleece::alloc_slice;
    };
#else
    typedef FLSlice FLHeapSlice;
#endif


typedef FLSlice FLString;
typedef FLSliceResult FLStringResult;


/** A convenient constant denoting a null slice. */
#ifdef _MSC_VER
    static const FLSlice kFLSliceNull = { NULL, 0 };
#else
    #define kFLSliceNull ((FLSlice){NULL, 0})
#endif


/** Exactly like memcmp, but safely handles the case where a or b is NULL and size is 0 (by returning 0),
    instead of producing "undefined behavior" as per the C spec. */
static inline FLPURE int FLMemCmp(const void *a, const void *b, size_t size) FLAPI {
    if (_usuallyFalse(size == 0))
        return 0;
    return memcmp(a, b, size);
}

/** Exactly like memcmp, but safely handles the case where dst or src is NULL and size is 0 (as a no-op),
    instead of producing "undefined behavior" as per the C spec. */
static inline void FLMemCpy(void *dst, const void *src, size_t size) FLAPI {
    if (_usuallyTrue(size > 0))
        memcpy(dst, src, size);
}


/** Returns a slice pointing to the contents of a C string.
    It's OK to pass NULL; this returns an empty slice.
    (Performance is O(n) with the length of the string, since it has to call `strlen`.) */
static inline FLSlice FLStr(const char *str) FLAPI {
    FLSlice foo = { str, str ? strlen(str) : 0 };
    return foo;
}

// Macro version of FLStr, for use in initializing compile-time constants.
// STR must be a C string literal. Has zero runtime overhead.
#ifdef __cplusplus
    #define FLSTR(STR) (FLSlice {("" STR), sizeof(("" STR))-1})
#else
    #define FLSTR(STR) ((FLSlice){("" STR), sizeof(("" STR))-1})
#endif


/** Equality test of two slices. */
bool FLSlice_Equal(FLSlice a, FLSlice b) FLAPI FLPURE;

/** Lexicographic comparison of two slices; basically like memcmp(), but taking into account
    differences in length. */
int FLSlice_Compare(FLSlice, FLSlice) FLAPI FLPURE;

/** Allocates an FLSliceResult of the given size, without initializing the buffer. */
FLSliceResult FLSliceResult_New(size_t) FLAPI;

/** Allocates an FLSliceResult, copying the given slice. */
FLSliceResult FLSlice_Copy(FLSlice) FLAPI;

void _FLBuf_Retain(const void*) FLAPI;   // internal; do not call
void _FLBuf_Release(const void*) FLAPI;  // internal; do not call

/** Increments the ref-count of a FLSliceResult. */
static inline FLSliceResult FLSliceResult_Retain(FLSliceResult s) FLAPI {
    _FLBuf_Retain(s.buf);
    return s;
}

/** Decrements the ref-count of a FLSliceResult, freeing its memory if it reached zero. */
static inline void FLSliceResult_Release(FLSliceResult s) FLAPI {
    _FLBuf_Release(s.buf);
}


/** @} */

#ifdef __cplusplus
}
#endif
