//
// slice_stream.hh
//
// Copyright © 2021 Couchbase. All rights reserved.
//
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
#include "slice.hh"

namespace fleece {


    /** A simple fixed-capacity output stream that writes to memory. */
    class slice_stream {
    public:
        /// Constructs a stream that will write to memory at `begin` with a capacity of `cap`.
        slice_stream(void* begin, size_t cap)   :_begin(begin), _next((uint8_t*)begin)
                                                 ,_capacity(cap) { }

        /// Constructs a stream that will write to the memory pointed to by a slice.
        /// \warning This implicitly violates the read-only nature of the slice.
        explicit slice_stream(slice s)          :slice_stream((uint8_t*)s.buf, s.size) { }

        // Utility that allocates a buffer, lets the callback write into it, then trims the buffer.
        template <class WRITER>
        static alloc_slice alloced(size_t maxSize, const WRITER &writer) {
            alloc_slice buf(maxSize);
            slice_stream out(buf);
            if (!writer(out))
                return nullslice;
            buf.shorten(out.bytesWritten());
            return buf;
        }

        /// The data written so far.
        slice output() const noexcept           {return slice(_begin, _next);}

        /// The number of bytes written so far.
        size_t bytesWritten() const noexcept    {return _next - (uint8_t*)_begin;}

        /// The number of bytes more that can be written.
        size_t capacity() const noexcept        {return _capacity;}

        /// True if the stream is full (capacity is zero.)
        bool full() const noexcept              {return _capacity == 0;}

#pragma mark - WRITING:

        // Note: The write methods all return false (and write nothing) if there's not enough room.

        /// Writes `size` bytes from `src` and returns true;
        /// or writes nothing and returns false if there's not enough room.
        bool write(const void *src, size_t size) noexcept;

        /// Writes bytes from a slice.
        bool write(slice s) noexcept            {return write(s.buf, s.size);}

        /// Writes a single byte.
        bool writeByte(uint8_t) noexcept;

        /// Writes an ASCII hex representation of the bytes in `src`.
        bool writeHex(pure_slice src) noexcept;

        /// Writes a hex number.
        bool writeHex(uint64_t) noexcept;

        /// Writes a decimal number.
        bool writeDecimal(uint64_t) noexcept;

#pragma mark - CUSTOM WRITING:

        /// Returns a pointer to where the next byte will be written.
        void* next() noexcept                   {return _next;}

        /// Returns the entire remaining writeable buffer.
        mutable_slice buffer() noexcept         {return {_next, _capacity};}

        /// Makes `next` the next byte to be written. Call this if you've written data yourself.
        void advanceTo(void *next);

        /// Advances the `next` pointer by `n` bytes. Call this if you've written data yourself.
        void advance(size_t n);

        /// Moves the `next` pointer back `n` bytes, "un-writing" data.
        void retreat(size_t n);

    private:
        void*    _begin;
        uint8_t* _next;
        size_t   _capacity;
    };

}
