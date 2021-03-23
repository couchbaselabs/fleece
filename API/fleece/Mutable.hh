//
// Mutable.hh
//
// Copyright © 2018 Couchbase. All rights reserved.
//

#pragma once
#ifndef _FLEECE_MUTABLE_HH
#define _FLEECE_MUTABLE_HH
#include "Fleece.hh"

namespace fleece {

    class Slot {
    public:
        void setNull()                              {FLSlot_SetNull(_slot);}
        void operator= (Null)                       {FLSlot_SetNull(_slot);}
        void operator= (bool v)                     {FLSlot_SetBool(_slot, v);}
        void operator= (int v)                      {FLSlot_SetInt(_slot, v);}
        void operator= (unsigned v)                 {FLSlot_SetUInt(_slot, v);}
        void operator= (int64_t v)                  {FLSlot_SetInt(_slot, v);}
        void operator= (uint64_t v)                 {FLSlot_SetUInt(_slot, v);}
        void operator= (float v)                    {FLSlot_SetFloat(_slot, v);}
        void operator= (double v)                   {FLSlot_SetDouble(_slot, v);}
        void operator= (slice v)                    {FLSlot_SetString(_slot, v);}
        void operator= (const char *v)              {FLSlot_SetString(_slot, slice(v));}
        void operator= (const std::string &v)       {FLSlot_SetString(_slot, slice(v));}
        void setData(slice v)                       {FLSlot_SetData(_slot, v);}
        void operator= (Value v)                    {FLSlot_SetValue(_slot, v);}
        void operator= (std::nullptr_t)             {FLSlot_SetValue(_slot, nullptr);}

        operator FLSlot()                           {return _slot;}

    private:
        friend class MutableArray;
        friend class MutableDict;

        Slot(FLSlot slot)                           :_slot(slot) { }
        Slot(Slot&& slot) noexcept                  :_slot(slot._slot) { }
        Slot(const Slot&) =delete;
        Slot& operator=(const Slot&) =delete;
        Slot& operator=(Slot&&) =delete;

        void operator= (const void*) = delete; // Explicitly disallow other pointer types!

        FLSlot const _slot;
    };


    template <class Collection, class Key>
    class keyref : public Value {
    public:
        keyref(Collection &coll, Key key)           :Value(coll.get(key)), _coll(coll), _key(key) { }
            template <class T>
        void operator= (const keyref &ref)          {_coll.set(_key, ref);}
            template <class T>
        void operator= (const T &value)             {_coll.set(_key, value);}
        void setData(slice value)                   {_coll.set(_key).setData(value);}
        void remove()                               {_coll.remove(_key);}

        operator FLSlot()                           {return _coll.set(_key);}
    private:
        Collection _coll;
        Key _key;
    };


    /** A mutable form of Array. Its storage lives in the heap, not in the (immutable) Fleece
        document. It can be used to make a changed form of a document, which can then be
        encoded to a new Fleece document. */
    class MutableArray : public Array {
    public:
        /** Creates a new, empty mutable array. */
        static MutableArray newArray()          {return MutableArray(FLMutableArray_New(), false);}

        MutableArray()                          :Array() { }
        MutableArray(FLMutableArray a)          :Array((FLArray)FLMutableArray_Retain(a)) { }
        MutableArray(const MutableArray &a)     :Array((FLArray)FLMutableArray_Retain(a)) { }
        MutableArray(MutableArray &&a) noexcept :Array((FLArray)a) {a._val = nullptr;}
        ~MutableArray()                         {FLMutableArray_Release(*this);}

        operator FLMutableArray () const        {return (FLMutableArray)_val;}

        MutableArray& operator= (const MutableArray &a) {
            FLMutableArray_Retain(a);
            FLMutableArray_Release(*this);
            _val = a._val;
            return *this;
        }

        MutableArray& operator= (MutableArray &&a) noexcept {
            if (a._val != _val) {
                FLMutableArray_Release(*this);
                _val = a._val;
                a._val = nullptr;
            }
            return *this;
        }

        /** The immutable Array this instance was constructed from (if any). */
        Array source() const                    {return FLMutableArray_GetSource(*this);}

        /** True if this array has been modified since it was created. */
        bool isChanged() const                  {return FLMutableArray_IsChanged(*this);}

        /** Removes a range of values from the array. */
        void remove(uint32_t first, uint32_t n =1) {FLMutableArray_Remove(*this, first, n);}

        /** Sets the array's size. If the array grows, new values begin as nulls. */
        void resize(uint32_t size)              {FLMutableArray_Resize(*this, size);}

        Slot set(uint32_t i)                    {return Slot(FLMutableArray_Set(*this, i));}
        void setNull(uint32_t i)                {set(i).setNull();}

        template <class T>
        void set(uint32_t i, T v)               {set(i) = v;}

        Slot append()                           {return FLMutableArray_Append(*this);}
        void appendNull()                       {append().setNull();}

        template <class T>
        void append(T v)                        {append() = v;}

        void insertNulls(uint32_t i, uint32_t n) {FLMutableArray_Insert(*this, i, n);}

        // This enables e.g. `array[10] = 17`
        inline keyref<MutableArray,uint32_t> operator[] (int i) {
            return keyref<MutableArray,uint32_t>(*this, i);
        }

        inline Value operator[] (int index) const {return get(index);} // const version


        inline MutableArray getMutableArray(uint32_t i);
        inline MutableDict getMutableDict(uint32_t i);

    private:
        MutableArray(FLMutableArray a, bool)     :Array((FLArray)a) {}
        friend class RetainedValue;
        friend class Array;
    };


    /** A mutable form of Dict. Its storage lives in the heap, not in the (immutable) Fleece
        document. It can be used to make a changed form of a document, which can then be
        encoded to a new Fleece document. */
    class MutableDict : public Dict {
    public:
        static MutableDict newDict()            {return MutableDict(FLMutableDict_New(), false);}

        MutableDict()                           :Dict() { }
        MutableDict(FLMutableDict d)            :Dict((FLDict)d) {FLMutableDict_Retain(*this);}
        MutableDict(const MutableDict &d)       :Dict((FLDict)d) {FLMutableDict_Retain(*this);}
        MutableDict(MutableDict &&d) noexcept   :Dict((FLDict)d) {d._val = nullptr;}
        ~MutableDict()                          {FLMutableDict_Release(*this);}

        operator FLMutableDict () const         {return (FLMutableDict)_val;}

        MutableDict& operator= (const MutableDict &d) {
            FLMutableDict_Retain(d);
            FLMutableDict_Release(*this);
            _val = d._val;
            return *this;
        }

        MutableDict& operator= (MutableDict &&d) noexcept {
            if (d._val != _val) {
                FLMutableDict_Release(*this);
                _val = d._val;
                d._val = nullptr;
            }
            return *this;
        }

        Dict source() const                     {return FLMutableDict_GetSource(*this);}
        bool isChanged() const                  {return FLMutableDict_IsChanged(*this);}

        void remove(slice key)                  {FLMutableDict_Remove(*this, key);}

        Slot set(slice key)                     {return FLMutableDict_Set(*this, key);}
        void setNull(slice key)                 {set(key) = nullValue;}

        template <class T>
        void set(slice key, T v)                {set(key) = v;}


        // This enables e.g. `dict["key"_sl] = 17`
        inline keyref<MutableDict,slice> operator[] (slice key)
            {return keyref<MutableDict,slice>(*this, key);}
        inline keyref<MutableDict,slice> operator[] (const char *key)
            {return keyref<MutableDict,slice>(*this, slice(key));}
        inline keyref<MutableDict,Key&> operator[] (Key &key)
            {return keyref<MutableDict,Key&>(*this, key);}

        inline Value operator[] (slice key) const       {return Dict::get(key);}
        inline Value operator[] (const char *key) const {return Dict::get(key);}

        inline MutableArray getMutableArray(slice key);
        inline MutableDict getMutableDict(slice key);

    private:
        MutableDict(FLMutableDict d, bool)      :Dict((FLDict)d) {}
        friend class RetainedValue;
        friend class Dict;
    };


    /** Equivalent to Value except that, if it holds a MutableArray/Dict, it will retain the
        reference so it won't be freed. */
    class RetainedValue : public Value {
    public:
        RetainedValue()                           =default;
        RetainedValue(FLValue v)                  :Value(FLValue_Retain(v)) { }
        RetainedValue(const Value &v)             :Value(FLValue_Retain(v)) { }
        RetainedValue(RetainedValue &&v) noexcept :Value(v) {v._val = nullptr;}
        RetainedValue(MutableArray &&v) noexcept  :Value(v) {v._val = nullptr;}
        RetainedValue(MutableDict &&v) noexcept   :Value(v) {v._val = nullptr;}
        ~RetainedValue()                          {FLValue_Release(_val);}

        RetainedValue& operator= (const Value &v) {
            FLValue_Retain(v);
            FLValue_Release(_val);
            _val = v;
            return *this;
        }

        RetainedValue& operator= (RetainedValue &&v) noexcept {
            if (v._val != _val) {
                FLValue_Release(_val);
                _val = v._val;
                v._val = nullptr;
            }
            return *this;
        }

        RetainedValue& operator= (std::nullptr_t) {      // disambiguation
            FLValue_Release(_val);
            _val = nullptr;
            return *this;
        }
    };



    //////// IMPLEMENTATION GUNK:

    inline MutableArray Array::mutableCopy(FLCopyFlags flags) const {
        return MutableArray(FLArray_MutableCopy(*this, flags), false);
    }
    inline MutableDict Dict::mutableCopy(FLCopyFlags flags) const {
        return MutableDict(FLDict_MutableCopy(*this, flags), false);
    }

    inline MutableArray MutableArray::getMutableArray(uint32_t i)
                                                {return FLMutableArray_GetMutableArray(*this, i);}
    inline MutableDict MutableArray::getMutableDict(uint32_t i)
                                                {return FLMutableArray_GetMutableDict(*this, i);}
    inline MutableArray MutableDict::getMutableArray(slice key)
                                                {return FLMutableDict_GetMutableArray(*this, key);}
    inline MutableDict MutableDict::getMutableDict(slice key)
                                                {return FLMutableDict_GetMutableDict(*this, key);}

    inline MutableArray Array::asMutable() const {
        return MutableArray(FLArray_AsMutable(*this));
    }

    inline MutableDict Dict::asMutable() const {
        return MutableDict(FLDict_AsMutable(*this));
    }

}

#endif // _FLEECE_MUTABLE_HH
