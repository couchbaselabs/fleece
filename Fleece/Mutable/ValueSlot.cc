//
// ValueSlot.cc
//
// Copyright © 2018 Couchbase. All rights reserved.
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

#include "ValueSlot.hh"
#include "HeapArray.hh"
#include "HeapDict.hh"
#include "Encoder.hh"
#include "varint.hh"
#include <algorithm>

namespace fleece { namespace impl {
    using namespace std;
    using namespace internal;


    static_assert(sizeof(ValueSlot) == 8);


    ValueSlot::ValueSlot()
    :_pointer(0)
    { }


    ValueSlot::ValueSlot(Null)
    :_pointerTag(0xFF)
    {
        _inlineVal[0] = ((kSpecialTag << 4) | kSpecialValueNull);
    }


    ValueSlot::ValueSlot(HeapCollection *md)
    :_pointer( uint64_t(retain(md)->asValue()) )
    { }


    ValueSlot::ValueSlot(const ValueSlot &other) noexcept
    :_pointer(other._pointer)
    {
        if (isPointer())
            retain(pointer());
    }


    ValueSlot& ValueSlot::operator= (const ValueSlot &other) noexcept {
        releaseValue();
        _pointer = other._pointer;
        if (isPointer())
            retain(pointer());
        return *this;
    }


    ValueSlot::ValueSlot(ValueSlot &&other) noexcept {
        _pointer = other._pointer;
        other._pointer = 0;
    }


    ValueSlot& ValueSlot::operator= (ValueSlot &&other) noexcept {
        release(asPointer());
        _pointer = other._pointer;
        other._pointer = 0;
        return *this;
    }



    ValueSlot::~ValueSlot() {
        if (isPointer())
            release(pointer());
    }


    void ValueSlot::releaseValue() {
        if (isPointer()) {
            release(pointer());
            _pointer = 0;
        }
    }


    const Value* ValueSlot::asValueOrUndefined() const {
        return _pointer ? asValue() : Value::kUndefinedValue;
    }


    void ValueSlot::setPointer(const Value *v) {
        precondition(v != nullptr);
        if (_usuallyFalse(v == pointer()))
            return;
        releaseValue();
        _pointer = uint64_t(size_t(retain(v)));
        assert(isPointer());
    }


    void ValueSlot::setInline(internal::tags valueTag, int tiny) {
        releaseValue();
        _pointerTag = 0xFF;
        _inlineVal[0] = uint8_t((valueTag << 4) | tiny);
    }

    void ValueSlot::set(Null) {
        setInline(kSpecialTag, kSpecialValueNull);
    }


    void ValueSlot::set(bool b) {
        setInline(kSpecialTag, b ? kSpecialValueTrue : kSpecialValueFalse);
    }


    void ValueSlot::set(int i)       {setInt(i);}
    void ValueSlot::set(unsigned i)  {setInt(i);}
    void ValueSlot::set(int64_t i)   {setInt(i);}
    void ValueSlot::set(uint64_t i)  {setInt(i);}


    template <class INT>
    void ValueSlot::setInt(INT i) {
        // In the following, int64_t(i) is to avoid compiler warning against comparision between unsigned and signed.
        // If INT is unsigned, int64_t(i) won't be executed, that is, we won't actually convert uint64 to int64.
        // We also assume the largest int representation is 64-bit.
        if (i < 2048 && (!numeric_limits<INT>::is_signed || int64_t(i) > -2048)) {
            setInline(kShortIntTag, (i >> 8) & 0x0F);
            _inlineVal[1] = (uint8_t)(i & 0xFF);
        } else {
            uint8_t buf[8];
            auto size = PutIntOfLength(buf, i, !numeric_limits<INT>::is_signed);
            setValue(kIntTag,
                     (int)(size-1) | (numeric_limits<INT>::is_signed ? 0 : 0x08),
                     {buf, size});
        }
    }


    void ValueSlot::set(float f) {
#if 0 // Perhaps an option in the future?
        if (Encoder::isIntRepresentable(f)) {
            set((int32_t)f);
        } else
#endif
        {
            struct {
                uint8_t filler = 0;
                endian::littleEndianFloat le;
            } data;
            data.le = f;
            setValue(kFloatTag, 0, {(char*)&data.le - 1, sizeof(data.le) + 1});
        }
        assert_postcondition(asValue()->asFloat() == f);
    }

    void ValueSlot::set(double d) {
#if 0 // Perhaps an option in the future?
        if (Encoder::isIntRepresentable(d)) {
            set((int64_t)d);
        } else
#endif
        if (Encoder::isFloatRepresentable(d)) {
            set((float)d);
        } else {
            setPointer(HeapValue::create(d)->asValue());
        }
        assert_postcondition(asValue()->asDouble() == d);
    }


    void ValueSlot::setValue(const Value *v) {
        if (v && v->tag() < kArrayTag) {
            auto size = v->dataSize();
            if (size <= kInlineCapacity) {
                // Copy value inline if it's small enough
                releaseValue();
                _pointerTag = 0xFF;
                memcpy(&_inlineVal, v, size);
                return;
            }
        }
        // else point to it
        setPointer(v);
    }


    void ValueSlot::setValue(tags valueTag, int tiny, slice bytes) {
        if (1 + bytes.size <= kInlineCapacity) {
            setInline(valueTag, tiny);
            memcpy(&_inlineVal[1], bytes.buf, bytes.size);
        } else {
            setPointer(HeapValue::create(valueTag, tiny, bytes)->asValue());
        }
    }


    void ValueSlot::setStringOrData(tags valueTag, slice s) {
        if (s.size + 1 <= kInlineCapacity) {
            // Short strings can go inline:
            setInline(valueTag, (int)s.size);
            memcpy(&_inlineVal[1], s.buf, s.size);
        } else {
            setPointer(HeapValue::createStr(valueTag, s)->asValue());
        }
    }


    HeapCollection* ValueSlot::asMutableCollection() const {
        const Value *ptr = asPointer();
        if (ptr && ptr->isMutable())
            return (HeapCollection*)HeapValue::asHeapValue(ptr);
        return nullptr;
    }


    HeapCollection* ValueSlot::makeMutable(tags ifType) {
        if (isInline())
            return nullptr;
        Retained<HeapCollection> mval = HeapCollection::mutableCopy(pointer(), ifType);
        if (mval)
            set(mval->asValue());
        return mval;
    }


    void ValueSlot::copyValue(CopyFlags flags) {
        const Value *value = asPointer();
        if (value && ((flags & kCopyImmutables) || value->isMutable())) {
            bool recurse = (flags & kDeepCopy);
            Retained<HeapCollection> copy;
            switch (value->tag()) {
                case kArrayTag:
                    copy = new HeapArray((Array*)value);
                    if (recurse)
                        ((HeapArray*)copy.get())->copyChildren(flags);
                    set(copy->asValue());
                    break;
                case kDictTag:
                    copy = new HeapDict((Dict*)value);
                    if (recurse)
                        ((HeapDict*)copy.get())->copyChildren(flags);
                    set(copy->asValue());
                    break;
                case kStringTag:
                    set(value->asString());
                    break;
                case kFloatTag:
                    set(value->asDouble());
                    break;
                default:
                    assert(false);
            }
        }
    }

} }
