//
// Builder.hh
//
// Copyright © 2021 Couchbase. All rights reserved.
//

#pragma once
#include "FleeceImpl.hh"
#include "slice_stream.hh"
#include "PlatformCompat.hh"
#include <stdarg.h>

namespace fleece::impl::builder {

    /** Creates a MutableArray or MutableDict by reading the format string and following arguments.
        The format string is basically JSON5, except that any value in it may be a printf-style
        '%' specifier instead of a literal, in which case that value will be read from the next
        argument. The supported format specifiers are:
        - Boolean:           `%c` (cast the arg to `char` to avoid a compiler warning)
        - Integer:           `%i` or `%d` (use size specifiers `l`, `ll`, or `z`)
        - Unsigned integer:  `%u` (use size specifiers `l`, `ll`, or `z`)
        - Floating point:    `%f` (arg can be `float` or `double`; no size spec needed)
        - C string:          `%s`
        - Ptr+length string: `%.*s` (takes two args, a `const char*` and an `int`. See FMTSLICE.)
        - Fleece value:      `%p` (arg must be a `const Value*` or `FLValue`)

        A `-` can appear after the `%`, indicating that the argument should be ignored if it has
        a default value, namely `false`, 0, or an empty string. This means the corresponding item
        won't be written (a Dict item will be erased if it previously existed.)

        If a string/value specifier is given a NULL pointer, nothing is written, and any
        pre-existing Dict item will be removed.

        \note It's legal for a Dict key to be repeated; later occurrences take precedence,
            i.e. each one overwrites the last.

        @param format  The format string. The following arguments will be type-checked against
                        its `%`-specifiers, if the compiler supports that.
        @return  A new non-null mutable Fleece value, either an array or dict depending on the outer
                        delimiter of the format string.
        @throw  A \ref FleeceException with code `InvalidData` if there's a syntax error in the
        format string, either in JSON5 or a `%`-specifier. The exception message highlights the
        location where the error occurred. */
    RetainedConst<Value> Build(const char *format, ...) __printflike(1, 2);


    /** Variant of \ref Build that takes a pre-existing `va_list`. */
    RetainedConst<Value> VBuild(const char *format, va_list args);
    RetainedConst<Value> VBuild(slice format, va_list args);


    /** Like \ref Build, except the properties are stored in (appended to) an existing Array. */
    void Put(MutableArray*, const char *format, ...) __printflike(2, 3);

    /** Like \ref Build, except the properties are stored into an existing Dict.
        (Pre-existing properties not appearing in the format string are preserved.) */
    void Put(MutableDict*, const char *format, ...) __printflike(2, 3);

    /** Variant of \ref Put that takes a pre-existing `va_list`. */
    void VPut(Value*, const char *format, va_list args);

#ifdef __APPLE__
    /** Variant of Build that allows `%@` for [Core]Foundation values; the corresponding arg
        must be a `CFStringRef`, `CFNumberRef`, `CFArrayRef` or `CFDictionaryRef`.
        \note The format string is a `CFStringRef` not a `char*`, because `CF_FORMAT_FUNCTION`
              requires that. */
    RetainedConst<Value> BuildCF(CFStringRef format, ...) CF_FORMAT_FUNCTION(1, 2);
#endif
}
