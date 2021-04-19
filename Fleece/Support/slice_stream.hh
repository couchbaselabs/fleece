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
#include "fleece/slice.hh"
#include <optional>

namespace fleece {


    /** A simple fixed-capacity output stream that writes to memory. */
    class slice_ostream {
    public:
        /// Constructs a stream that will write to memory at `begin` with a capacity of `cap`.
        slice_ostream(void* begin, size_t cap)   :_begin(begin), _next((uint8_t*)begin)
                                                 ,_end(_next + cap) { }

        /// Constructs a stream that will write to the memory pointed to by a slice.
        /// \warning Like a `const_cast`, this violates the read-only nature of the slice.
        explicit slice_ostream(slice s)          :slice_ostream((uint8_t*)s.buf, s.size) { }

        /// Captures the stream's state to another stream. You can use this as a way to "rewind"
        /// the stream afterwards. (This is equivalent to a copy constructor, but that constructor
        /// is explicitly deleted to avoid confusion.)
        slice_ostream capture() const            {return slice_ostream(*this);}

        // Utility that allocates a buffer, lets the callback write into it, then trims the buffer.
        // The callback must take a `slice_ostream&` parameter and return `bool`.
        // (If you need a growable buffer, use a \ref Writer instead.)
        template <class CALLBACK>
        static alloc_slice alloced(size_t maxSize, const CALLBACK &writer) {
            alloc_slice buf(maxSize);
            slice_ostream out(buf);
            if (!writer(out) || out.overflowed())
                return nullslice;
            buf.shorten(out.bytesWritten());
            return buf;
        }

        /// The data written so far.
        slice output() const noexcept FLPURE            {return slice(_begin, _next);}

        /// The number of bytes written so far.
        size_t bytesWritten() const noexcept FLPURE     {return _next - (uint8_t*)_begin;}

        /// The number of bytes more that can be written.
        size_t capacity() const noexcept FLPURE         {return _end - _next;}

        /// True if the stream is full (capacity is zero.)
        bool full() const noexcept FLPURE               {return _next >= _end;}

        /// True if at least one write or advance operation has failed (returned false.)
        bool overflowed() const noexcept FLPURE         {return _overflowed;}

#pragma mark  WRITING:

        // NOTE: The write methods are "all-or-nothing": they return false and set the
        // \ref overflowed flag, instead of writing any data, if there's not enough capacity.

        /// Writes exactly `size` bytes from `src`.
        bool write(const void *src, size_t size) noexcept;

        /// Writes the bytes of a slice.
        bool write(slice s) noexcept            {return write(s.buf, s.size);}

        /// Writes a single byte.
        bool writeByte(uint8_t) noexcept;

        /// Writes an ASCII hex representation of the bytes in `src`.
        bool writeHex(pure_slice src) noexcept;

        /// Writes an ASCII hex number.
        bool writeHex(uint64_t) noexcept;

        /// Writes an ASCII unsigned decimal number.
        bool writeDecimal(uint64_t) noexcept;

        /// Writes a number in Google/Go Unsigned VarInt format. (See varint.hh)
        bool writeUVarInt(uint64_t) noexcept;

#pragma mark  CUSTOM WRITING:

        /// Returns a pointer to where the next byte will be written.
        MUST_USE_RESULT
        void* next() noexcept                   {return _next;}

        /// Returns the entire remaining writeable buffer.
        mutable_slice buffer() noexcept         {return {_next, _end};}

        /// Makes `next` the next byte to be written, or returns false if it's past the end.
        /// Call this after you've written data yourself.
        bool advanceTo(void *next) noexcept;

        /// Advances the `next` pointer by `n` bytes, or returns false if there's no room.
        /// Call this after you've written data yourself.
        bool advance(size_t n) noexcept;

        /// Moves the `next` pointer back `n` bytes, "un-writing" data.
        void retreat(size_t n);

    private:
        // Pass-by-value is intentionally forbidden to make passing a `slice_stream` as a
        // parameter illegal. That's because its behavior would be wrong: the caller's stream
        // position wouldn't advance after the callee wrote data.
        // Always pass a reference, `slice_ostream&`.
        slice_ostream(const slice_ostream&) = default;

        void*    _begin;                // Beginning of output buffer
        uint8_t* _next;                 // Address of next byte to write
        uint8_t* _end;                  // End of buffer (past last byte)
        bool     _overflowed {false};   // Set to true if any write fails (returns false)
    };


    inline slice_ostream& operator<< (slice_ostream &out, slice data) noexcept {
        out.write(data); return out;
    }
    inline slice_ostream& operator<< (slice_ostream &out, uint8_t byte) noexcept {
        out.writeByte(byte); return out;
    }



    /** A simple stream that reads from memory using a slice to keep track of the available bytes. */
    struct slice_istream : public slice {
        // slice_istream is constructed from a slice, or from the same parameters as a slice.
        constexpr slice_istream(const slice &s) noexcept        :slice(s) { }
        constexpr slice_istream(const alloc_slice &s) noexcept  :slice(s) { }
        constexpr slice_istream(const void* b, size_t s) noexcept STEPOVER    :slice(b, s) {}
        constexpr slice_istream(const void* s NONNULL, const void* e NONNULL) noexcept STEPOVER
                                                                                :slice(s, e) { }
        /// The number of bytes remaining to be read.
        size_t bytesRemaining() const noexcept FLPURE           {return size;}

        /// Returns true when there's no more data to read.
        bool eof() const noexcept FLPURE                        {return size == 0;}

#pragma mark  READING:

        /// Reads _exactly_ `nBytes` bytes and returns them as a \ref slice.
        /// (This doesn't actually copy any memory, just does some pointer arithmetic.)
        /// If not enough bytes are available, returns `nullslice`.
        slice readAll(size_t nBytes) noexcept;

        /// Reads _up to_ `nBytes` bytes and returns them as a \ref slice.
        /// (This doesn't actually copy any memory, just does some pointer arithmetic.)
        slice readAtMost(size_t nBytes) noexcept;

        /// Copies _exactly_ `dstSize` bytes to `dstBuf` and returns true.
        /// If not enough bytes are available, copies nothing and returns false.
        bool readAll(void *dstBuf, size_t dstSize) noexcept;

        /// Copies _up to_ `dstSize` bytes to `dstBuf`, and returns the number of bytes copied.
        size_t readAtMost(void *dstBuf, size_t dstSize) noexcept;

        /// Searches for `delim`. If found, it returns all the data before it and moves the stream
        /// position past it.
        /// If the delimiter is not found, returns `nullslice` and does not advance the stream.
        slice readToDelimiter(slice delim) noexcept;

        /// Searches for `delim`. If found, it returns all the data before it and moves the stream
        /// position past it.
        /// If the delimiter is not found, returns all the data in the stream and advances to EOF.
        slice readToDelimiterOrEnd(slice delim) noexcept;

        /// Reads consecutive bytes as long as they're contained in the data of `set`.
        /// Returns the bytes read (possibly an empty slice.)
        slice readBytesInSet(slice set) noexcept;

        /// Reads the next byte. If the stream is already at EOF, returns 0.
        uint8_t readByte() noexcept;

        /// Returns the next byte, or 0 if at EOF, but does not advance the stream.
        uint8_t peekByte() const noexcept FLPURE            {return (size > 0) ? (*this)[0] : 0;}

        /// Returns all the remaining data that hasn't been read yet, without consuming it.
        slice peek() const noexcept FLPURE                      {return *this;}

#pragma mark  CUSTOM READING:

        /// Returns a pointer to where the next bytes will be read from.
        MUST_USE_RESULT
        const void* next() const noexcept FLPURE                {return buf;}

        /// Advances past `n` bytes without doing anything with them.
        void skip(size_t n)                                     {slice::moveStart(n);}

        /// Advances to the given address, skipping intervening bytes.
        void skipTo(const void *pos);

        /// Moves back to the given position, "un-reading" bytes.
        /// The `pos` value should come from a previous call to \ref next.
        void rewindTo(const void *pos);

#pragma mark - NUMERIC:

        /// Reads and decodes an ASCII hexadecimal number from the stream.
        /// It reads until there are no more digits, it hits EOF, or the number would overflow
        /// 64 bits. (If the very first byte isn't a hex digit, returns 0.)
        uint64_t readHex() noexcept;

        /// Reads and decodes an ASCII unsigned decimal number from the stream.
        /// It reads until there are no more digits, it hits EOF, or the number would overflow
        /// a `uint64_t`.
        /// (If the very first byte isn't a digit, returns 0.)
        uint64_t readDecimal() noexcept;

        /// Reads and decodes an ASCII signed decimal number from the stream.
        /// It first reads an optional "-" sign; then reads decimal digits until there are no more,
        /// or it hits EOF, or the number would overflow an `int64_t`.
        /// (If the first byte isn't a digit or "-" sign, returns 0.)
        int64_t readSignedDecimal() noexcept;

        /// Reads a 64-bit number in Google/Go unsigned-VarInt format. (See varint.hh)
        /// If the number is invalid, returns `nullopt` without advancing the stream.
        std::optional<uint64_t> readUVarInt() noexcept;

        /// Reads a 32-bit number in Google/Go unsigned-VarInt format. (See varint.hh)
        /// If the number is invalid or too large, returns `nullopt` without advancing the stream.
        std::optional<uint32_t> readUVarInt32() noexcept;

    private:
        // Pass-by-value is intentionally forbidden to make passing a `slice_istream` as a
        // parameter illegal. That's because its behavior would be wrong: reads made by the
        // callee would not be reflected in the caller. Always pass a reference, `slice_istream&`.
        slice_istream(const slice_istream&) = default;
    };
}
