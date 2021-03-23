//
// Base.h
//
// Copyright © 2018 Couchbase. All rights reserved.
//

#pragma once
#ifndef FLEECE_BASE_H
#define FLEECE_BASE_H

// The __has_xxx() macros are only(?) implemented by Clang. (Except GCC has __has_attribute...)
// Define them to return 0 on other compilers.

#ifndef __has_attribute
    #define __has_attribute(x) 0
#endif

#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#ifndef __has_extension
    #define __has_extension(x) 0
#endif


// https://clang.llvm.org/docs/AttributeReference.html
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html

#if defined(__clang__) || defined(__GNUC__)
    // Tells the optimizer that a function's return value is never NULL.
    #define RETURNS_NONNULL                 __attribute__((returns_nonnull))

    // Triggers a compile error if a call to the function ignores the returned value.
    // Typically this is because the return value is something that must be released/freed.
    #define MUST_USE_RESULT                 __attribute__((warn_unused_result))

    // These have no effect on behavior, but they hint to the optimizer which branch of an 'if'
    // statement to make faster.
    #define _usuallyTrue(VAL)               __builtin_expect(VAL, true)
    #define _usuallyFalse(VAL)              __builtin_expect(VAL, false)
#else
    #define RETURNS_NONNULL
    #define MUST_USE_RESULT

    #define _usuallyTrue(VAL)               (VAL)
    #define _usuallyFalse(VAL)              (VAL)
#endif

// Declares that a parameter must not be NULL. The compiler can sometimes detect violations
// of this at compile time, if the parameter value is a literal.
// The Clang Undefined-Behavior Sanitizer will detect all violations at runtime.
#ifdef __clang__
    #define NONNULL                     __attribute__((nonnull))
#else
    // GCC's' `nonnull` works differently (not as well: requires parameter numbers be given)
    #define NONNULL
#endif


// FLPURE functions are _read-only_. They cannot write to memory (in a way that's detectable),
// and they cannot access volatile data or do I/O.
//
// Calling an FLPURE function twice in a row with the same arguments must return the same result.
//
// "Many functions have no effects except the return value, and their return value depends only on
//  the parameters and/or global variables. Such a function can be subject to common subexpression
//  elimination and loop optimization just as an arithmetic operator would be. These functions
//  should be declared with the attribute pure."
// "The pure attribute prohibits a function from modifying the state of the program that is
//  observable by means other than inspecting the function’s return value. However, functions
//  declared with the pure attribute can safely read any non-volatile objects, and modify the value
//  of objects in a way that does not affect their return value or the observable state of the
//  program." -- GCC manual
#if defined(__GNUC__) || __has_attribute(__pure__)
    #define FLPURE                      __attribute__((__pure__))
#else
    #define FLPURE
#endif

// FLCONST is even stricter than FLPURE. The function cannot access memory at all (except for
// reading immutable values like constants.) The return value can only depend on the parameters.
//
// Calling an FLCONST function with the same arguments must _always_ return the same result.
//
// "Calls to functions whose return value is not affected by changes to the observable state of the
//  program and that have no observable effects on such state other than to return a value may lend
//  themselves to optimizations such as common subexpression elimination. Declaring such functions
//  with the const attribute allows GCC to avoid emitting some calls in repeated invocations of the
//  function with the same argument values."
// "Note that a function that has pointer arguments and examines the data pointed to must not be
//  declared const if the pointed-to data might change between successive invocations of the
//  function.
// "In general, since a function cannot distinguish data that might change from data that cannot,
//  const functions should never take pointer or, in C++, reference arguments. Likewise, a function
//  that calls a non-const function usually must not be const itself." -- GCC manual
#if defined(__GNUC__) || __has_attribute(__const__)
    #define FLCONST                     __attribute__((__const__))
#else
    #define FLCONST
#endif


// `constexpr14` is for uses of `constexpr` that are valid in C++14 but not earlier.
// In constexpr functions this includes `if`, `for`, `while` statements; or multiple `return`s.
// The macro expands to `constexpr` in C++14 or later, otherwise to nothing.
#ifdef __cplusplus
    #if __cplusplus >= 201400L || _MSVC_LANG >= 201400L
        #define constexpr14 constexpr
    #else
        #define constexpr14
    #endif
#endif // __cplusplus

#else // FLEECE_BASE_H
    #warn "Compiler is not honoring #pragma once"
#endif

// Used for functions that are annoying to step into in the debugger, like RefCounted's operator->,
// or slice constructors. Suppressing debug info for those functions means the debugger will
// continue through them when stepping in.
#if __has_attribute(nodebug)
    #define STEPOVER __attribute((nodebug))
#else
    #define STEPOVER
#endif

// Marks a function as being a hot-spot. Optimizes it for speed and may move it to a common
// code section for hot functions. Has no effect in an unoptimized build.
#ifndef __hot
#   if defined(__OPTIMIZE__)
#       if defined(__clang__) && !__has_attribute(__hot_) \
        && __has_attribute(__section__) && (defined(__linux__) || defined(__gnu_linux__))
            /* just put frequently used functions in separate section */
#           define __hot __attribute__((__section__("text.hot"))) __optimize(HOTLEVEL)
#       elif defined(__GNUC__) || __has_attribute(__hot__)
#           define __hot __attribute__((__hot__)) __optimize(HOTLEVEL)
#       else
#           define __hot  __optimize(HOTLEVEL)
#       endif
#   else
#       define __hot
#   endif
#endif /* __hot */

// Marks a function as being rarely used (e.g. error handling.) Optimizes it for size and
// moves it to a common code section for cold functions. Has no effect in an unoptimized build.
#ifndef __cold
#   if defined(__OPTIMIZE__)
#       if defined(__clang__) && !__has_attribute(cold) \
        && __has_attribute(__section__) && (defined(__linux__) || defined(__gnu_linux__))
            /* just put infrequently used functions in separate section */
#           define __cold __attribute__((__section__("text.unlikely"))) __optimize(COLDLEVEL)
#       elif defined(__GNUC__) || __has_attribute(cold)
#           define __cold __attribute__((__cold__)) __optimize(COLDLEVEL)
#       else
#           define __cold __optimize(COLDLEVEL)
#       endif
#   else
#       define __cold
#   endif
#endif /* __cold */

#ifndef _MSC_VER
    #define WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) 0
#endif
