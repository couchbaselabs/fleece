//
// KeyTree.cc
//
// Copyright (c) 2016 Couchbase, Inc All rights reserved.
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

#include "KeyTree.hh"
#include "varint.hh"
#include <math.h>
#include <iostream>
#include <algorithm>
#include "slice_stream.hh"
#include "PlatformCompat.hh"
#include "betterassert.hh"

#if (defined __ANDROID__) && (__ANDROID_API__ < 18)
#define log2(x) (log(x) / log(2))
#endif
namespace fleece {


    // Data format of a tree is:
    // depth                    1 byte
    // [root node]
    //
    // Data format of a tree node is:
    // string length            varint
    // string                   variable
    // offset to right subtree  varint
    // [left subtree]
    // [right subtree]
    //
    // Offset to right subtree is 0 if there is no right subtree,
    // and the field is entirely missing in the bottom nodes (which have no subtrees.)


#pragma mark - WRITING:

    class keyTreeWriter {
    public:
        keyTreeWriter(const std::vector<slice> &strings)
        :_strings(strings),
         _sizes(strings.size())
        { }

        alloc_slice writeTree() {
            auto n = _strings.size();
            size_t totalSize = 1 + sizeKeyTree(0, n);
            alloc_slice output(totalSize);
            _out = (uint8_t*)output.buf;

            writeByte((uint8_t)ceil(log2(n)));   // Write the depth first
            writeKeyTree(0, n);
            assert_postcondition(_out == output.end());
            return output;
        }

    private:
        const std::vector<slice> &_strings;
        std::vector<size_t> _sizes;
        uint8_t* _out;

        // Same logic as writeKeyTree, but just returns the size it would write, without writing.
        size_t sizeKeyTree(size_t begin, size_t end)
        {
            size_t mid = (begin + end) / 2;
            slice str = _strings[mid];
            size_t size = SizeOfVarInt(str.size) + str.size;   // middle string, with length prefix

            if (end - begin > 1) {
                size_t leftSize = sizeKeyTree(begin, mid);
                if (mid+1 < end) {
                    size += SizeOfVarInt(leftSize);             // right subtree offset
                    size += leftSize;                           // left subtree
                    size += sizeKeyTree(mid+1, end);            // right subtree
                } else {
                    size += 1;                                  // no right subtree (offset 0)
                    size += leftSize;                           // left subtree
                }
            }
            _sizes[mid] = size;
            return size;
        }

        void writeKeyTree(size_t begin, size_t end)
        {
            size_t mid = (begin + end) / 2;
            // Write middle string, with length prefix:
            slice str = _strings[mid];
            writeVarInt(str.size);
            write(str);

            if (end - begin > 1) {
                if (mid+1 < end) {
                    size_t leftSize = _sizes[(begin + mid) / 2];
                    writeVarInt(leftSize);                  // Write right subtree offset
                    writeKeyTree(begin, mid);               // Write left subtree
                    writeKeyTree(mid+1, end);               // Write right subtree
                } else {
                    writeByte(0);                           // No right subtree (offset 0)
                    writeKeyTree(begin, mid);               // Write left subtree
                }
            }
        }

        inline void writeByte(uint8_t byte) {
            *_out++ = byte;
        }

        inline void write(slice s) {
            s.copyTo(_out);
            _out += s.size;
        }

        inline size_t writeVarInt(size_t n) {
            size_t size = PutUVarInt(_out, n);
            _out += size;
            return size;
        }
    };
    

    KeyTree KeyTree::fromSortedStrings(const std::vector<slice>& strings) {
        return KeyTree(keyTreeWriter(strings).writeTree());
    }

    KeyTree KeyTree::fromStrings(std::vector<slice> strings) {
        std::sort(strings.begin(), strings.end());
        return fromSortedStrings(strings);
    }


#pragma mark - READING:

    KeyTree::KeyTree(const void *encodedDataStart)
    :_data(encodedDataStart)
    { }

    KeyTree::KeyTree(alloc_slice encoded)
    :_ownedData(encoded),
    _data(encoded.buf)
    { }
    
    static int32_t readVarInt(const uint8_t* &tree) {
        slice_istream buf(tree, kMaxVarintLen32);
        std::optional<uint32_t> n = buf.readUVarInt();
        if (!n)
            return -1;
        tree = (const uint8_t*)buf.buf;
        return *n;
    }

    static slice readKey(const uint8_t* &tree) {
        slice key;
        int32_t len = readVarInt(tree);
        if (len >= 0) {
            key = slice(tree, len);
            tree += len;
        }
        return key;
    }


    unsigned KeyTree::operator[] (slice str) const {
        const uint8_t* tree = (const uint8_t*)_data;
        unsigned id = 0;
        unsigned mask = 1;
        for (unsigned depth = *tree++; depth > 0; --depth) {
            slice key = readKey(tree);
            if (!key.buf)
                return 0; // parse error
            int cmp = str.compare(key);
            if (cmp == 0)
                return id | mask;
            if (depth == 1)
                return 0;
            int32_t leftTreeSize = readVarInt(tree);
            if (leftTreeSize < 0)
                return 0; // parse error
            if (cmp > 0) {
                tree += leftTreeSize;
                id |= mask;
            }
            mask <<= 1;
        }
        return 0;
    }

    slice KeyTree::operator[] (unsigned id) const {
        if (id == 0)
            return nullslice;
        const uint8_t* tree = (const uint8_t*)_data;
        for (unsigned depth = *tree++; depth > 0; --depth) {
            slice key = readKey(tree);
            if (!key.buf)
                return key; // parse error
            if (id == 1)
                return key;
            if (depth == 1)
                break;
            int32_t leftTreeSize = readVarInt(tree);
            if (leftTreeSize < 0)
                return nullslice; // parse error
            if (id & 1) {
                if (leftTreeSize == 0)
                    return nullslice; // no right subtree
                tree += leftTreeSize;
            }
            id >>= 1;
        }
        return nullslice;
    }

}
