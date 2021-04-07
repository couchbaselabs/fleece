//
// Writer.cc
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

#include "Writer.hh"
#include "PlatformCompat.hh"
#include "FleeceException.hh"
#include "decode.h"
#include "encode.h"
#include <algorithm>
#include "betterassert.hh"


namespace fleece {

    Writer::Writer(size_t initialCapacity)
    :_chunkSize(initialCapacity)
    ,_outputFile(nullptr)
    {
        addChunk(initialCapacity);
    }


    Writer::Writer(FILE *outputFile)
    :Writer(kDefaultInitialCapacity)
    {
        assert_precondition(outputFile);
        _outputFile = outputFile;
    }


    Writer::Writer(Writer&& w) noexcept
    :_available(std::move(w._available))
    ,_chunks(std::move(w._chunks))
    ,_chunkSize(w._chunkSize)
    ,_length(w._length)
    ,_outputFile(w._outputFile)
    {
        migrateInitialBuf(w);
        memcpy(_initialBuf, w._initialBuf, sizeof(_initialBuf));
        w._outputFile = nullptr;
    }


    Writer::~Writer() {
        if (_outputFile)
            flush();
        for (auto &chunk : _chunks)
            freeChunk(chunk);
    }


    Writer& Writer::operator= (Writer&& w) noexcept {
        _available = std::move(w._available);
        _length = w._length;
        _chunks = std::move(w._chunks);
        migrateInitialBuf(w);
        _outputFile = w._outputFile;
        memcpy(_initialBuf, w._initialBuf, sizeof(_initialBuf));
        w._outputFile = nullptr;
        return *this;
    }


    void Writer::_reset() {
        if (_outputFile)
            return;

        size_t nChunks = _chunks.size();
        if (nChunks > 1) {
            for (size_t i = 0; i < nChunks-1; i++)
                freeChunk(_chunks[i]);
            _chunks.erase(_chunks.begin(), _chunks.end() - 1);
        }
        _available = _chunks[0];
#if DEBUG
        // We will reuse the buffer, but it's invalid so fill it with garbage for troubleshooting:
        memset((void*)_available.buf, 0xdd, _available.size);
#endif
    }


    void Writer::reset() {
        _reset();
        _length = _available.size;  // effectively 0
    }


#if DEBUG
    void Writer::assertLengthCorrect() const {
        if (!_outputFile) {
            size_t len = 0;
            forEachChunk([&](slice chunk) {
                len += chunk.size;
            });
            assert_postcondition(len == length());
        }
    }
#endif


    void* Writer::_write(const void* dataOrNull, size_t length) {
        void* result;
        if (_usuallyTrue(length <= _available.size)) {
            result = (void*)_available.buf;
            if (dataOrNull)
                memcpy((void*)_available.buf, dataOrNull, length);
            _available.moveStart(length);
        } else {
            result = writeToNewChunk(dataOrNull, length);
        }
        // No need to add to _length; the length() method compensates.
        assertLengthCorrect();
        return result;
    }


    void* Writer::writeToNewChunk(const void* dataOrNull, size_t length) {
        // If we got here, a call to `write` or `reserveSpace` would not fit in the current chunk
        if (_outputFile) {
            flush();
            if (length > _chunkSize) {
                freeChunk(_chunks.back());
                _chunks.clear();
                addChunk(length);
            }
            _length -= _available.size;
            _available = _chunks[0];
            _length += _available.size;
        } else {
            if (_usuallyTrue(_chunkSize <= 64*1024))
                _chunkSize *= 2;
            addChunk(std::max(length, _chunkSize));
        }

        // Now that we have room, write:
        auto result = (void*)_available.buf;
        if (dataOrNull)
            memcpy((void*)_available.buf, dataOrNull, length);
        _available.moveStart(length);
        return result;
    }


    void Writer::flush() {
        if (!_outputFile)
            return;
        auto chunk = _chunks.back();
        size_t writtenLength = chunk.size - _available.size;
        if (writtenLength > 0) {
            _length -= _available.size;
            if (fwrite(chunk.buf, 1, writtenLength, _outputFile) < writtenLength)
                FleeceException::_throwErrno("Writer can't write to file");
            _available = chunk;
            _length += _available.size;
        }
        assertLengthCorrect();
    }


    void Writer::addChunk(size_t capacity) {
        _length -= _available.size;
        if (!_chunks.empty()) {
            auto &last = _chunks.back();
            // (should I realloc() it?)
            last.setSize(last.size - _available.size);
        }
        if (_chunks.empty() && capacity <= kDefaultInitialCapacity)
            _available = _chunks.emplace_back(_initialBuf, sizeof(_initialBuf));
        else
            _available = _chunks.emplace_back(slice::newBytes(capacity), capacity);
        _length += _available.size;
    }


    void Writer::freeChunk(slice chunk) {
        if (chunk.buf != &_initialBuf)
            ::free((void*)chunk.buf);
    }

    void Writer::migrateInitialBuf(const Writer& other) {
        // If a simple std::move is used for _chunks, there will be a leftover
        // garbage entry pointing to the old initial buffer of the previous
        // object. Replace it with the current initial buf.
        for(auto& chunk : _chunks) {
            if(chunk.buf == other._initialBuf) {
                chunk = slice(_initialBuf, chunk.size);
                break;
            }
        }

        // By this time, _available has been moved from the old object
        slice oldInitialBuf = {other._initialBuf, sizeof(other._initialBuf)};
        if(oldInitialBuf.containsAddress(_available.buf)) {
            const size_t availableOffset = oldInitialBuf.offsetOf(_available.buf);
            _available = slice(_initialBuf, sizeof(_initialBuf));
            _available.moveStart(availableOffset);
        }
    }



    std::vector<slice> Writer::output() const {
        std::vector<slice> result;
        result.reserve(_chunks.size());
        forEachChunk([&](slice chunk) {
            result.push_back(chunk);
        });
        return result;
    }


    void Writer::copyOutputTo(void *dst) const {
        forEachChunk([&](slice chunk) {
            chunk.copyTo(dst);
            dst = offsetby(dst, chunk.size);
        });
    }


    alloc_slice Writer::copyOutput() const {
        assert(!_outputFile);
        alloc_slice output(length());
        copyOutputTo((void*)output.buf);
        return output;
    }


    alloc_slice Writer::finish() {
        alloc_slice output;
        if (_outputFile) {
            flush();
        } else {
            output = copyOutput();
            reset();
        }
        assertLengthCorrect();
        return output;
    }


    bool Writer::writeOutputToFile(FILE *f) {
        assert_precondition(!_outputFile);
        bool result = true;
        forEachChunk([&](slice chunk) {
            if (result && fwrite(chunk.buf, chunk.size, 1, f) < chunk.size)
                result = false;
        });
        if (result) {
            auto len = length();
            _reset();       // don't reset _length, so offsets remain consistent after
            _length = len - _available.size;
        }
        return result;
    }


#pragma mark - BASE64:


    void Writer::writeBase64(slice data) {
        size_t base64size = ((data.size + 2) / 3) * 4;
        char *dst;
        if (_outputFile)
            dst = (char*)slice::newBytes(base64size);
        else
            dst = (char*)reserveSpace(base64size);
        base64::encoder enc;
        enc.set_chars_per_line(0);
        size_t written = enc.encode(data.buf, data.size, dst);
        written += enc.encode_end(dst + written);
        if (_outputFile) {
            write(dst, written);
            free(dst);
        }
        assert_postcondition((size_t)written == base64size);
        (void)written;      // suppresses 'unused value' warning in release builds
    }


    void Writer::writeDecodedBase64(slice base64) {
        base64::decoder dec;
        std::vector<char> buf((base64.size + 3) / 4 * 3);
        size_t len = dec.decode(base64.buf, base64.size, buf.data());
        write(buf.data(), len);
    }

}
