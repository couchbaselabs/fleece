//
// NumConversion.cc
//
// Copyright © 2019 Couchbase. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "NumConversion.hh"
#include "SwiftDtoa.h"
#include <ctype.h>
#include <locale.h>
#include <stdlib.h>
#if !defined(_MSC_VER) && !defined(__GLIBC__)
#include <xlocale.h>
#endif

namespace fleece {

    // subroutine that parses only digits
    static bool _parseUInt(const char *str NONNULL, uint64_t &result, bool allowTrailing) {
        uint64_t n = 0;
        if (!isdigit(*str))
            return false;
        while (isdigit(*str)) {
            int digit = (*str++ - '0');
            if (_usuallyFalse(n > UINT64_MAX / 10))
                return false;
            n *= 10;
            if (_usuallyFalse(n > UINT64_MAX - digit))
                return false;
            n += digit;
        }
        if (!allowTrailing) {
            while (isspace(*str))
                ++str;
            if (_usuallyFalse(*str != '\0'))
                return false;
        }
        result = n;
        return true;
    }

    // Unsigned version:
    bool ParseInteger(const char *str NONNULL, uint64_t &result, bool allowTrailing) {
        while (isspace(*str))
            ++str;
        if (*str == '+')
            ++str;
        return _parseUInt(str, result, allowTrailing);
    }


    // Signed version:
    bool ParseInteger(const char *str NONNULL, int64_t &result, bool allowTrailing) {
        while (isspace(*str))
            ++str;
        bool negative = (*str == '-');
        if (negative || *str == '+')
            ++str;
        uint64_t uresult;
        if (!_parseUInt(str, uresult, allowTrailing))
            return false;

        if (negative) {
            if (_usuallyTrue(uresult <= uint64_t(INT64_MAX))) {
                result = -int64_t(uresult);
            } else if (uresult == uint64_t(INT64_MAX) + 1) {
                // Special-case the conversion of 9223372036854775808 into -9223372036854775808,
                // because the normal cast (above) would create a temporary integer overflow that
                // triggers a runtime Undefined Behavior Sanitizer warning.
                result = INT64_MIN;
            } else {
                return false;
            }
        } else {
            if (_usuallyFalse(uresult > uint64_t(INT64_MAX)))
                return false;
            result = int64_t(uresult);
        }
        return true;
    }


    bool ParseDouble(const char *str, double &result, bool allowTrailing) noexcept {
        char *end;
        // strtod is locale-aware, so in some locales it will not interpret '.' as a decimal point.
        // To work around that, use the C locale explicitly.
        #ifdef LC_C_LOCALE          // Apple & BSD
            result = strtod_l(str, &end, LC_C_LOCALE);
        #elif defined(_MSC_VER)     // Windows
            static _locale_t kCLocale = _create_locale(LC_ALL, "C");
            result = _strtod_l(str, &end, kCLocale);
        #elif defined(__ANDROID__) && __ANDROID_API__ < 26
            // Note: Android only supports the following locales, all of which use
            // period, so no problem:  C, POSIX, en_US.  Android API 26 introduces
            // strtod_l, which maybe will be eventually implemented when and if more
            // locales come in
            result = strtod(str, &end);
        #else                       // Linux
            static locale_t kCLocale = newlocale(LC_ALL_MASK, "C", NULL);
            result = strtod_l(str, &end, kCLocale);
        #endif
        return (allowTrailing || *end == '\0');
    }


    double ParseDouble(const char *str) noexcept {
        double n;
        (void)ParseDouble(str, n, true);
        return n;
    }


    size_t WriteFloat(float n, char *dst, size_t capacity) {
        return swift_format_float(n, dst, capacity);
    }


    size_t WriteFloat(double n, char *dst, size_t capacity) {
        return swift_format_double(n, dst, capacity);
    }
}
