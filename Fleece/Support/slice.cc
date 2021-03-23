//
// slice.cc
//
// Copyright (c) 2014 Couchbase, Inc All rights reserved.
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

#define __STDC_WANT_LIB_EXT1__ 1

#include "fleece/slice.hh"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "betterassert.hh"

#ifdef _MSC_VER
    #include <Windows.h>    // for SecureZeroMemory()
#endif


namespace fleece {


#pragma mark - MISCELLANY:


    void slice::wipe() noexcept {
        if (size > 0) {
#if defined(_MSC_VER)
            SecureZeroMemory((void*)buf, size);
#elif defined(__STDC_LIB_EXT1__) || defined(__APPLE__)
            // memset_s is an optional feature of C11, and available in Apple's C library.
            memset_s((void*)buf, size, 0, size);
#else
            // Generic implementation (`volatile` ensures the writes will not be optimized away.)
            volatile unsigned char* p = (unsigned char *)buf;
            for (auto s = size; s > 0; --s)
                *p++ = 0;
#endif
        }
    }


    void slice::shorten(size_t s) {
        assert_precondition(s <= size);
        setSize(s);
    }



    void slice::setStart(const void *s) noexcept {
        assert_precondition(s <= end());
        set(s, (uint8_t*)end() - (uint8_t*)s);
    }


    bool pure_slice::toCString(char *str, size_t bufSize) const noexcept {
        size_t n = std::min(size, bufSize-1);
        memcpy(str, buf, n);
        str[n] = 0;
        return n == size;
    }


#pragma mark - COMPARISON:


    __hot int pure_slice::compare(pure_slice b) const noexcept {
        // Optimized for speed
        if (this->size == b.size)
            return memcmp(this->buf, b.buf, this->size);
        else if (this->size < b.size) {
            int result = memcmp(this->buf, b.buf, this->size);
            return result ? result : -1;
        } else {
            int result = memcmp(this->buf, b.buf, b.size);
            return result ? result : 1;
        }
    }

    __hot int pure_slice::caseEquivalentCompare(pure_slice b) const noexcept {
        size_t minSize = std::min(size, b.size);
        for (size_t i = 0; i < minSize; i++) {
            if ((*this)[i] != b[i]) {
                int cmp = tolower((*this)[i]) - tolower(b[i]);
                if (cmp != 0)
                    return cmp;
            }
        }
        return (int)size - (int)b.size;
    }

    __hot bool pure_slice::caseEquivalent(pure_slice b) const noexcept {
        if (size != b.size)
            return false;
        for (size_t i = 0; i < size; i++)
            if (tolower((*this)[i]) != tolower(b[i]))
                return false;
        return true;
    }


#pragma mark - FIND:


    __hot slice pure_slice::find(pure_slice target) const noexcept {
        char* src = (char *)buf;
        char* search = (char *)target.buf;
        char* found = std::search(src, src + size, search, search + target.size);
        if(found == src + size) {
            return nullslice;
        }

        return {found, target.size};
    }


    __hot const uint8_t* pure_slice::findByteOrEnd(uint8_t byte) const noexcept {
        auto result = findByte(byte);
        return result ? result : (const uint8_t*)end();
    }


    __hot const uint8_t* pure_slice::findAnyByteOf(pure_slice targetBytes) const noexcept {
        const void* result = nullptr;
        for (size_t i = 0; i < targetBytes.size; ++i) {
            auto r = findByte(targetBytes[i]);
            if (r && (!result || r < result))
                result = r;
        }
        return (const uint8_t*)result;
    }


    __hot const uint8_t* pure_slice::findByteNotIn(pure_slice targetBytes) const noexcept {
        for (auto c = (const uint8_t*)buf; c != end(); ++c) {
            if (!targetBytes.findByte(*c))
                return c;
        }
        return nullptr;
    }


    bool pure_slice::hasPrefix(pure_slice s) const noexcept {
        return s.size > 0 && size >= s.size && ::memcmp(buf, s.buf, s.size) == 0;
    }


    bool pure_slice::hasSuffix(pure_slice s) const noexcept {
        return s.size > 0 && size >= s.size
            && ::memcmp(offsetby(buf, size - s.size), s.buf, s.size) == 0;
    }


    const void* pure_slice::containsBytes(pure_slice s) const noexcept {
        char* src = (char *)buf;
        char* search = (char *)s.buf;
        char* found = std::search(src, src + size, search, search + s.size);
        return found == src + size ? nullptr : found;
    }


    bool pure_slice::containsAddress(const void *addr) const noexcept {
        return addr >= buf && addr < end();
    }


    bool pure_slice::containsAddressRange(pure_slice s) const noexcept {
        return s.buf >= buf && s.end() <= end();
    }


#pragma mark - READ/WRITE:


    __hot slice slice::read(size_t nBytes) noexcept  {
        if (nBytes > size)
            return nullslice;
        slice result(buf, nBytes);
        moveStart(nBytes);
        return result;
    }

    __hot slice slice::readAtMost(size_t nBytes) noexcept {
        nBytes = std::min(nBytes, size);
        slice result(buf, nBytes);
        moveStart(nBytes);
        return result;
    }

    __hot slice slice::readToDelimiter(slice delim) noexcept  {
        slice found = find(delim);
        if (!found)
            return nullslice;
        slice result(buf, found.buf);
        setStart(found.end());
        return result;
    }

    __hot slice slice::readToDelimiterOrEnd(slice delim) noexcept {
        slice found = find(delim);
        if (found) {
            slice result(buf, found.buf);
            setStart(found.end());
            return result;
        } else {
            slice result = *this;
            setStart(end());
            return result;
        }
    }

    __hot slice slice::readBytesInSet(slice set) noexcept {
        const void *next = findByteNotIn(set);
        if (!next)
            next = end();
        slice result(buf, next);
        setStart(next);
        return result;
    }


    __hot bool slice::readInto(slice dst) noexcept {
        if (dst.size > size)
            return false;
        ::memcpy((void*)dst.buf, buf, dst.size);
        moveStart(dst.size);
        return true;
    }

    __hot bool slice::writeFrom(slice src) noexcept {
        if (src.size > size)
            return false;
        ::memcpy((void*)buf, src.buf, src.size);
        moveStart(src.size);
        return true;
    }

    __hot uint8_t slice::peekByte() const noexcept {
        return (size > 0) ? (*this)[0] : 0;
    }

    __hot uint8_t slice::readByte() noexcept {
        if (size == 0)
            return 0;
        uint8_t result = (*this)[0];
        moveStart(1);
        return result;
    }

    __hot bool slice::writeByte(uint8_t n) noexcept {
        if (size == 0)
            return false;
        *((char*)buf) = n;
        moveStart(1);
        return true;
    }


#pragma mark - DECIMAL CONVERSION:


    static int _digittoint(char ch) {
        int d = ch - '0';
        if ((unsigned) d < 10)
            return d;
        d = ch - 'a';
        if ((unsigned) d < 6)
            return d + 10;
        d = ch - 'A';
        if ((unsigned) d < 6)
            return d + 10;
        return -1;
    }

    unsigned slice::sizeOfDecimal(uint64_t n) noexcept {
        if (n == 0)
            return 1;
        return 1 + (unsigned)::floor(::log10(n));
    }

    __hot uint64_t slice::readDecimal() noexcept {
        uint64_t n = 0;
        while (size > 0 && isdigit(*(char*)buf)) {
            n = 10*n + (*(char*)buf - '0');
            moveStart(1);
            if (n > UINT64_MAX/10)
                break;          // Next digit would overflow uint64_t
        }
        return n;
    }

    __hot int64_t slice::readSignedDecimal() noexcept {
        bool negative = (size > 0 && (*this)[0] == '-');
        if (negative)
            moveStart(1);
        uint64_t n = readDecimal();
        if (n > INT64_MAX)
            return 0;
        return negative ? -(int64_t)n : (int64_t)n;
    }

    __hot bool slice::writeDecimal(uint64_t n) noexcept {
        // Optimized for speed; this is on a hot path in LiteCore
        size_t len;
        if (n < 10) {
            if (size < 1)
                return false;
            *((char*)buf) = '0' + (char)n;
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
            if (size < len)
                return false;
            ::memcpy((void*)buf, dst, len);
        }
        moveStart(len);
        return true;
    }


#pragma mark - HEX CONVERSION:


    static inline char _hexDigit(int n) {
        static constexpr const char kDigits[] = "0123456789abcdef";
        return kDigits[n];
    }


    uint64_t slice::readHex() noexcept {
        uint64_t n = 0;
        while (size > 0) {
            int digit = _digittoint(*(char*)buf);
            if (digit < 0)
                break;
            n = (n <<4 ) + digit;
            moveStart(1);
            if (n > UINT64_MAX/16)
                break;          // Next digit would overflow uint64_t
        }
        return n;
    }


    bool slice::writeHex(slice src) noexcept {
        if (_usuallyFalse(size < 2 * src.size))
            return false;
        auto dst = (char*)buf;
        for (size_t i = 0; i < src.size; ++i) {
            *dst++ = _hexDigit(src[i] >> 4);
            *dst++ = _hexDigit(src[i] & 0x0F);
        }
        setStart(dst);
        return true;
    }


    bool slice::writeHex(uint64_t n) noexcept {
        char temp[16]; // max length is 16 hex digits
        char *dst = &temp[16];
        size_t len = 0;
        do {
            *(--dst) = _hexDigit(n & 0x0F);
            n >>= 4;
            len++;
        } while (n > 0);
        if (size < len)
            return false;
        ::memcpy((void*)buf, dst, len);
        moveStart(len);
        return true;
    }


    std::string pure_slice::hexString() const {
        static const char kDigits[17] = "0123456789abcdef";
        std::string result;
        result.reserve(2 * size);
        for (size_t i = 0; i < size; i++) {
            uint8_t byte = (*this)[(unsigned)i];
            result += kDigits[byte >> 4];
            result += kDigits[byte & 0xF];
        }
        return result;
    }


#pragma mark - MEMORY ALLOCATION


    /** Raw memory allocation. Just like malloc but throws/terminates on failure. */
    RETURNS_NONNULL
    void* pure_slice::newBytes(size_t sz) {
        void* result = ::malloc(sz);
        if (_usuallyFalse(!result)) failBadAlloc();
        return result;
    }


    slice pure_slice::copy() const {
        if (buf == nullptr)
            return nullslice;
        void* copied = newBytes(size);
        ::memcpy(copied, buf, size);
        return slice(copied, size);
    }


    void slice::free() noexcept {
        ::free((void*)buf);
        set(nullptr, 0);
    }


    [[noreturn]] void pure_slice::failBadAlloc() {
#ifdef __cpp_exceptions
        throw std::bad_alloc();
#else
        fprintf(stderr, "*** FATAL ERROR: heap allocation failed (fleece/slice.cc) ***\n");
        std::terminate();
#endif
    }


#pragma mark - ALLOC_SLICE


    __hot alloc_slice::alloc_slice(size_t sz)
    :alloc_slice(FLSliceResult_New(sz))
    {
        if (_usuallyFalse(!buf))
            failBadAlloc();
    }


    __hot alloc_slice::alloc_slice(pure_slice s)
    :alloc_slice(FLSlice_Copy(s))
    {
        if (_usuallyFalse(!buf) && s.buf)
            failBadAlloc();
    }


    alloc_slice::alloc_slice(FLHeapSlice s) noexcept
    :pure_slice(s.buf, s.size)
    {
        retain();
    }


    __hot alloc_slice::alloc_slice(const FLSliceResult &sr) noexcept
    :pure_slice(sr.buf, sr.size)
    {
        retain();
    }


    alloc_slice alloc_slice::nullPaddedString(pure_slice str) {
        // Leave a trailing null byte after the end, so it can be used as a C string
        alloc_slice a(str.size + 1);
        memcpy((char*)a.buf, str.buf, str.size);
        ((char*)a.buf)[str.size] = '\0';
        a.shorten(str.size);            // the null byte is not part of the slice
        return a;
    }


    __hot alloc_slice& alloc_slice::operator=(const alloc_slice& s) noexcept {
        if (_usuallyTrue(s.buf != buf)) {
            release();
            assignFrom(s);
            retain();
        }
        return *this;
    }


    __hot alloc_slice& alloc_slice::operator=(FLHeapSlice s) noexcept {
        if (_usuallyTrue(s.buf != buf)) {
            release();
            assignFrom(slice(s));
            retain();
        }
        return *this;
    }


    __hot alloc_slice& alloc_slice::operator=(pure_slice s) {
        return *this = alloc_slice(s);
    }


    void alloc_slice::reset() noexcept {
        release();
        assignFrom(nullslice);
    }


    void alloc_slice::resize(size_t newSize) {
        if (newSize == size) {
            return;
        } else if (buf == nullptr) {
            reset(newSize);
        } else {
            // We don't realloc the current buffer; that would affect other alloc_slice objects
            // sharing the buffer, and possibly confuse them. Instead, alloc a new buffer & copy.
            alloc_slice newSlice(newSize);
            memcpy((void*)newSlice.buf, buf, std::min(size, newSize));
            *this = std::move(newSlice);
        }
    }


    void alloc_slice::append(pure_slice suffix) {
        if (buf)
            assert_precondition(!containsAddress(suffix.buf) && !containsAddress(suffix.end()));
        size_t oldSize = size;
        resize(oldSize + suffix.size);
        memcpy((void*)offset(oldSize), suffix.buf, suffix.size);
    }

    void alloc_slice::shorten(size_t s) {
        assert_precondition(s <= size);
        pure_slice::setSize(s);
    }


}
