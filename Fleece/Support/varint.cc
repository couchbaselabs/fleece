//
// varint.cc
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

#include "varint.hh"
#include "fleece/slice.hh"
#include "Endian.hh"
#include <algorithm>
#include "betterassert.hh"


namespace fleece {

__hot
size_t SizeOfVarInt(uint64_t n) {
    size_t size = 1;
    while (n >= 0x80) {
        size++;
        n >>= 7;
    }
    return size;
}


__hot
size_t PutUVarInt(void *buf, uint64_t n) {
    uint8_t* dst = (uint8_t*)buf;
    while (n >= 0x80) {
        *dst++ = (n & 0xFF) | 0x80;
        n >>= 7;
    }
    *dst++ = (uint8_t)n;
    return dst - (uint8_t*)buf;
}


__hot
size_t _GetUVarInt(slice buf, uint64_t *n) {
    // NOTE: The public inline function GetUVarInt already decodes 1-byte varints,
    // so if we get here we can assume the varint is at least 2 bytes.
    auto pos = (const uint8_t*)buf.buf;
    auto end = pos + std::min(buf.size, (size_t)kMaxVarintLen64);
    uint64_t result = *pos++ & 0x7F;
    int shift = 7;
    while (pos < end) {
        uint8_t byte = *pos++;
        if (_usuallyTrue(byte >= 0x80)) {
            result |= (uint64_t)(byte & 0x7F) << shift;
            shift += 7;
        } else {
            result |= (uint64_t)byte << shift;
            *n = result;
            size_t nBytes = pos - (const uint8_t*)buf.buf;
            if (_usuallyFalse(nBytes == kMaxVarintLen64 && byte > 1))
                nBytes = 0; // Numeric overflow
            return nBytes;
        }
    }
    return 0; // buffer too short
}

__hot
size_t _GetUVarInt32(slice buf, uint32_t *n) {
    uint64_t n64;
    size_t size = _GetUVarInt(buf, &n64);
    if (size == 0 || n64 > UINT32_MAX) // Numeric overflow
        return 0;
    *n = (uint32_t)n64;
    return size;
}



#pragma mark - VARIABLE LENGTH INTS:

    // These just use a variable length little-endian encoding.

    __hot
    int64_t GetIntOfLength(const void *src, unsigned length) {
        assert_precondition(length >= 1 && length <= 8);
        int64_t result = 0;
        if (((int8_t*)src)[length-1] < 0)
            result = -1;                        // sign-extend the result
        memcpy(&result, src, length);
        return endian::decLittle64(result);
    }

    __hot
    size_t PutIntOfLength(void *buf, int64_t n, bool isUnsigned) {
        int64_t littlen = endian::encLittle64(n);
        memcpy(buf, &littlen, 8);
        size_t size;
        if (isUnsigned) {
            // Skip trailing 00 bytes
            for (size = 8; size > 1; --size) {
                uint8_t byte = ((uint8_t*)buf)[size-1];
                if (byte != 0) {
                    break;
                }
            }
            return size;

        } else {
            // Skip trailing bytes that are FF (if negative) or else 00
            uint8_t trim = (n >= 0) ? 0 : 0xFF;
            for (size = 8; size > 1; --size) {
                uint8_t byte = ((uint8_t*)buf)[size-1];
                if (byte != trim) {
                    // May have to keep an extra byte so the sign is correctly discoverable
                    if ((byte ^ trim) & 0x80)
                        ++size;
                    break;
                }
            }
            return size;
        }
    }


#pragma mark - COLLATABLE INTS:


    // These consist of the number's shortest big-endian representation (no leading 00 bytes),
    // prefixed by the byte-count of that representation.
    // Examples:   0x0 --> 00
    //            0xbc --> 01 bc
    //          0x1234 --> 02 12 34
    // Encoded integers can be compared by memcmp() or the equivalent.

    size_t SizeOfCollatableUInt(uint64_t n) {
        size_t size = 1;
        while (n != 0) {
            ++size;
            n >>= 8;
        }
        return size;
    }

    size_t PutCollatableUInt(void *buf, uint64_t n) {
        uint8_t *result = (uint8_t*)buf;
        size_t len = 0;
        for (auto tmp = n; tmp != 0; tmp >>= 8)
            ++len;
        result[0] = uint8_t(len);
        for (size_t i = len; i > 0; --i) {
            result[i] = n & 0xFF;
            n >>= 8;
        }
        return len + 1;
    }

    size_t GetCollatableUInt(slice buf, uint64_t *n) {
        if (buf.size == 0)
            return 0;
        uint8_t len = buf[0];
        if (len > 8 || len >= buf.size)
            return 0;
        uint64_t result = 0;
        for (uint8_t i = 1; i <= len; i++)
            result = (result << 8) | buf[i];
        *n = result;
        return len;
    }



}
