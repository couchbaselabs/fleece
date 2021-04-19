//
// varint.hh
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

#pragma once
#include "fleece/slice.hh"

namespace fleece {

    
#pragma mark - UNSIGNED VARINTS:

// Based on varint implementation from the Go language (src/pkg/encoding/binary/varint.go)
// This file implements "varint" encoding of unsigned 64-bit integers.
// The encoding is:
// - unsigned integers are serialized 7 bits at a time, starting with the
//   least significant bits
// - the most significant bit (msb) in each output byte indicates if there
//   is a continuation byte (msb = 1)
// (This implementation does not support signed integers, which have a more complex encoding.)


/** MaxVarintLenN is the maximum length of a varint-encoded N-bit integer. */
enum {
    kMaxVarintLen16 = 3,
    kMaxVarintLen32 = 5,
    kMaxVarintLen64 = 10,
};

/** Returns the number of bytes needed to encode a specific integer. */
size_t SizeOfVarInt(uint64_t n);

/** Encodes n as a varint, writing it to buf. Returns the number of bytes written. */
size_t PutUVarInt(void *buf NONNULL, uint64_t n);

size_t _GetUVarInt(slice buf, uint64_t *n NONNULL);   // do not call directly
size_t _GetUVarInt32(slice buf, uint32_t *n NONNULL); // do not call directly

    
/** Decodes a varint from the bytes in buf, storing it into *n.
    Returns the number of bytes read, or 0 if the data is invalid (buffer too short or number
    too long.) */
__hot
static inline size_t GetUVarInt(slice buf, uint64_t *n NONNULL) {
    if (_usuallyFalse(buf.size == 0))
        return 0;
    uint8_t byte = buf[0];
    if (_usuallyTrue(byte < 0x80)) {
        *n = byte;
        return 1;
    }
    return _GetUVarInt(buf, n);
}


/** Decodes a varint from the bytes in buf, storing it into *n.
    Returns the number of bytes read, or 0 if the data is invalid (buffer too short or number
    too long.) */
__hot
static inline size_t GetUVarInt32(slice buf, uint32_t *n NONNULL) {
    if (_usuallyFalse(buf.size == 0))
        return 0;
    uint8_t byte = buf[0];
    if (_usuallyTrue(byte < 0x80)) {
        *n = byte;
        return 1;
    }
    return _GetUVarInt32(buf, n);
}


/** Skips a pointer past a varint without decoding it. */
__hot
static inline const void* SkipVarInt(const void *buf NONNULL) {
    auto p = (const uint8_t*)buf;
    uint8_t byte;
    do {
        byte = *p++;
    } while (byte & 0x80);
    return p;
}


#pragma mark - VARIABLE LENGTH INTS:

// This is a compact encoding that requires the length to be stored externally.

/** Encodes an integer `n` to `buf` and returns the number of bytes used (1-8).
    if `isUnsigned` is true, the number is treated as unsigned (uint64_t.) */
size_t PutIntOfLength(void *buf NONNULL, int64_t n, bool isUnsigned =false);

/** Encodes an unsigned integer `n` to `buf` and returns the number of bytes used (1-8). */
inline size_t PutUIntOfLength(void *buf NONNULL, uint64_t n) {return PutIntOfLength(buf, n, true);}

/** Returns a signed integer decoded from `length` bytes starting at `buf`. */
int64_t GetIntOfLength(const void *buf NONNULL, unsigned length);


#pragma mark - COLLATABLE INTS:

// Collatable ints use an encoding that can be compared using memcmp().

static constexpr size_t kMaxCollatableUIntLen64 = 9;

/** Returns the number of bytes needed to encode a specific integer. */
size_t SizeOfCollatableUInt(uint64_t n);

/** Encodes n as a collatable int, writing it to buf. Returns the number of bytes written. */
size_t PutCollatableUInt(void *buf, uint64_t n);

/** Decodes a collatable int from the bytes in buf, storing it into *n.
    Returns the number of bytes read, or 0 if the data is invalid (buffer too short or number
    too long.) */
size_t GetCollatableUInt(slice buf, uint64_t *n NONNULL);

}
