//
// Fleece.cc
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

#include "Fleece+ImplGlue.hh"
#include "MutableArray.hh"
#include "MutableDict.hh"
#include "JSONDelta.hh"
#include "fleece/Fleece.h"
#include "JSON5.hh"
#include "betterassert.hh"


namespace fleece { namespace impl {

    void recordError(const std::exception &x, FLError *outError) noexcept {
        if (outError)
            *outError = (FLError) FleeceException::getCode(x);
    }

} }


const FLValue kFLNullValue  = Value::kNullValue;
const FLArray kFLEmptyArray = Array::kEmpty;
const FLDict kFLEmptyDict   = Dict::kEmpty;


static FLSliceResult toSliceResult(alloc_slice &&s) {
    s.retain();
    return {(void*)s.buf, s.size};
}


FLValue FLValue_FromData(FLSlice data, FLTrust trust) FLAPI {
    return trust ? Value::fromTrustedData(data) : Value::fromData(data);
}


const char* FLDump(FLValue v) FLAPI {
    FLStringResult json = FLValue_ToJSON(v);
    auto cstr = (char*)malloc(json.size + 1);
    FLMemCpy(cstr, json.buf, json.size);
    cstr[json.size] = 0;
    return cstr;
}

const char* FLDumpData(FLSlice data) FLAPI {
    return FLDump(Value::fromData(data));
}


FLValueType FLValue_GetType(FLValue v) FLAPI {
    if (_usuallyFalse(v == NULL || v->isUndefined()))
        return kFLUndefined;
    else
        return (FLValueType)v->type();
}


bool FLValue_IsInteger(FLValue v)          FLAPI {return v && v->isInteger();}
bool FLValue_IsUnsigned(FLValue v)         FLAPI {return v && v->isUnsigned();}
bool FLValue_IsDouble(FLValue v)           FLAPI {return v && v->isDouble();}
bool FLValue_AsBool(FLValue v)             FLAPI {return v && v->asBool();}
int64_t FLValue_AsInt(FLValue v)           FLAPI {return v ? v->asInt() : 0;}
uint64_t FLValue_AsUnsigned(FLValue v)     FLAPI {return v ? v->asUnsigned() : 0;}
float FLValue_AsFloat(FLValue v)           FLAPI {return v ? v->asFloat() : 0.0f;}
double FLValue_AsDouble(FLValue v)         FLAPI {return v ? v->asDouble() : 0.0;}
FLString FLValue_AsString(FLValue v)       FLAPI {return v ? (FLString)v->asString() : kFLSliceNull;}
FLSlice FLValue_AsData(FLValue v)          FLAPI {return v ? (FLSlice)v->asData() : kFLSliceNull;}
FLArray FLValue_AsArray(FLValue v)         FLAPI {return v ? v->asArray() : nullptr;}
FLDict FLValue_AsDict(FLValue v)           FLAPI {return v ? v->asDict() : nullptr;}
FLTimestamp FLValue_AsTimestamp(FLValue v) FLAPI {return v ? v->asTimestamp() : FLTimestampNone;}
FLValue FLValue_Retain(FLValue v)          FLAPI {return retain(v);}
void FLValue_Release(FLValue v)            FLAPI {release(v);}


FLDoc FLValue_FindDoc(FLValue v) FLAPI {
    return v ? retain(Doc::containing(v).get()) : nullptr;
}

bool FLValue_IsEqual(FLValue v1, FLValue v2) FLAPI {
    if (_usuallyTrue(v1 != nullptr))
        return v1->isEqual(v2);
    else
        return v2 == nullptr;
}

FLSliceResult FLValue_ToString(FLValue v) FLAPI {
    if (v) {
        try {
            return toSliceResult(v->toString());    // toString can throw
        } catchError(nullptr)
    }
    return {nullptr, 0};
}


FLSliceResult FLValue_ToJSONX(FLValue v,
                              bool json5,
                              bool canonical) FLAPI
{
    if (v) {
        try {
            JSONEncoder encoder;
            encoder.setJSON5(json5);
            encoder.setCanonical(canonical);
            encoder.writeValue(v);
            return toSliceResult(encoder.finish());
        } catchError(nullptr)
    }
    return {nullptr, 0};
}

FLSliceResult FLValue_ToJSON(FLValue v)      FLAPI {return FLValue_ToJSONX(v, false, false);}
FLSliceResult FLValue_ToJSON5(FLValue v)     FLAPI {return FLValue_ToJSONX(v, true,  false);}


FLSliceResult FLData_ConvertJSON(FLSlice json, FLError *outError) FLAPI {
    FLEncoderImpl e(kFLEncodeFleece, json.size);
    FLEncoder_ConvertJSON(&e, json);
    return FLEncoder_Finish(&e, outError);
}


FLStringResult FLJSON5_ToJSON(FLString json5,
                              FLStringResult *outErrorMessage, size_t *outErrorPos,
                              FLError *error) FLAPI {
    alloc_slice errorMessage;
    size_t errorPos = 0;
    try {
        std::string json = ConvertJSON5((std::string((char*)json5.buf, json5.size)));
        return toSliceResult(alloc_slice(json));
    } catch (const json5_error &x) {
        errorMessage = alloc_slice(x.what());
        errorPos = x.inputPos;
        if (error)
            *error = kFLJSONError;
    } catch (const std::exception &x) {
        errorMessage = alloc_slice(x.what());
        recordError(x, error);
    }
    if (outErrorMessage)
        *outErrorMessage = toSliceResult(std::move(errorMessage));
    if (outErrorPos)
        *outErrorPos = errorPos;
    return {};
}


FLSliceResult FLData_Dump(FLSlice data) FLAPI {
    try {
        return toSliceResult(alloc_slice(Value::dump(data)));
    } catchError(nullptr)
    return {nullptr, 0};
}


#pragma mark - ARRAYS:


uint32_t FLArray_Count(FLArray a)                    FLAPI {return a ? a->count() : 0;}
bool FLArray_IsEmpty(FLArray a)                      FLAPI {return a ? a->empty() : true;}
FLValue FLArray_Get(FLArray a, uint32_t index)       FLAPI {return a ? a->get(index) : nullptr;}

void FLArrayIterator_Begin(FLArray a, FLArrayIterator* i) FLAPI {
    static_assert(sizeof(FLArrayIterator) >= sizeof(Array::iterator),"FLArrayIterator is too small");
    new (i) Array::iterator(a);
    // Note: this is safe even if a is null.
}

uint32_t FLArrayIterator_GetCount(const FLArrayIterator* i) FLAPI {
    return ((Array::iterator*)i)->count();
}

FLValue FLArrayIterator_GetValue(const FLArrayIterator* i) FLAPI {
    return ((Array::iterator*)i)->value();
}

FLValue FLArrayIterator_GetValueAt(const FLArrayIterator *i, uint32_t offset) FLAPI {
    return (*(Array::iterator*)i)[offset];
}

bool FLArrayIterator_Next(FLArrayIterator* i) FLAPI {
    try {
        auto& iter = *(Array::iterator*)i;
        ++iter;                 // throws if iterating past end
        return (bool)iter;
    } catchError(nullptr)
    return false;
}


static FLMutableArray _newMutableArray(FLArray a, FLCopyFlags flags) noexcept {
    try {
        return (MutableArray*)retain(MutableArray::newArray(a, CopyFlags(flags)).get());
    } catchError(nullptr)
    return nullptr;
}

FLMutableArray FLMutableArray_New(void) FLAPI {
    return _newMutableArray(nullptr, kFLDefaultCopy);
}

FLMutableArray FLArray_MutableCopy(FLArray a, FLCopyFlags flags) FLAPI {
    return a ? _newMutableArray(a, flags) : nullptr;
}

FLMutableArray FLArray_AsMutable(FLArray a)         FLAPI {return a ? a->asMutable() : nullptr;}
FLArray FLMutableArray_GetSource(FLMutableArray a)  FLAPI {return a ? a->source() : nullptr;}
bool FLMutableArray_IsChanged(FLMutableArray a)     FLAPI {return a && a->isChanged();}
void FLMutableArray_Resize(FLMutableArray a, uint32_t size)    FLAPI {a->resize(size);}

FLSlot FLMutableArray_Set(FLMutableArray a, uint32_t index)    FLAPI {return &a->setting(index);}
FLSlot FLMutableArray_Append(FLMutableArray a)                 FLAPI {return &a->appending();}

void FLMutableArray_Insert(FLMutableArray a, uint32_t firstIndex, uint32_t count) FLAPI {
    if (a) a->insert(firstIndex, count);
}

void FLMutableArray_Remove(FLMutableArray a, uint32_t firstIndex, uint32_t count) FLAPI {
    if(a) a->remove(firstIndex, count);
}

FLMutableArray FLMutableArray_GetMutableArray(FLMutableArray a, uint32_t index) FLAPI {
    return a ? a->getMutableArray(index) : nullptr;
}

FLMutableDict FLMutableArray_GetMutableDict(FLMutableArray a, uint32_t index) FLAPI {
    return a ? a->getMutableDict(index) : nullptr;
}


#pragma mark - DICTIONARIES:


uint32_t FLDict_Count(FLDict d)                 FLAPI {return d ? d->count() : 0;}
bool FLDict_IsEmpty(FLDict d)                   FLAPI {return d ? d->empty() : true;}
FLValue FLDict_Get(FLDict d, FLSlice keyString) FLAPI {return d ? d->get(keyString) : nullptr;}

#if 0
FLSlice FLSharedKey_GetKeyString(FLSharedKeys sk, int keyCode, FLError* outError)
{
    slice key;
    try {
        key = sk->decode((keyCode));
        if(!key && outError != nullptr) {
            *outError = kFLNotFound;
        }
    } catchError(outError)
    
    return key;
}
#endif

void FLDictIterator_Begin(FLDict d, FLDictIterator* i) FLAPI {
    static_assert(sizeof(FLDictIterator) >= sizeof(Dict::iterator), "FLDictIterator is too small");
    new (i) Dict::iterator(d);
    // Note: this is safe even if d is null.
}

FLValue FLDictIterator_GetKey(const FLDictIterator* i) FLAPI {
    return ((Dict::iterator*)i)->key();
}

FLString FLDictIterator_GetKeyString(const FLDictIterator* i) FLAPI {
    return ((Dict::iterator*)i)->keyString();
}

FLValue FLDictIterator_GetValue(const FLDictIterator* i) FLAPI {
    return ((Dict::iterator*)i)->value();
}

uint32_t FLDictIterator_GetCount(const FLDictIterator* i) FLAPI {
    return ((Dict::iterator*)i)->count();
}

bool FLDictIterator_Next(FLDictIterator* i) FLAPI {
    try {
        auto& iter = *(Dict::iterator*)i;
        ++iter;                 // throws if iterating past end
        if (iter)
            return true;
        iter.~iterator();
    } catchError(nullptr)
    return false;
}

void FLDictIterator_End(FLDictIterator* i) FLAPI {
    ((Dict::iterator*)i)->~iterator();
}


FLDictKey FLDictKey_Init(FLSlice string) FLAPI {
    FLDictKey key;
    static_assert(sizeof(FLDictKey) >= sizeof(Dict::key), "FLDictKey is too small");
    new (&key) Dict::key(string);
    return key;
}

FLSlice FLDictKey_GetString(const FLDictKey *key) FLAPI {
    auto realKey = (const Dict::key*)key;
    return realKey->string();
}

FLValue FLDict_GetWithKey(FLDict d, FLDictKey *k) FLAPI {
    if (!d)
        return nullptr;
    auto &key = *(Dict::key*)k;
    return d->get(key);
}


static FLMutableDict _newMutableDict(FLDict d, FLCopyFlags flags) noexcept {
    try {
        return (MutableDict*)retain(MutableDict::newDict(d, CopyFlags(flags)).get());
    } catchError(nullptr)
    return nullptr;
}

FLMutableDict FLMutableDict_New(void) FLAPI {
    return _newMutableDict(nullptr, kFLDefaultCopy);
}

FLMutableDict FLDict_MutableCopy(FLDict d, FLCopyFlags flags) FLAPI {
    return d ? _newMutableDict(d, flags) : nullptr;
}

FLMutableDict FLDict_AsMutable(FLDict d)           FLAPI {return d ? d->asMutable() : nullptr;}
FLDict FLMutableDict_GetSource(FLMutableDict d)    FLAPI {return d ? d->source() : nullptr;}
bool FLMutableDict_IsChanged(FLMutableDict d)      FLAPI {return d && d->isChanged();}

FLSlot FLMutableDict_Set(FLMutableDict d, FLString k)    FLAPI {return &d->setting(k);}

void FLMutableDict_Remove(FLMutableDict d, FLString key) FLAPI {if(d) d->remove(key);}
void FLMutableDict_RemoveAll(FLMutableDict d)            FLAPI {if(d) d->removeAll();}

FLMutableArray FLMutableDict_GetMutableArray(FLMutableDict d, FLString key) FLAPI {
    return d ? d->getMutableArray(key) : nullptr;
}

FLMutableDict FLMutableDict_GetMutableDict(FLMutableDict d, FLString key) FLAPI {
    return d ? d->getMutableDict(key) : nullptr;
}


//////// SHARED KEYS


FLSharedKeys FLSharedKeys_Create()                         FLAPI {return retain(new SharedKeys());}
FLSharedKeys FLSharedKeys_Retain(FLSharedKeys sk)          FLAPI {return retain(sk);}
void FLSharedKeys_Release(FLSharedKeys sk)                 FLAPI {release(sk);}
unsigned FLSharedKeys_Count(FLSharedKeys sk)               FLAPI {return (unsigned)sk->count();}
FLSharedKeys FLSharedKeys_CreateFromStateData(FLSlice data)FLAPI {return retain(new SharedKeys(data));}
FLSliceResult FLSharedKeys_GetStateData(FLSharedKeys sk)   FLAPI {return toSliceResult(sk->stateData());}
FLString FLSharedKeys_Decode(FLSharedKeys sk, int key)     FLAPI {return sk->decode(key);}

int FLSharedKeys_Encode(FLSharedKeys sk, FLString keyStr, bool add) FLAPI {
    int intKey;
    if (!(add ? sk->encodeAndAdd(keyStr, intKey) : sk->encode(keyStr, intKey)))
        intKey = -1;
    return intKey;
}


#pragma mark - SLOTS:


void FLSlot_SetNull(FLSlot slot)                        FLAPI {slot->set(Null());}
void FLSlot_SetBool(FLSlot slot, bool v)                FLAPI {slot->set(v);}
void FLSlot_SetInt(FLSlot slot, int64_t v)              FLAPI {slot->set(v);}
void FLSlot_SetUInt(FLSlot slot, uint64_t v)            FLAPI {slot->set(v);}
void FLSlot_SetFloat(FLSlot slot, float v)              FLAPI {slot->set(v);}
void FLSlot_SetDouble(FLSlot slot, double v)            FLAPI {slot->set(v);}
void FLSlot_SetString(FLSlot slot, FLString v)          FLAPI {slot->set(v);}
void FLSlot_SetData(FLSlot slot, FLSlice v)             FLAPI {slot->setData(v);}
void FLSlot_SetValue(FLSlot slot, FLValue v)            FLAPI {slot->set(v);}


#pragma mark - DEEP ITERATOR:


FLDeepIterator FLDeepIterator_New(FLValue v)            FLAPI {return new DeepIterator(v);}
void FLDeepIterator_Free(FLDeepIterator i)              FLAPI {delete i;}
FLValue FLDeepIterator_GetValue(FLDeepIterator i)       FLAPI {return i->value();}
FLSlice FLDeepIterator_GetKey(FLDeepIterator i)         FLAPI {return i->keyString();}
uint32_t FLDeepIterator_GetIndex(FLDeepIterator i)      FLAPI {return i->index();}
size_t FLDeepIterator_GetDepth(FLDeepIterator i)        FLAPI {return i->path().size();}
void FLDeepIterator_SkipChildren(FLDeepIterator i)      FLAPI {i->skipChildren();}

bool FLDeepIterator_Next(FLDeepIterator i) FLAPI {
    i->next();
    return i->value() != nullptr;
}

void FLDeepIterator_GetPath(FLDeepIterator i, FLPathComponent* *outPath, size_t *outDepth) FLAPI {
    static_assert(sizeof(FLPathComponent) == sizeof(DeepIterator::PathComponent),
                  "FLPathComponent does not match PathComponent");
    auto &path = i->path();
    *outPath = (FLPathComponent*) path.data();
    *outDepth = path.size();
}

FLSliceResult FLDeepIterator_GetPathString(FLDeepIterator i) FLAPI {
    return toSliceResult(alloc_slice(i->pathString()));
}

FLSliceResult FLDeepIterator_GetJSONPointer(FLDeepIterator i) FLAPI {
    return toSliceResult(alloc_slice(i->jsonPointer()));
}


#pragma mark - KEY-PATHS:


FLKeyPath FLKeyPath_New(FLSlice specifier, FLError *outError) FLAPI {
    try {
        return new Path((std::string)(slice)specifier);
    } catchError(outError)
    return nullptr;
}

void FLKeyPath_Free(FLKeyPath path) FLAPI {
    delete path;
}

FLValue FLKeyPath_Eval(FLKeyPath path, FLValue root) FLAPI {
    return path->eval(root);
}

FLValue FLKeyPath_EvalOnce(FLSlice specifier, FLValue root, FLError *outError) FLAPI {
    try {
        return Path::eval((std::string)(slice)specifier, root);
    } catchError(outError)
    return nullptr;
}


#pragma mark - ENCODER:


FLEncoder FLEncoder_New(void) FLAPI {
    return FLEncoder_NewWithOptions(kFLEncodeFleece, 0, true);
}

FLEncoder FLEncoder_NewWithOptions(FLEncoderFormat format,
                                   size_t reserveSize, bool uniqueStrings) FLAPI
{
    return new FLEncoderImpl(format, reserveSize, uniqueStrings);
}

FLEncoder FLEncoder_NewWritingToFile(FILE *outputFile, bool uniqueStrings) FLAPI {
    return new FLEncoderImpl(outputFile, uniqueStrings);
}

void FLEncoder_Reset(FLEncoder e) FLAPI {
    e->reset();
}

void FLEncoder_Free(FLEncoder e) FLAPI {
    delete e;
}

void FLEncoder_SetSharedKeys(FLEncoder e, FLSharedKeys sk) FLAPI {
    if (e->isFleece())
        e->fleeceEncoder->setSharedKeys(sk);
}

void FLEncoder_SuppressTrailer(FLEncoder e) FLAPI {
    if (e->isFleece())
        e->fleeceEncoder->suppressTrailer();
}

void FLEncoder_Amend(FLEncoder e, FLSlice base, bool reuseStrings, bool externPointers) FLAPI {
    if (e->isFleece() && base.size > 0) {
        e->fleeceEncoder->setBase(base, externPointers);
        if(reuseStrings)
            e->fleeceEncoder->reuseBaseStrings();
    }
}

FLSlice FLEncoder_GetBase(FLEncoder e) FLAPI {
    if (e->isFleece())
        return e->fleeceEncoder->base();
    return {};
}

size_t FLEncoder_GetNextWritePos(FLEncoder e) FLAPI {
    if (e->isFleece())
        return e->fleeceEncoder->nextWritePos();
    return 0;
}

size_t FLEncoder_BytesWritten(FLEncoder e) FLAPI {
    return ENCODER_DO(e, bytesWritten());
}

bool FLEncoder_WriteNull(FLEncoder e)              FLAPI {ENCODER_TRY(e, writeNull());}
bool FLEncoder_WriteUndefined(FLEncoder e)         FLAPI {ENCODER_TRY(e, writeUndefined());}
bool FLEncoder_WriteBool(FLEncoder e, bool b)      FLAPI {ENCODER_TRY(e, writeBool(b));}
bool FLEncoder_WriteInt(FLEncoder e, int64_t i)    FLAPI {ENCODER_TRY(e, writeInt(i));}
bool FLEncoder_WriteUInt(FLEncoder e, uint64_t u)  FLAPI {ENCODER_TRY(e, writeUInt(u));}
bool FLEncoder_WriteFloat(FLEncoder e, float f)    FLAPI {ENCODER_TRY(e, writeFloat(f));}
bool FLEncoder_WriteDouble(FLEncoder e, double d)  FLAPI {ENCODER_TRY(e, writeDouble(d));}
bool FLEncoder_WriteString(FLEncoder e, FLSlice s) FLAPI {ENCODER_TRY(e, writeString(s));}
bool FLEncoder_WriteDateString(FLEncoder e, FLTimestamp ts, bool asUTC)
                                                   FLAPI {ENCODER_TRY(e, writeDateString(ts,asUTC));}
bool FLEncoder_WriteData(FLEncoder e, FLSlice d)   FLAPI {ENCODER_TRY(e, writeData(d));}
bool FLEncoder_WriteRaw(FLEncoder e, FLSlice r)    FLAPI {ENCODER_TRY(e, writeRaw(r));}
bool FLEncoder_WriteValue(FLEncoder e, FLValue v)  FLAPI {ENCODER_TRY(e, writeValue(v));}

bool FLEncoder_BeginArray(FLEncoder e, size_t reserve)  FLAPI {ENCODER_TRY(e, beginArray(reserve));}
bool FLEncoder_EndArray(FLEncoder e)                    FLAPI {ENCODER_TRY(e, endArray());}
bool FLEncoder_BeginDict(FLEncoder e, size_t reserve)   FLAPI {ENCODER_TRY(e, beginDictionary(reserve));}
bool FLEncoder_WriteKey(FLEncoder e, FLSlice s)         FLAPI {ENCODER_TRY(e, writeKey(s));}
bool FLEncoder_WriteKeyValue(FLEncoder e, FLValue key)  FLAPI {ENCODER_TRY(e, writeKey(key));}
bool FLEncoder_EndDict(FLEncoder e)                     FLAPI {ENCODER_TRY(e, endDictionary());}


bool FLEncoder_ConvertJSON(FLEncoder e, FLSlice json) FLAPI {
    if (!e->hasError()) {
        try {
            if (e->isFleece()) {
                JSONConverter *jc = e->jsonConverter.get();
                if (jc) {
                    jc->reset();
                } else {
                    jc = new JSONConverter(*e->fleeceEncoder);
                    e->jsonConverter.reset(jc);
                }
                if (jc->encodeJSON(json)) {                   // encodeJSON can throw
                    return true;
                } else {
                    e->errorCode = (FLError)jc->errorCode();
                    e->errorMessage = jc->errorMessage();
                }
            } else {
                e->jsonEncoder->writeJSON(json);
            }
        } catch (const std::exception &x) {
            e->recordException(x);
        }
    }
    return false;
}

FLError FLEncoder_GetError(FLEncoder e) FLAPI {
    return (FLError)e->errorCode;
}

const char* FLEncoder_GetErrorMessage(FLEncoder e) FLAPI {
    return e->hasError() ? e->errorMessage.c_str() : nullptr;
}

void FLEncoder_SetExtraInfo(FLEncoder e, void *info) FLAPI {
    e->extraInfo = info;
}

void* FLEncoder_GetExtraInfo(FLEncoder e) FLAPI {
    return e->extraInfo;
}

size_t FLEncoder_FinishItem(FLEncoder e) FLAPI {
    if (e->isFleece())
        return e->fleeceEncoder->finishItem();
    return 0;
}

FLDoc FLEncoder_FinishDoc(FLEncoder e, FLError *outError) FLAPI {
    if (e->fleeceEncoder) {
        if (!e->hasError()) {
            try {
                return retain(e->fleeceEncoder->finishDoc().get());       // finish() can throw
            } catch (const std::exception &x) {
                e->recordException(x);
            }
        }
    } else {
        e->errorCode = kFLUnsupported;  // Doc class doesn't support JSON data
    }
    // Failure:
    if (outError)
        *outError = e->errorCode;
    e->reset();
    return nullptr;
}


FLSliceResult FLEncoder_Finish(FLEncoder e, FLError *outError) FLAPI {
    if (!e->hasError()) {
        try {
            return toSliceResult(ENCODER_DO(e, finish()));       // finish() can throw
        } catch (const std::exception &x) {
            e->recordException(x);
        }
    }
    // Failure:
    if (outError)
        *outError = e->errorCode;
    e->reset();
    return {nullptr, 0};
}


#pragma mark - DOCUMENTS


FLDoc FLDoc_FromResultData(FLSliceResult data, FLTrust trust, FLSharedKeys sk, FLSlice externData) FLAPI {
    return retain(new Doc(alloc_slice(data), (Doc::Trust)trust, sk, externData));
}

FLDoc FLDoc_FromJSON(FLSlice json, FLError *outError) FLAPI {
    try {
        return retain(Doc::fromJSON(json).get());
    } catchError(outError);
    return nullptr;
}

void FLDoc_Release(FLDoc doc)                  FLAPI {release(doc);}
FLDoc FLDoc_Retain(FLDoc doc)                  FLAPI {return retain(doc);}

FLSharedKeys FLDoc_GetSharedKeys(FLDoc doc)    FLAPI {return doc ? doc->sharedKeys() : nullptr;}
FLValue FLDoc_GetRoot(FLDoc doc)               FLAPI {return doc ? doc->root() : nullptr;}
FLSlice FLDoc_GetData(FLDoc doc)               FLAPI {return doc ? doc->data() : slice();}

FLSliceResult FLDoc_GetAllocedData(FLDoc doc) FLAPI {
    return doc ? toSliceResult(doc->allocedData()) : FLSliceResult{};
}


#pragma mark - DELTA COMPRESSION


FLSliceResult FLCreateJSONDelta(FLValue old, FLValue nuu) FLAPI {
    try {
        return toSliceResult(JSONDelta::create(old, nuu));
    } catch (const std::exception&) {
        return {};
    }
}

bool FLEncodeJSONDelta(FLValue old, FLValue nuu, FLEncoder jsonEncoder) FLAPI {
    try {
        JSONEncoder *enc = jsonEncoder->jsonEncoder.get();
        precondition(enc);  //TODO: Support encoding to Fleece
        JSONDelta::create(old, nuu, *enc);
        return true;
    } catch (const std::exception &x) {
        jsonEncoder->recordException(x);
        return false;
    }
}


FLSliceResult FLApplyJSONDelta(FLValue old, FLSlice jsonDelta, FLError *outError) FLAPI {
    try {
        return toSliceResult(JSONDelta::apply(old, jsonDelta));
    } catchError(outError);
    return {};
}

bool FLEncodeApplyingJSONDelta(FLValue old, FLSlice jsonDelta, FLEncoder encoder) FLAPI {
    try {
        Encoder *enc = encoder->fleeceEncoder.get();
        if (!enc)
            FleeceException::_throw(EncodeError, "FLEncodeApplyingJSONDelta cannot encode JSON");
        JSONDelta::apply(old, jsonDelta, false, *enc);
        return true;
    } catch (const std::exception &x) {
        encoder->recordException(x);
        return false;
    }
}
