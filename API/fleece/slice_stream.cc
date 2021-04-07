//
// slicestream.cc
//
// Copyright © 2021 Couchbase. All rights reserved.
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

#include "slice_stream.hh"
#include <assert.h>

namespace fleece {


    void slice_stream::advanceTo(void *pos) {
        assert(pos > _next);
        advance(intptr_t(pos) - intptr_t(_next));
    }

    void slice_stream::advance(size_t n) {
        assert(n <= _capacity);
        _next += n;
        _capacity -= n;
    }


    void slice_stream::retreat(size_t n) {
        assert(n <= bytesWritten());
        _next -= n;
        _capacity += n;
    }


    static inline char _hexDigit(int n) {
        static constexpr const char kDigits[] = "0123456789abcdef";
        return kDigits[n];
    }


    bool slice_stream::writeByte(uint8_t n) noexcept {
        if (_capacity == 0)
            return false;
        *_next++ = n;
        --_capacity;
        return true;
    }


    bool slice_stream::write(const void *src, size_t srcSize) noexcept {
        if (_usuallyFalse(srcSize > _capacity))
            return false;
        ::memcpy(_next, src, srcSize);
        advance(srcSize);
        return true;
    }


    bool slice_stream::writeDecimal(uint64_t n) noexcept {
        // Optimized for speed; this is on a hot path in LiteCore
        size_t len;
        if (n < 10) {
            if (_capacity < 1)
                return false;
            *((char*)_next) = '0' + (char)n;
            len = 1;
        } else {
            char temp[20]; // max length is 20 decimal digits
            char *dst = &temp[20];
            len = 0;
            do {
                *(--dst) = '0' + (n % 10);
                n /= 10;
                len++;
            } while (n > 0);
            if (_capacity < len)
                return false;
            ::memcpy(_next, dst, len);
        }
        advance(len);
        return true;
    }


    bool slice_stream::writeHex(pure_slice src) noexcept {
        if (_usuallyFalse(_capacity < 2 * src.size))
            return false;
        auto dst = (char*)_next;
        for (size_t i = 0; i < src.size; ++i) {
            *dst++ = _hexDigit(src[i] >> 4);
            *dst++ = _hexDigit(src[i] & 0x0F);
        }
        advanceTo(dst);
        return true;
    }


    bool slice_stream::writeHex(uint64_t n) noexcept {
        char temp[16]; // max length is 16 hex digits
        char *dst = &temp[16];
        size_t len = 0;
        do {
            *(--dst) = _hexDigit(n & 0x0F);
            n >>= 4;
            len++;
        } while (n > 0);
        return write(dst, len);
    }

}
