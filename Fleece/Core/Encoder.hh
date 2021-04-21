//
// Encoder.hh
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

#include "Value.hh"
#include "Writer.hh"
#include "Doc.hh"
#include "StringTable.hh"
#include "SmallVector.hh"
#include "function_ref.hh"


namespace fleece { namespace impl {
    class SharedKeys;
    class key_t;


    /** Generates Fleece-encoded data. */
    class Encoder {
    public:
        /** Constructs an encoder. */
        Encoder(size_t reserveOutputSize =256);
        Encoder(FILE* NONNULL);
        ~Encoder();

        /** Sets the uniqueStrings property. If true (the default), the encoder tries to write
            each unique string only once. This saves space but makes the encoder slightly slower. */
        void uniqueStrings(bool b)      {_uniqueStrings = b;}

        /** Sets the base Fleece data that the encoded data will be (logically) appended to.
            Any writeValue() calls whose Value points into the base data will be written as
            pointers.
            @param base  The base Fleece document that's being appended to.
            @param markExternPointers  If true, pointers into the base document (i.e. out of the
                        encoded data) will be marked with the `extern` flag. The resulting Fleece
                        document must then be opened as a Doc using the `externData` property
                        pointing to wherever a copy of the base document is.
            @param cutoff  If nonzero, this specifies the maximum number of bytes of the base
                        (starting from the end) that should be used. Any base data before
                        the cutoff will not be referenced in the encoder output. */
        void setBase(slice base, bool markExternPointers =false, size_t cutoff =0);

        /** Scans the base document for strings and adds them to the encoder's string table.
            If equivalent strings are written to the encoder they will then be encoded as pointers
            to the existing strings. */
        void reuseBaseStrings();

        bool valueIsInBase(const Value *value) const;

        bool isEmpty() const            {return _out.length() == 0 && _stackDepth == 1 && _items->empty();}
        size_t bytesWritten() const     {return _out.length();} // may be an underestimate

        /** Ends encoding, writing the last of the data to the Writer. */
        void end();

        /** Returns the encoded data. This implicitly calls end(). */
        alloc_slice finish();

        /** Returns the encoded data as a Doc. This implicitly calls end(). */
        Retained<Doc> finishDoc();

        /** Resets the encoder so it can be used again. */
        void reset();

        /////// Writing data:

        void writeNull();
        void writeUndefined();
        void writeBool(bool);

        void writeInt(int64_t i);
        void writeUInt(uint64_t i);
        void writeFloat(float);
        void writeDouble(double);

        void writeString(slice s)                           {(void)_writeString(s);}

        void writeDateString(int64_t timestamp, bool asUTC =true);

        void writeData(slice s);

        void writeValue(const Value* NONNULL v)             {writeValue(v, nullptr);}

        using WriteValueFunc = function_ref<bool(const Value *key, const Value *value)>;

        /** Alternative writeValue that invokes a callback before writing any Value.
            If the callback returns false, the value is written as usual, otherwise it's skipped;
            the callback can invoke the Encoder to write a different Value instead if it likes. */
        void writeValue(const Value* NONNULL v, WriteValueFunc fn)  {writeValue(v, &fn);}

#ifdef __OBJC__
        /** Writes an Objective-C object. Supported classes are the ones allowed by
            NSJSONSerialization, as well as NSData. */
        void writeObjC(id);
#endif

        //////// Writing arrays:

        /** Begins creating an array. Until endArray is called, values written to the encoder are
            added to this array.
            @param reserve  If nonzero, space is preallocated for this many values. This has no
                            effect on the output but can speed up encoding slightly. */
        void beginArray(size_t reserve =0);

        /** Ends creating an array. The array is written to the output and added as a Value to
            the next outermost collection (or made the root if there is no collection active.) */
        void endArray();

        //////// Writing dictionaries:

        /** Begins creating a dictionary. Until endDict is called, values written to the encoder
            are added to this dictionary.
            While creating a dictionary, writeKey must be called before every value.
            @param reserve  If nonzero, space is preallocated for this many values. This has no
                            effect on the output but can speed up encoding slightly. */
        void beginDictionary(size_t reserve =0);

        /** Begins creating a dictionary which inherits from an existing dictionary. */
        void beginDictionary(const Dict *parent NONNULL, size_t reserve =0);

        /** Ends creating a dictionary. The dict is written to the output and added as a value to
            the next outermost collection (or made the root if there is no collection active.) */
        void endDictionary();

        /** Writes a key to the current dictionary. This must be called before adding a value. */
        void writeKey(slice);

        /** Writes a string or int Value as a key to the current dictionary. */
        void writeKey(const Value* NONNULL, const SharedKeys* =nullptr);

        void writeKey(key_t);

        /** Associates a SharedKeys object with this Encoder. The writeKey() methods that take
            strings will consult this object to possibly map the key to an integer. */
        void setSharedKeys(SharedKeys *s);

        //////// "<<" convenience operators;

        // Note: overriding <<(bool) would be dangerous due to implicit conversion
        Encoder& operator<< (Null)              {writeNull(); return *this;}
        Encoder& operator<< (long long i)       {writeInt(i); return *this;}
        Encoder& operator<< (unsigned long long i)  {writeUInt(i); return *this;}
        Encoder& operator<< (long i)            {writeInt(i); return *this;}
        Encoder& operator<< (unsigned long i)   {writeUInt(i); return *this;}
        Encoder& operator<< (int i)             {writeInt(i); return *this;}
        Encoder& operator<< (unsigned int i)    {writeUInt(i); return *this;}
        Encoder& operator<< (double d)          {writeDouble(d); return *this;}
        Encoder& operator<< (float f)           {writeFloat(f); return *this;}
        Encoder& operator<< (slice s)           {writeString(s); return *this;} // string not data!
        Encoder& operator<< (const Value *v NONNULL)    {writeValue(v); return *this;}

        //////// Pre-encoded scalar values for convenience:

        static const slice kPreEncodedTrue, kPreEncodedFalse, kPreEncodedNull, kPreEncodedEmptyDict;

        // For advanced use cases only... be careful!
        void suppressTrailer()                  {_trailer = false;}
        void writeRaw(slice s)                  {_out.write(s);}
        size_t nextWritePos();
        size_t finishItem();
        slice base() const                      {return _base;}
        slice baseUsed() const                  {return _baseMinUsed != 0 ? slice(_baseMinUsed, _base.end()) : slice();}
        const StringTable& strings() const      {return _strings;}

        enum class PreWrittenValue : ssize_t { none = 0 };
        PreWrittenValue lastValueWritten() const;   // Opaque reference to last thing written
        void writeValueAgain(PreWrittenValue);         // Writes pointer to an already-written value

        /** Returns the data written so far as a standalone Fleece document, whose root is the last
            value written. You can continue writing, and the final output returned by \ref finish will
            consist of everything after this point. It can be used in the future by loading it with the
            first part as its `extern` reference. */
        alloc_slice snip();

#if 0
        static bool isIntRepresentable(float n) noexcept;
        static bool isIntRepresentable(double n) noexcept;
#endif
        static bool isFloatRepresentable(double n) noexcept;

    private:
        using byte = uint8_t;

        static constexpr size_t kInitialStackSize = 4;
        static constexpr size_t kInitialCollectionCapacity = 16;

        // Stores the pending values to be written to an in-progress array/dict
        class valueArray : public smallVector<Value, kInitialCollectionCapacity> {
        public:
            valueArray()                    =default;
            void reset(internal::tags t)    {tag = t; wide = false; keys.clear();}
            
            internal::tags tag;
            bool wide;
            smallVector<FLSlice, kInitialCollectionCapacity> keys;
        };

        void init();
        void resetStack();
        byte* placeItem();
        void addSpecial(int specialValue);
        template <bool canInline> byte* placeValue(size_t size);
        template <bool canInline> byte* placeValue(internal::tags tag, byte param, size_t size);
        void reuseBaseStrings(const Value* NONNULL);
        void cacheString(slice s, size_t offsetInBase);
        static bool isNarrowValue(const Value *value NONNULL);
        void writePointer(ssize_t pos);
        void writeSpecial(uint8_t special);
        void writeInt(uint64_t i, bool isShort, bool isUnsigned);
        void _writeFloat(float);
        const void* writeData(internal::tags, slice s);
        const void* _writeString(slice);
        void addingKey();
        void addedKey(FLSlice str);
        void sortDict(valueArray &items);
        void checkPointerWidths(valueArray *items NONNULL, size_t writePos);
        void fixPointers(valueArray *items NONNULL);
        void endCollection(internal::tags tag);
        void push(internal::tags tag, size_t reserve);
        inline void pop();
        void writeKey(int);
        void writeValue(const Value* NONNULL, const WriteValueFunc*);
        void writeValue(const Value* NONNULL, const SharedKeys* &, const WriteValueFunc*);
        const Value* minUsed(const Value *value);

        Encoder(const Encoder&) = delete;
        Encoder& operator=(const Encoder&) = delete;

        //////// Data members:

        static constexpr size_t kInitialStringTableSize = 32;

        Writer _out;                 // Where output is written to
        valueArray *_items;          // Values of currently-open array/dict; == &_stack[_stackDepth-1]
        smallVector<valueArray, kInitialStackSize> _stack; // Stack of open arrays/dicts
        unsigned _stackDepth;        // Current depth of _stack
        PreallocatedStringTable<kInitialStringTableSize> _strings; // Maps strings to the offsets where they appear as values
        Writer _stringStorage;       // Backing store for strings in _strings
        bool _uniqueStrings {true};  // Should strings be uniqued before writing?
        Retained<SharedKeys> _sharedKeys;  // Client-provided key-to-int mapping
        slice _base;                 // Base Fleece data being appended to (if any)
        alloc_slice _ownedBase;      // If I allocated _base, it's stored here too to retain it
        const void* _baseCutoff {0}; // Lowest addr in _base that I can write a ptr to
        const void* _baseMinUsed {0};// Lowest addr in _base I've written a ptr to
        int _copyingCollection {0};  // Nonzero inside writeValue when writing array/dict
        bool _writingKey    {false}; // True if Value being written is a key
        bool _blockedOnKey  {false}; // True if writes should be refused
        bool _trailer       {true};  // Write standard trailer at end?
        bool _markExternPtrs{false}; // Mark pointers outside encoded data as 'extern'

        friend class EncoderTests;
#ifndef NDEBUG
    public: // Statistics for use in tests
        unsigned _numNarrow {0}, _numWide {0}, _narrowCount {0}, _wideCount {0},
                 _numSavedStrings {0};
#endif
    };

} }
