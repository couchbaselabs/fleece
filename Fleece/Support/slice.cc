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

#include "fleece/slice.hh"
#include "encode.h"
#include "decode.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <cstdio>
#include "betterassert.hh"

namespace fleece {

    void slice::setStart(const void *s) noexcept {
        assert_precondition(s <= end());
        set(s, (uint8_t*)end() - (uint8_t*)s);
    }

    bool pure_slice::containsAddress(const void *addr) const noexcept {
        return addr >= buf && addr < end();
    }

    __hot int pure_slice::caseEquivalentCompare(pure_slice b) const noexcept {
        size_t minSize = std::min(size, b.size);
        for (size_t i = 0; i < minSize; i++) {
            int cmp = tolower((*this)[i]) - tolower(b[i]);
            if (cmp != 0)
                return cmp;
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
        if (_usuallyTrue(dst.size > 0)) {
            ::memcpy((void*)dst.buf, buf, dst.size);
            moveStart(dst.size);
        }
        return true;
    }

    __hot bool slice::writeFrom(slice src) noexcept {
        if (src.size > size)
            return false;
        if (_usuallyTrue(src.size > 0)) {
            ::memcpy((void*)buf, src.buf, src.size);
            moveStart(src.size);
        }
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

    __hot uint64_t slice::readDecimal() noexcept {
        uint64_t n = 0;
        while (size > 0 && isdigit(*(char*)buf)) {
            n = 10*n + (*(char*)buf - '0');
            moveStart(1);
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
        // Optimized for speed
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

    unsigned slice::sizeOfDecimal(uint64_t n) noexcept {
        if (n == 0)
            return 1;
        return 1 + (unsigned)::floor(::log10(n));
    }

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


    /** Raw memory allocation. Just like malloc but throws on failure. */
    void* pure_slice::newBytes(size_t sz) {
        void* result = ::malloc(sz);
        if (!result) throw std::bad_alloc();
        return result;
    }

    template <typename T>
    T* pure_slice::reallocBytes(T* bytes, size_t newSz) {
        T* newBytes = (T*)::realloc(bytes, newSz);
        if (!newBytes) throw std::bad_alloc();
        return newBytes;
    }

    slice pure_slice::copy() const {
        if (buf == nullptr)
            return nullslice;
        void* copied = newBytes(size);
        FLMemCpy(copied, buf, size);
        return slice(copied, size);
    }

    void slice::free() noexcept {
        ::free((void*)buf);
        set(nullptr, 0);
    }
    
    bool pure_slice::hasPrefix(pure_slice s) const noexcept {
        return s.size > 0 && size >= s.size && FLMemCmp(buf, s.buf, s.size) == 0;
    }

    bool pure_slice::hasSuffix(pure_slice s) const noexcept {
        return s.size > 0 && size >= s.size
            && FLMemCmp(offsetby(buf, size - s.size), s.buf, s.size) == 0;
    }

    const void* pure_slice::containsBytes(pure_slice s) const noexcept {
        char* src = (char *)buf;
        char* search = (char *)s.buf;
        char* found = std::search(src, src + size, search, search + s.size);
        return found == src + size ? nullptr : found;
    }

    bool pure_slice::containsAddressRange(pure_slice s) const noexcept {
        return s.buf >= buf && s.end() <= end();
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


    bool pure_slice::toCString(char *str, size_t bufSize) const noexcept {
        assert(bufSize > 0);
        size_t n = std::min(size, bufSize-1);
        FLMemCpy(str, buf, n);
        str[n] = 0;
        return n == size;
    }


    __hot uint32_t pure_slice::hash() const noexcept {
        // <https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function#FNV-1a_hash>
        uint32_t h = 2166136261;
        for (size_t i = 0; i < size; i++) {
            h = (h ^ (*this)[i]) * 16777619;
	}
            
	return h;
    }


    std::string pure_slice::base64String() const {
        std::string str;
        size_t strLen = ((size + 2) / 3) * 4;
        str.resize(strLen);
        char *dst = &str[0];
        base64::encoder enc;
        enc.set_chars_per_line(0);
        size_t written = enc.encode(buf, size, dst);
        written += enc.encode_end(dst + written);
        assert(written == strLen);
        (void)written;  // avoid compiler warning in release build when 'assert' is a no-op
        return str;
    }


    slice pure_slice::readBase64Into(pure_slice output) const noexcept {
        size_t expectedLen = (size + 3) / 4 * 3;
        if (expectedLen > output.size)
            return nullslice;
        base64::decoder dec;
        size_t len = dec.decode(buf, size, (void*)output.buf);
        assert(len <= output.size);
        return slice(output.buf, len);
    }


    alloc_slice pure_slice::decodeBase64() const {
        size_t expectedLen = (size + 3) / 4 * 3;
        alloc_slice result(expectedLen);
        slice decoded = readBase64Into(result);
        if (decoded.size == 0)
            return {};
        assert(decoded.size <= expectedLen);
        result.resize(decoded.size);
        return result;
    }


#pragma mark - ALLOC_SLICE


    static inline pure_slice asSlice(FLSliceResult s) {
        return {s.buf, s.size};
    }


    __hot
    alloc_slice::alloc_slice(size_t sz)
    :pure_slice(asSlice(FLSliceResult_New(sz)))
    {
        if (_usuallyFalse(!buf))
            throw std::bad_alloc();
    }


    __hot
    alloc_slice::alloc_slice(pure_slice s)
    :pure_slice(asSlice(FLSlice_Copy({s.buf, s.size})))
    {
        if (_usuallyFalse(!buf) && s.buf)
            throw std::bad_alloc();
    }


    __hot
    alloc_slice alloc_slice::nullPaddedString(pure_slice str) {
        // Leave a trailing null byte after the end, so it can be used as a C string
        alloc_slice a(str.size + 1);
        FLMemCpy((char*)a.buf, str.buf, str.size);
        ((char*)a.buf)[str.size] = '\0';
        a.shorten(str.size);            // the null byte is not part of the slice
        return a;
    }


    __hot
    alloc_slice& alloc_slice::operator=(const alloc_slice& s) noexcept {
        if (_usuallyTrue(s.buf != buf)) {
            release();
            assignFrom(s);
            retain();
        }
        return *this;
    }


    __hot
    alloc_slice& alloc_slice::operator=(alloc_slice&& s) noexcept {
        if (_usuallyTrue(s.buf != buf)) {
            release();
            assignFrom(s);
            s.set(nullptr, 0);
        }
        return *this;
    }


    __hot
    alloc_slice& alloc_slice::operator=(FLHeapSlice s) noexcept {
        if (_usuallyTrue(s.buf != buf)) {
            release();
            assignFrom({s.buf, s.size});
            retain();
        }
        return *this;
    }


    void alloc_slice::reset() noexcept {
        release();
        assignFrom(nullslice);
    }

    void alloc_slice::reset(size_t sz) {
        *this = alloc_slice(sz);
    }

    
    __hot
    alloc_slice& alloc_slice::operator=(pure_slice s) {
        return *this = alloc_slice(s);
    }


    void alloc_slice::resize(size_t newSize) {
        if (newSize == size) {
            return;
        } else if (buf == nullptr) {
            reset(newSize);
        } else {
            alloc_slice newSlice(newSize);
            FLMemCpy((void*)newSlice.buf, buf, std::min(size, newSize));
            *this = std::move(newSlice);
        }
    }


    void alloc_slice::append(pure_slice suffix) {
        if (_usuallyFalse(suffix.size == 0))
            return;
        if (buf)
            assert_precondition(!containsAddress(suffix.buf) && !containsAddress(suffix.end()));
        size_t oldSize = size;
        resize(oldSize + suffix.size);
        ::memcpy((void*)offset(oldSize), suffix.buf, suffix.size);
    }

}
