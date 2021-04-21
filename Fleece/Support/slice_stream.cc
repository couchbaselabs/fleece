//
// slice_stream.cc
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
#include "varint.hh"
#include "betterassert.hh"

namespace fleece {


    bool slice_ostream::advanceTo(void *pos) noexcept {
        if (_usuallyFalse(pos < _next || pos > _end) ) {
            _overflowed = true;
            return false;
        }
        assert_precondition(pos >= _next && pos <= _end);
        _next = (uint8_t*)pos;
        return true;
    }


    __hot
    bool slice_ostream::advance(size_t n) noexcept {
        if (_usuallyFalse(n > capacity())) {
            _overflowed = true;
            return false;
        }
        _next += n;
        return true;
    }


    void slice_ostream::retreat(size_t n) {
        assert_precondition(n <= bytesWritten());
        _next -= n;
    }


    bool slice_ostream::writeByte(uint8_t n) noexcept {
        if (_next >= _end) {
            _overflowed = true;
            return false;
        }
        *_next++ = n;
        return true;
    }


    bool slice_ostream::write(const void *src, size_t srcSize) noexcept {
        if (_usuallyFalse(srcSize > capacity())) {
            _overflowed = true;
            return false;
        }
        if (_usuallyFalse(srcSize == 0))
            return true;
        ::memcpy(_next, src, srcSize);
        _next += srcSize;
        return true;
    }


#pragma mark  NUMERIC OUTPUT:


    static inline uint8_t _hexDigit(int n) {
        static constexpr const uint8_t kDigits[] = "0123456789abcdef";
        return kDigits[n];
    }


    __hot
    bool slice_ostream::writeDecimal(uint64_t n) noexcept {
        // Optimized for speed; this is on a hot path in LiteCore
        if (n < 10) {
            return writeByte('0' + (char)n);
        } else {
            char temp[20]; // max length is 20 decimal digits
            char *dst = &temp[20];
            size_t len = 0;
            do {
                *(--dst) = '0' + (n % 10);
                n /= 10;
                len++;
            } while (n > 0);
            return write(dst, len);
        }
    }


    bool slice_ostream::writeHex(uint64_t n) noexcept {
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


    bool slice_ostream::writeHex(pure_slice src) noexcept {
        if (_usuallyFalse(capacity() < 2 * src.size)) {
            _overflowed = true;
            return false;
        }
        auto dst = _next;
        for (size_t i = 0; i < src.size; ++i) {
            *dst++ = _hexDigit(src[i] >> 4);
            *dst++ = _hexDigit(src[i] & 0x0F);
        }
        _next = dst;
        return true;
    }


    bool slice_ostream::writeUVarInt(uint64_t n) noexcept {
        if (capacity() < kMaxVarintLen64 && capacity() < SizeOfVarInt(n)) {
            _overflowed = true;
            return false;
        }
        _next += PutUVarInt(_next, n);
        return true;
    }


#pragma mark - INPUT STREAM:


    __hot slice slice_istream::readAll(size_t nBytes) noexcept  {
        if (nBytes > size)
            return nullslice;
        slice result(buf, nBytes);
        skip(nBytes);
        return result;
    }


    __hot slice slice_istream::readAtMost(size_t nBytes) noexcept {
        nBytes = std::min(nBytes, size);
        slice result(buf, nBytes);
        skip(nBytes);
        return result;
    }


    __hot bool slice_istream::readAll(void *dstBuf, size_t dstSize) noexcept {
        if (dstSize > size)
            return false;
        ::memcpy(dstBuf, buf, dstSize);
        skip(dstSize);
        return true;
    }


    size_t slice_istream::readAtMost(void *dstBuf, size_t dstSize) noexcept {
        dstSize = std::min(dstSize, size);
        readAll(dstBuf, dstSize);
        return dstSize;
    }


    __hot slice slice_istream::readToDelimiter(slice delim) noexcept  {
        slice found = find(delim);
        if (!found)
            return nullslice;
        slice result(buf, found.buf);
        setStart(found.end());
        return result;
    }


    __hot slice slice_istream::readToDelimiterOrEnd(slice delim) noexcept {
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


    __hot slice slice_istream::readBytesInSet(slice set) noexcept {
        const void *next = findByteNotIn(set);
        if (!next)
            next = end();
        slice result(buf, next);
        setStart(next);
        return result;
    }


    __hot uint8_t slice_istream::readByte() noexcept {
        if (_usuallyFalse(size == 0))
            return 0;
        uint8_t result = (*this)[0];
        skip(1);
        return result;
    }


    void slice_istream::skipTo(const void *pos) {
        assert_precondition(pos >= buf && pos <= end());
        setStart(pos);
    }


    void slice_istream::rewindTo(const void *pos) {
        // There's no way to validate this since we don't keep track of the original start of the
        // stream (the original `buf` value.) We could add that as a data member; is it worth it?
        assert_precondition(pos <= buf);
        setStart(pos);
    }


#pragma mark  NUMERIC INPUT:


    FLPURE static int _digittoint(char ch) noexcept {
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


    __hot uint64_t slice_istream::readDecimal() noexcept {
        uint64_t n = 0;
        while (size > 0 && isdigit(*(char*)buf)) {
            n = 10*n + (*(char*)buf - '0');
            skip(1);
            if (n > UINT64_MAX/10)
                break;          // Next digit would overflow uint64_t
        }
        return n;
    }


    __hot int64_t slice_istream::readSignedDecimal() noexcept {
        bool negative = (size > 0 && (*this)[0] == '-');
        if (negative)
            skip(1);
        uint64_t n = readDecimal();
        if (n > INT64_MAX)
            return 0;
        return negative ? -(int64_t)n : (int64_t)n;
    }


    uint64_t slice_istream::readHex() noexcept {
        uint64_t n = 0;
        while (size > 0) {
            int digit = _digittoint(*(char*)buf);
            if (digit < 0)
                break;
            n = (n <<4 ) + digit;
            skip(1);
            if (n > UINT64_MAX/16)
                break;          // Next digit would overflow uint64_t
        }
        return n;
    }


    __hot
    std::optional<uint64_t> slice_istream::readUVarInt() noexcept {
        if (size == 0)
            return false;
        uint64_t n;
        if (size_t bytesRead = GetUVarInt(*this, &n); bytesRead > 0) {
            skip(bytesRead);
            return n;
        } else {
            return std::nullopt;
        }
    }


    __hot
    std::optional<uint32_t> slice_istream::readUVarInt32() noexcept {
        if (size == 0)
            return false;
        uint32_t n;
        if (size_t bytesRead = GetUVarInt32(*this, &n); bytesRead > 0) {
            skip(bytesRead);
            return n;
        } else {
            return std::nullopt;
        }
    }

}
