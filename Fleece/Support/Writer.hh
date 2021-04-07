//
// Writer.hh
//
// Copyright (c) 2015 Couchbase, Inc All rights reserved.
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
#include "SmallVector.hh"
#include <stdio.h>
#include <vector>
#include "betterassert.hh"

namespace fleece {

    /// A simple write-only stream that buffers its output into a slice.
    ///  (Used instead of C++ ostreams because those have too much overhead.)
    class Writer {
    public:
        static constexpr size_t kDefaultInitialCapacity = 256;

        explicit Writer(size_t initialCapacity =kDefaultInitialCapacity);
        ~Writer();

        Writer(Writer&&) noexcept;
        Writer& operator= (Writer&&) noexcept;

        /// The number of bytes written.
        size_t length() const                   {return _length - _available.size;}

        //-------- Writing:

        /// Writes data. Returns a pointer to where the data got written to.
        const void* write(slice s)              {return _write(s.buf, s.size);}

        const void* write(const void* data NONNULL, size_t length)  {return _write(data, length);}

        Writer& operator<< (uint8_t byte)       {_write(&byte,1); return *this;}
        Writer& operator<< (slice s)            {write(s); return *this;}

        /// Pads output to even length by writing a zero byte if necessary.
        void padToEvenLength()                  {if (length() & 1) *this << 0;}

        /// Encodes the data to base64 format and writes that to the output.
        /// The encoded data will contain no line breaks.
        void writeBase64(slice data);

        /// Decodes a base64-encoded string and writes it to the output.
        void writeDecodedBase64(slice base64String);

        //-------- Zero-Copy Writing:

        /// Reserves space for data, but leaves that space uninitialized.
        /// The data must be filled in later, before accessing the output,
        /// otherwise there will be garbage in the output.
        void* reserveSpace(size_t length)       {return _write(nullptr, length);}

        /// Reserves space for \ref count values of type \ref T.
        template <class T>
        T* reserveSpace(size_t count)           {return (T*) reserveSpace(count * sizeof(T));}

        /// Reserves `maxLength` bytes of space and passes a pointer to the callback.
        /// The callback must write _up to_ `maxLength` bytes, then return the byte count written.
        template <class T>
        void* write(size_t maxLength, T callback) {
            auto dst = (uint8_t*)reserveSpace(maxLength);
            size_t usedLength = callback(dst);
            _available.moveStart((ssize_t)usedLength - (ssize_t)maxLength);
            return dst;
        }

        //-------- Accessing the output

        /// Returns the data written, in pieces. Does not change the state of the Writer.
        std::vector<slice> output() const;

        /// Returns the data written, as a single allocated slice. Does not change state.
        alloc_slice copyOutput() const;

        /// Copies the output data to memory starting at `dst`.
        void copyOutputTo(void *dst) const;

        /// Invokes the callback for each range of bytes in the output.
        template <class T>
        void forEachChunk(T callback) const {
            assert_precondition(!_outputFile);
            auto n = _chunks.size();
            for (auto chunk : _chunks) {
                if (_usuallyFalse(--n == 0)) {
                    chunk.setSize(chunk.size - _available.size);
                    if (chunk.size == 0)
                        continue;
                }
                callback(chunk);
            }
        }

        /// Writes the output to a file. (Must not already be writing to a file.)
        bool writeOutputToFile(FILE *);

        //-------- Finishing:

        /// Clears the Writer, discarding the data written. It can then be reused.
        void reset();

        /// Returns a copy of the data written, and resets.
        alloc_slice finish();

        /// Passes the output as a slice (not alloc_slice) to the callback, and resets.
        /// Unlike the regular `finish`, this avoids heap allocation if there's only one chunk.
        template <class T>
        void finish(T callback) {
            if (_chunks.size() == 1) {
                slice chunk = _chunks[0];
                chunk.setSize(chunk.size - _available.size);
                callback(chunk);
                reset();
            } else {
                alloc_slice output = finish();
                callback(output);
            }
        }

        //-------- Writing Directly To A File:

        /// Constructs a Writer that writes directly to a file.
        /// * Its output cannot be accessed directly; all the `output` methods fail assertions.
        /// * The memory reserved by `reserveSpace` must be written to before _the next write_,
        ///   not just before finishing, otherwise the uninitialized data may be flushed to disk.
        /// * `reset` has no effect.
        /// * `finish` calls `flush`, but returns null.
        explicit Writer(FILE * NONNULL outputFile);

        /// The output file, or NULL.
        FILE* outputFile() const                {return _outputFile;}

        /// If writing to a file, flushes to disk. Otherwise a no-op.
        void flush();


    private:
        void _reset();
        void* _write(const void* dataOrNull, size_t length);
        void* writeToNewChunk(const void* dataOrNull, size_t length);
        void addChunk(size_t capacity);
        void freeChunk(slice);
        void migrateInitialBuf(const Writer& other);

        Writer(const Writer&) = delete;
        const Writer& operator=(const Writer&) = delete;

#if DEBUG
        void assertLengthCorrect() const;
#else
        void assertLengthCorrect() const { }
#endif

        /* IMPLEMENTATION NOTES:
           The output is stored in a vector of "chunks", each a slice.
           The first chunk usually points to `_initialBuf`, if the `initialCapacity` permits;
           This avoids a `malloc` in the common case of writing short output (256 bytes or less.)
           All subsequent chunks are heap-allocated, each twice as big as the last.

           The last chunk is usually partially empty. `_available` points to its empty space,
           so `_available.buf` is the first free byte, and `_available.size` is the free size.

           Writes are never split across chunks; instead, if a write is too big to fit the
           available space, a new chunk is allocated. (Yes, this can waste some space at the end.)
           This is done so that the caller can access the written data contiguously afterwards,
           which is important for the Fleece Encoder class.
        */

        // TODO: Make _available a slice_stream

        slice _available;               // Available range of current chunk
        smallVector<slice, 4> _chunks;  // Chunks in consecutive order. Last is written to.
        size_t _chunkSize;              // Size of next chunk to allocate
        size_t _length {0};             // Output length, offset by _available.size
        FILE* _outputFile;              // File writing to, or NULL
        uint8_t _initialBuf[kDefaultInitialCapacity];   // Inline buffer to avoid a malloc
    };

}
