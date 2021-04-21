//
// Encoder.cc
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

#include "Encoder.hh"
#include "FleeceImpl.hh"
#include "Pointer.hh"
#include "SharedKeys.hh"
#include "MutableDict.hh"
#include "Endian.hh"
#include "varint.hh"
#include "FleeceException.hh"
#include "ParseDate.hh"
#include "PlatformCompat.hh"
#include "TempArray.hh"
#include <algorithm>
#include <cmath>
#include <float.h>
#include <stdlib.h>
#include "betterassert.hh"


namespace fleece { namespace impl {
    using namespace internal;

    const slice Encoder::kPreEncodedTrue  = {Value::kTrueValue,  kNarrow};
    const slice Encoder::kPreEncodedFalse = {Value::kFalseValue, kNarrow};
    const slice Encoder::kPreEncodedNull  = {Value::kNullValue,  kNarrow};
    const slice Encoder::kPreEncodedEmptyDict = {Dict::kEmpty,   kNarrow};

    Encoder::Encoder(size_t reserveSize)
    :_out(reserveSize),
     _stack(kInitialStackSize),
     _strings(20)
    {
        init();
    }

    Encoder::Encoder(FILE *outputFile)
    :_out(outputFile),
     _stack(kInitialStackSize),
     _strings(10)
    {
        init();
    }

    Encoder::~Encoder() =default;

    void Encoder::init() {
        // Initial state has a placeholder collection on the stack, which will contain the real
        // root value.
        resetStack();
        _items->reset(kSpecialTag);
        _items->reserve(1);
    }

    void Encoder::resetStack() {
        _items = &_stack[0];
        _stackDepth = 1;
    }

    void Encoder::reset() {
        if (_items)
            _items->clear();
        _out.reset();
        _strings.clear();
        _stringStorage.reset();
        _writingKey = _blockedOnKey = false;
        resetStack();
        setBase(nullslice);
    }

    void Encoder::setSharedKeys(SharedKeys *s) {
        _sharedKeys = s;
    }

    void Encoder::setBase(slice base, bool markExternPointers, size_t cutoff) {
        throwIf(_base && base, EncodeError, "There's already a base");
        _base = base;
        _ownedBase = nullslice;
        _baseCutoff = nullptr;
        if (base && cutoff > 0 && cutoff < base.size) {
            assert_precondition(cutoff >= 8);
            _baseCutoff = (char*)base.end() - cutoff;
        }
        _baseMinUsed = _base.end();
        _markExternPtrs = markExternPointers;
    }

    void Encoder::end() {
        if (!_items)
            return;
        throwIf(_stackDepth > 1, EncodeError, "unclosed array/dict");
        throwIf(_items->size() > 1, EncodeError, "top level must have only one value");

        if (_trailer && !_items->empty()) {
            checkPointerWidths(_items, nextWritePos());
            fixPointers(_items);
            Value &root = (*_items)[0];
            if (_items->wide) {
                _out.write(&root, kWide);
                // Top level Value is 4 bytes, so append a 2-byte pointer to it, because the trailer
                // needs to be a 2-byte Value:
                new (_out.reserveSpace(kNarrow)) Pointer(4, kNarrow);
            } else {
                _out.write(&root, kNarrow);
            }
            _items->clear();
        }
        _out.flush();
        // Go to "finished" state, where stack is empty:
        _items = nullptr;
        _stackDepth = 0;
    }

    size_t Encoder::finishItem() {
        throwIf(_stackDepth > 1, EncodeError, "unclosed array/dict");
        throwIf(!_items || _items->empty(), EncodeError, "No item to end");

        size_t itemPos;
        const Value *item = &(*_items)[0];
        if (item->isPointer()) {
            itemPos = item->_asPointer()->offset<true>() - _base.size;
        } else {
            itemPos = nextWritePos();
            _out.write(item, (_items->wide ? kWide : kNarrow));
        }
        _items->clear();
        resetStack();
        return itemPos;
    }

    alloc_slice Encoder::finish() {
        end();
        alloc_slice out = _out.finish();
        if (out.size == 0)
            out.reset();
        return out;
    }

    Retained<Doc> Encoder::finishDoc() {
        Retained<Doc> doc = new Doc(finish(),
                                    Doc::kTrusted,
                                    _sharedKeys,
                                    (_markExternPtrs ? _base : slice()));
        return doc;
    }

    // Returns position in the stream of the next write. Pads stream to even pos if necessary.
    size_t Encoder::nextWritePos() {
        _out.padToEvenLength();
        return _out.length();
    }

    // Returns an opaque reference to the last value written, but not if it's inline.
    Encoder::PreWrittenValue Encoder::lastValueWritten() const {
        if (!_items->empty())
            if (auto ptr = _items->back()._asPointer(); ptr->isPointer())
                return PreWrittenValue(ptr->offset<true>());
        return PreWrittenValue::none;
    }

    // Writes a pointer to an already-written value, allowing it to appear twice without overhead.
    void Encoder::writeValueAgain(PreWrittenValue pos) {
        throwIf(pos == PreWrittenValue::none, EncodeError, "Can't rewrite an inline Value");
        writePointer(ssize_t(pos) - _base.size);
    }


    alloc_slice Encoder::snip() {
        throwIf(_base, EncodeError, "Can't snip when there's already a base");
        // Find the last value that was written:
        auto pos = lastValueWritten();
        if (pos == PreWrittenValue::none)
            return nullslice;

        // Write a pointer to it, to form the top of the snipped document:
        auto offset = _out.length() - ssize_t(pos);
        new (_out.reserveSpace(kNarrow)) Pointer(offset, kNarrow);

        // Collect what's been written so far and return a copy:
        alloc_slice result = _out.finish();
        setBase(result, true);
        _ownedBase = result;
        return result;
    }


#pragma mark - WRITING:

    // Adds an empty Value to the current collection's item list and returns a pointer to it.
    // Caller is responsible for initializing the Value.
    uint8_t* Encoder::placeItem() {
        throwIf(_blockedOnKey, EncodeError, "need a key before this value");
        if (_writingKey) {
            _writingKey = false;
        } else {
            if (_items->tag == kDictTag)
                _blockedOnKey = _writingKey = true;
        }

        return (uint8_t*) _items->push_back_new();
    }

    // Writes blank space for a Value of the given size and returns a pointer to it.
    template <bool canInline>
    uint8_t* Encoder::placeValue(size_t size) {
        byte *buf;
        if (canInline && size <= 4) {
            buf = placeItem();
            if (size < 4)
                buf[2] = buf[3] = 0;
            if (size > 2)
                _items->wide = true;
            return buf;
        } else {
            writePointer(nextWritePos());
            bool pad = (size & 1);
            buf = _out.reserveSpace<byte>(size + pad);
            if (pad)
                buf[size] = 0;
        }
        return buf;
    }

    template <bool canInline>
    uint8_t* Encoder::placeValue(tags tag, byte param, size_t size) {
        assert_precondition(param <= 0x0F);
        byte *buf = placeValue<canInline>(size);
        buf[0] = byte((tag << 4) | param);
        return buf;
    }


#pragma mark - SCALARS:

    void Encoder::addSpecial(int specVal)  {new (placeItem()) Value(kSpecialTag, specVal);}
    void Encoder::writeNull()              {addSpecial(kSpecialValueNull);}
    void Encoder::writeUndefined()         {addSpecial(kSpecialValueUndefined);}
    void Encoder::writeBool(bool b)        {addSpecial(b ? kSpecialValueTrue : kSpecialValueFalse);}

    void Encoder::writeInt(uint64_t i, bool isSmall, bool isUnsigned) {
        if (isSmall) {
            new (placeItem()) Value(kShortIntTag, (i >> 8) & 0x0F, i & 0xFF);
        } else {
            byte intbuf[10];
            auto size = PutIntOfLength(intbuf, i, isUnsigned);
            byte *buf = placeValue<false>(kIntTag, byte(size - 1), 1 + size);
            if (isUnsigned)
                buf[0] |= 0x08;
            memcpy(buf + 1, intbuf, size);
        }
    }

    void Encoder::writeInt(int64_t i)   {writeInt(i, (i < 2048 && i >= -2048), false);}
    void Encoder::writeUInt(uint64_t i) {writeInt(i, (i < 2048),               true);}

    void Encoder::writeDouble(double n) {
        throwIf(std::isnan(n), InvalidData, "Can't write NaN");
#if 0 // Perhaps an option in the future?
        if (isIntRepresentable(n)) {
            return writeInt((int64_t)n);
        } else
#endif
        if (isFloatRepresentable(n)) {
            return _writeFloat((float)n);
        } else {
            endian::littleEndianDouble swapped = n;
            auto buf = placeValue<false>(kFloatTag, 0x08, 2 + sizeof(swapped));
            buf[1] = 0;
            memcpy(&buf[2], &swapped, sizeof(swapped));
        }
    }

    void Encoder::writeFloat(float n) {
        throwIf(std::isnan(n), InvalidData, "Can't write NaN");
#if 0 // Perhaps an option in the future?
        if (isIntRepresentable(n))
            writeInt((int32_t)n);
        else
#endif
            _writeFloat(n);
    }

    void Encoder::_writeFloat(float n) {
        endian::littleEndianFloat swapped = n;
        auto buf = placeValue<false>(kFloatTag, 0, 2 + sizeof(swapped));
        buf[1] = 0;
        memcpy(&buf[2], &swapped, sizeof(swapped));
    }

#if 0
    bool Encoder::isIntRepresentable(float n) noexcept {
        return (n <= INT32_MAX && n >= INT32_MIN && n == floorf(n));
    }

    bool Encoder::isIntRepresentable(double n) noexcept {
        return (n <= INT64_MAX && n >= INT64_MIN && n == floor(n));
    }
#endif

    bool Encoder::isFloatRepresentable(double n) noexcept {
        return (fabs(n) <= FLT_MAX && n == (float)n);
    }


#pragma mark - STRINGS / DATA:

    // Subroutine for writing strings or binary data. Returns the address of the string in the
    // output (not the Value; the raw string itself), which can be used until the enoding is over.
    // (Unless the string is written inline, or directly to file, in which case NULL is returned.)
    const void* Encoder::writeData(tags tag, slice s) {
        byte *buf;
        if (s.size < kNarrow) {
            // Tiny data (0 or 1 byte) fits inline:
            buf = placeValue<true>(tag, byte(s.size), 1 + s.size);
            buf[1] = s.size > 0 ? s[0] : 0;
            buf = nullptr; // this string is ephemeral
        } else {
            // Large data doesn't:
            size_t bufLen = 1 + s.size;
            if (s.size >= 0x0F)
                bufLen += SizeOfVarInt(s.size);
            buf = placeValue<false>(tag, 0, bufLen);
            if (s.size < 0x0F) {
                *buf++ |= byte(s.size);
            } else {
                *buf++ |= 0x0F;
                buf += PutUVarInt(buf, s.size);
            }
            memcpy(buf, s.buf, s.size);
            if (_out.outputFile())
                buf = nullptr;          // ephemeral if writing to file
        }
        return buf;
    }

    // Writes a string, or a pointer to an already-written copy of the same string.
    // This is the main body of writeString() and writeKey().
    // Returns the address where s got written to, if possible, just like writeData above.
    const void* Encoder::_writeString(slice s) {
        if (!_usuallyTrue(_uniqueStrings && s.size >= kNarrow && s.size <= kMaxSharedStringSize)) {
            // Not uniquing this string, so just write it:
            return writeData(kStringTag, s);
        }

        // Check whether this string's already been written:
        StringTable::entry_t *entry;
        bool isNew;
        std::tie(entry, isNew) = _strings.insert(s, 0);
        if (!isNew) {
            // String exists: Write pointer to it, as long as the offset's not too large:
            ssize_t offset = entry->second - _base.size;
            if (_items->wide || nextWritePos() - offset <= Pointer::kMaxNarrowOffset - 32) {
                writePointer(offset);
                if (offset < 0) {
                    const void *stringVal = &_base[_base.size + offset];
                    if (stringVal < _baseMinUsed)
                        _baseMinUsed = stringVal;
                }
#ifndef NDEBUG
                _numSavedStrings++;
#endif
                return entry->first.buf; // done!
            }
        }

        // Write the string to the output:
        auto offset = _base.size + nextWritePos();
        throwIf(offset > 1u<<31, MemoryError, "encoded data too large");
        writeData(kStringTag, s);

        // Store a copy of the string, since _out won't necessarily keep it around (if it's
        // writing to a file, or if the caller calls snip()), and the offset:
        const void* writtenStr = _stringStorage.write(s);
        *entry = {{writtenStr, s.size}, (uint32_t)offset};
        return writtenStr;
    }

    // Adds a preexisting string to the cache
    void Encoder::cacheString(slice s, size_t offsetInBase) {
        if (_usuallyTrue(_uniqueStrings && s.size >= kNarrow && s.size <= kMaxSharedStringSize))
            _strings.insert(s, uint32_t(offsetInBase));
    }

    void Encoder::writeData(slice s) {
        writeData(kBinaryTag, s);
    }


    void Encoder::reuseBaseStrings() {
        reuseBaseStrings(Value::fromTrustedData(_base));
    }

    void Encoder::reuseBaseStrings(const Value *value) {
        if (value < _baseCutoff)
            return;
        switch (value->tag()) {
            case kStringTag:
                cacheString(value->asString(), (size_t)value - (ssize_t)_base.buf);
                break;
            case kArrayTag:
                for (Array::iterator iter(value->asArray()); iter; ++iter)
                    reuseBaseStrings(iter.value());
                break;
            case kDictTag:
                for (Dict::iterator iter(value->asDict()); iter; ++iter) {
                    reuseBaseStrings(iter.key());
                    reuseBaseStrings(iter.value());
                }
                break;
            default:
                break;
        }
    }


    void Encoder::writeDateString(int64_t timestamp, bool asUTC) {
        char str[kFormattedISO8601DateMaxSize];
        writeString(FormatISO8601Date(str, timestamp, asUTC));
    }


#pragma mark - WRITING VALUES:


    bool Encoder::isNarrowValue(const Value *value) {
        if (value->tag() >= kArrayTag)
            return value->countIsZero();
        else
            return value->dataSize() <= kNarrow;
    }


    // Returns the minimum address used by the given Value (transitively).
    // If that minimum address comes before _baseCutoff, immediately returns null.
    const Value* Encoder::minUsed(const Value *value) {
        if (value < _baseCutoff)
            return nullptr;
        switch (value->type()) {
        case kArray: {
            const Value *minVal = value;
            for (Array::iterator i((const Array*)value); i; ++i) {
                minVal = std::min(minVal, minUsed(i.value()));
                if (minVal == nullptr)
                    break;
            }
            return minVal;
        }
        case kDict: {
            const Value *minVal = value;
            for (Dict::iterator i((const Dict*)value, false); i; ++i) {
                minVal = std::min(minVal, minUsed(i.key()));
                minVal = std::min(minVal, minUsed(i.value()));
                if (minVal == nullptr)
                    break;
            }
            return minVal;
        }
        default:
            return value;
        }
    }


    void Encoder::writeValue(const Value *value,
                             const SharedKeys* &sk,
                             const WriteValueFunc *writeNestedValue)
    {
        if (valueIsInBase(value) && !isNarrowValue(value)) {
            auto minVal = minUsed(value);
            if (minVal >= _baseCutoff) {
                // Value is in the base data, and close enough; I can just emit a pointer to it:
                writePointer( (ssize_t)value - (ssize_t)_base.end() );
                if (minVal && minVal < _baseMinUsed)
                    _baseMinUsed = minVal;
                return;
            }
        }
        switch (value->tag()) {
            case kShortIntTag:
            case kIntTag:
            case kFloatTag:
            case kSpecialTag: {
                size_t size = value->dataSize();
                memcpy(placeValue<true>(size), value, size);
                break;
            }
            case kStringTag:
                writeString(value->asString());
                break;
            case kBinaryTag:
                writeData(value->asData());
                break;
            case kArrayTag: {
                ++_copyingCollection;
                auto iter = value->asArray()->begin();
                beginArray(iter.count());
                for (; iter; ++iter) {
                    if (!writeNestedValue || !(*writeNestedValue)(nullptr, iter.value()))
                        writeValue(iter.value(), sk, writeNestedValue);
                }
                endArray();
                --_copyingCollection;
                break;
            }
            case kDictTag: {
                ++_copyingCollection;
                auto dict = (const Dict*)value;
                if (dict->isMutable()) {
                    dict->heapDict()->writeTo(*this/*, writeNestedValue*/);
                } else {
                    auto iter = dict->begin();
                    beginDictionary(iter.count());
                    for (; iter; ++iter) {
                        if (!writeNestedValue || !(*writeNestedValue)(iter.key(), iter.value())) {
                            if (!sk && iter.key()->isInteger())
                                sk = value->sharedKeys();
                            writeKey(iter.key(), sk);
                            writeValue(iter.value(), sk, writeNestedValue);
                        }
                    }
                    endDictionary();
                }
                --_copyingCollection;
                break;
            }
            default:
                FleeceException::_throw(UnknownValue, "illegal tag in Value; corrupt data?");
        }
    }


    void Encoder::writeValue(const Value* NONNULL value, const WriteValueFunc *fn) {
        const SharedKeys *sk = nullptr;
        writeValue(value, sk, fn);
    }



#pragma mark - POINTERS:


    // Pointers are added here as absolute positions in the stream, then fixed up before they're
    // written to the output. This is because we don't know yet what the position of the pointer
    // itself will be.
    // When there's a base (i.e. we're writing a delta), we calculate the absolute position as
    // being from the start of the base data. That way we can represent pointers back into the base
    // without having to write negative numbers as positions.

    bool Encoder::valueIsInBase(const Value *value) const {
        return _base && value >= _base.buf && value < (const void*)_base.end();
    }

    // Parameter p is an offset into the current stream, not taking into account the base.
    void Encoder::writePointer(ssize_t p)   {
        new (placeItem()) Pointer(_base.size + p, kWide);
    }

    // Check whether any pointers in _items can't fit in a narrow Value:
    void Encoder::checkPointerWidths(valueArray *items, size_t pointerOrigin) {
        if (!items->wide) {
            for (Value &v : *items) {
                if (v.isPointer()) {
                    ssize_t pos = v._asPointer()->offset<true>() - _base.size;
                    if (pointerOrigin - pos > Pointer::kMaxNarrowOffset) {
                        items->wide = true;
                        break;
                    }
                }
                pointerOrigin += kNarrow;
            }
        }
    }

    // Convert absolute offsets to relative in _items:
    void Encoder::fixPointers(valueArray *items) {
        size_t pointerOrigin = nextWritePos();
        int width = items->wide ? kWide : kNarrow;
        for (Value &v : *items) {
            if (v.isPointer()) {
                ssize_t pos = v._asPointer()->offset<true>() - _base.size;
                assert(pos < (ssize_t)pointerOrigin);
                bool isExternal = (pos < 0);
                v = Pointer(pointerOrigin - pos, width, isExternal && _markExternPtrs);
            }
            pointerOrigin += width;
        }
    }

#pragma mark - ARRAYS / DICTIONARIES:

    void Encoder::addingKey() {
        if (_usuallyFalse(!_blockedOnKey)) {
            if (_items->tag == kDictTag)
                FleeceException::_throw(EncodeError, "need a value after a key");
            else
                FleeceException::_throw(EncodeError, "not writing a dictionary");
        }
        _blockedOnKey = false;
    }

    void Encoder::writeKey(slice s) {
        int encoded;
        if (_sharedKeys && _sharedKeys->encodeAndAdd(s, encoded)) {
            writeKey(encoded);
            return;
        }
        addingKey();
        const void* writtenKey = _writeString(s);
        if (!writtenKey && _copyingCollection)
            writtenKey = s.buf;         // Workaround for written strings not being kept in memory by the Writer if it's writing to a file
        addedKey({writtenKey, s.size});
    }

    void Encoder::writeKey(int n) {
        assert_precondition(_sharedKeys || n == Dict::kMagicParentKey || gDisableNecessarySharedKeysCheck);
        addingKey();
        writeInt(n);
        addedKey(nullslice);
    }

    void Encoder::writeKey(const Value *key, const SharedKeys *sk) {
        if (key->isInteger()) {
            int intKey = (int)key->asInt();
            if (!sk) {
                sk = key->sharedKeys();
                throwIf(!sk, EncodeError, "Numeric key given without SharedKeys");
            }
            if (sk == _sharedKeys) {
                if (sk->isUnknownKey(intKey)) {
                    throwIf(!sk->decode(intKey), InvalidData, "Unrecognized integer key");
                }
                writeKey(intKey);
            } else {
                slice keySlice = sk->decode(intKey);
                throwIf(!keySlice, InvalidData, "Unrecognized integer key");
                writeKey(keySlice);
            }
        } else {
            slice str = key->asString();
            throwIf(!str, InvalidData, "Key must be a string or integer");
            int encoded;
            if (_sharedKeys && _sharedKeys->encodeAndAdd(str, encoded)) {
                writeKey(encoded);
            } else {
                addingKey();
                writeValue(key, nullptr);
                addedKey(str);
            }
        }
    }

    void Encoder::writeKey(key_t key) {
        if (key.shared())
            writeKey(key.asInt());
        else
            writeKey(key.asString());
    }

    void Encoder::addedKey(FLSlice str) {
        // Note: str will be nullslice iff the key is numeric
        _items->keys.push_back(str);
    }

    void Encoder::push(tags tag, size_t reserve) {
        if (_usuallyFalse(_stackDepth == 0))
            reset();                        // I'm being reused after finish(), so initialize
        if (_usuallyFalse(_stackDepth >= _stack.size()))
            _stack.resize(2*_stackDepth);
        _items = &_stack[_stackDepth++];
        _items->reset(tag);
        if (reserve > 0) {
            if (_usuallyTrue(tag == kDictTag)) {
                _items->reserve(2 * reserve);
                _items->keys.reserve(reserve);
            } else {
                _items->reserve(reserve);
            }
        }
    }

    void Encoder::pop() {
        throwIf(_stackDepth <= 1, InternalError, "Encoder stack underflow!");
        --_stackDepth;
        _items = &_stack[_stackDepth - 1];
    }

    void Encoder::beginArray(size_t reserve) {
        push(kArrayTag, reserve);
    }

    void Encoder::beginDictionary(size_t reserve) {
        push(kDictTag, 2*reserve);
        _writingKey = _blockedOnKey = true;
    }

    void Encoder::beginDictionary(const Dict *parent, size_t reserve) {
        throwIf(!valueIsInBase(parent), EncodeError, "parent is not in base");
        beginDictionary(1 + reserve);
        writeKey(Dict::kMagicParentKey);
        writeValue(parent);
    }

    void Encoder::endArray() {
        endCollection(internal::kArrayTag);
    }

    void Encoder::endDictionary() {
        throwIf(!_writingKey, EncodeError, "need a value");
        endCollection(internal::kDictTag);
    }

    void Encoder::endCollection(tags tag) {
        if (_usuallyFalse(_items->tag != tag)) {
            if (_items->tag == kSpecialTag)
                FleeceException::_throw(EncodeError, "endCollection: not in a collection");
            else
                FleeceException::_throw(EncodeError, "ending wrong type of collection");
        }

        // Pop _items off the stack:
        valueArray *items = _items;
        pop();
        _writingKey = _blockedOnKey = false;

        auto nValues = items->size();    // includes keys if this is a dict!
        auto count = (uint32_t)nValues;
        if (_usuallyTrue(count > 0)) {
            if (_usuallyTrue(tag == kDictTag)) {
                count /= 2;
                sortDict(*items);
            }

            // Write the array/dict header to the outer Value:
            size_t bufLen = 2;
            if (count >= kLongArrayCount)
                bufLen += SizeOfVarInt(count - kLongArrayCount);
            uint32_t inlineCount = std::min(count, (uint32_t)kLongArrayCount);
            byte *buf = placeValue<false>(tag, byte(inlineCount >> 8), bufLen);
            buf[1]  = (byte)(inlineCount & 0xFF);
            if (count >= kLongArrayCount)
                PutUVarInt(&buf[2], count - kLongArrayCount);

            checkPointerWidths(items, nextWritePos());
            if (items->wide)
                buf[0] |= 0x08;     // "wide" flag

            fixPointers(items);

            // Write the values:
            if (items->wide) {
                _out.write(&(*items)[0], kWide*nValues);
            } else {
                auto narrow = _out.reserveSpace<uint16_t>(nValues);
                for (auto &v : *items)
                    ::memcpy(narrow++, &v, kNarrow);
            }
        } else {
            byte *buf = placeValue<true>(tag, 0, 2);
            buf[1] = 0;
        }

#ifndef NDEBUG
        if (items->wide) {
            _numWide++;
            _wideCount += count;
        } else {
            _numNarrow++;
            _narrowCount += count;
        }
#endif

        items->clear();
    }

    // compares dictionary keys as slices. If a slice has a null `buf`, it represents an integer
    // key, whose value is in the `size` field.
    static inline int compareKeysByIndex(const FLSlice *sa, const FLSlice *sb) {
        if (sa->buf) {
            if (sb->buf)
                return FLSlice_Compare(*sa, *sb) < 0;                // string key comparison
            else
                return false;
        } else {
            if (sb->buf)
                return true;
            else
                return (int)sa->size < (int)sb->size;       // integer key comparison
        }
    }

    void Encoder::sortDict(valueArray &items) {
        auto &keys = items.keys;
        size_t n = keys.size();
        if (n < 2)
            return;

        // Fill in the pointers of any keys that refer to inline strings:
        for (unsigned i = 0; i < n; i++) {
            if (keys[i].buf == nullptr) {
                const Value *item = &items[2*i];
                if (item->tag() == kStringTag) {
                    keys[i].buf = offsetby(item, 1);                    // inline string
                } else {
                    assert(item->tag() == kShortIntTag);
                    keys[i] = {nullptr, (size_t)item->asUnsigned()};    // integer
                }
            }
        }

        // Construct an array that describes the permutation of item indices:
        TempArray(indices, const FLSlice*, n);
        const FLSlice* base = &keys[0];
        for (unsigned i = 0; i < n; i++)
            indices[i] = base + i;
        std::sort(&indices[0], &indices[n], &compareKeysByIndex);
        // indices[i] is now a pointer to the Value that should go at index i

        // Now rewrite items according to the permutation in indices:
        TempArray(oldBuf, char, 2*n * sizeof(Value));
        auto old = (Value*)oldBuf;
        memcpy(old, &items[0], 2*n * sizeof(Value));
        for (size_t i = 0; i < n; i++) {
            auto j = indices[i] - base;
            if ((ssize_t)i != j) {
                items[2*i]   = old[2*j];
                items[2*i+1] = old[2*j+1];
            }
        }
    }

} }
