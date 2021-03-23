//
// Base64.cc
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

#include "Base64.hh"
#include "encode.h"
#include "decode.h"
#include "betterassert.hh"

namespace fleece { namespace base64 {

    std::string encode(slice data) {
        std::string str;
        size_t strLen = ((data.size + 2) / 3) * 4;
        str.resize(strLen);
        char *dst = &str[0];
        ::base64::encoder enc;
        enc.set_chars_per_line(0);
        size_t written = enc.encode(data.buf, data.size, dst);
        written += enc.encode_end(dst + written);
        assert(written == strLen);
        (void)written;  // avoid compiler warning in release build when 'assert' is a no-op
        return str;
    }


    alloc_slice decode(slice b64) {
        size_t expectedLen = (b64.size + 3) / 4 * 3;
        alloc_slice result(expectedLen);
        slice decoded = decode(b64, (void*)result.buf, result.size);
        if (decoded.size == 0)
            return nullslice;
        assert(decoded.size <= expectedLen);
        result.resize(decoded.size);
        return result;
    }


    slice decode(slice b64, void *outputBuffer, size_t bufferSize) noexcept {
        size_t expectedLen = (b64.size + 3) / 4 * 3;
        if (expectedLen > bufferSize)
            return nullslice;
        ::base64::decoder dec;
        size_t len = dec.decode(b64.buf, b64.size, outputBuffer);
        assert(len <= bufferSize);
        return slice(outputBuffer, len);
    }

} }
