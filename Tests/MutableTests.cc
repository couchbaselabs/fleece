//
// MutableTests.cc
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

#include "FleeceTests.hh"
#include "FleeceImpl.hh"
#include "MutableArray.hh"
#include "MutableDict.hh"
#include "Doc.hh"
#include <iostream>

namespace fleece {
    using namespace fleece::impl;

    TEST_CASE("MutableArray type checking", "[Mutable]") {
        Retained<MutableArray> ma = MutableArray::newArray();

        CHECK(ma->asArray() == ma);
        CHECK(ma->isMutable());
        CHECK(ma->asMutable() == ma);

        CHECK(ma->type() == kArray);

        CHECK(ma->asBool() == true);
        CHECK(ma->asInt() == 0);
        CHECK(ma->asUnsigned() == 0);
        CHECK(ma->asFloat() == 0.0);
        CHECK(ma->asDouble() == 0.0);

        CHECK(!ma->isInteger());
        CHECK(!ma->isUnsigned());
        CHECK(!ma->isDouble());

        CHECK(ma->asString() == nullslice);
        CHECK(ma->asData() == nullslice);
        CHECK(ma->toString() == nullslice);
        CHECK(ma->asDict() == nullptr);
        CHECK(ma->asArray() == ma);
    }


    TEST_CASE("MutableArray set values", "[Mutable]") {
        static constexpr size_t kSize = 17;
        Retained<MutableArray> ma = MutableArray::newArray();

        REQUIRE(ma->count() == 0);
        REQUIRE(ma->empty());
        REQUIRE(ma->get(0) == nullptr);

        {
            MutableArray::iterator i(ma);
            CHECK(!i);
        }

        CHECK(!ma->isChanged());
        ma->resize(kSize);
        CHECK(ma->isChanged());
        REQUIRE(ma->count() == kSize);
        REQUIRE(ma->count() == kSize);
        REQUIRE(!ma->empty());

        for (int i = 0; i < kSize; i++)
            REQUIRE(ma->get(i)->type() == kNull);

        ma->set(0, nullValue);
        ma->set(1, false);
        ma->set(2, true);
        ma->set(3, 0);
        ma->set(4, -123);
        ma->set(5, 2017);
        ma->set(6, 123456789);
        ma->set(7, -123456789);
        ma->set(8, "Hot dog"_sl);
        ma->set(9, float(M_PI));
        ma->set(10, M_PI);
        ma->set(11, std::numeric_limits<uint64_t>::max());
        ma->set(12, (int64_t)0x100000000LL);
        ma->set(13, (uint64_t)0x100000000ULL);
        ma->set(14, std::numeric_limits<int64_t>::min());
        ma->set(15, (int64_t)9223372036854775807LL);
        ma->set(16, (int64_t)-9223372036854775807LL);

        static const valueType kExpectedTypes[kSize] = {
            kNull, kBoolean, kBoolean, kNumber, kNumber, kNumber, kNumber, kNumber, kString,
            kNumber, kNumber, kNumber, kNumber, kNumber, kNumber, kNumber, kNumber
        };
        for (int i = 0; i < 9; i++)
            CHECK(ma->get(i)->type() == kExpectedTypes[i]);
        CHECK(ma->get(1)->asBool() == false);
        CHECK(ma->get(2)->asBool() == true);
        CHECK(ma->get(3)->asInt() == 0);
        CHECK(ma->get(4)->asInt() == -123);
        CHECK(ma->get(5)->asInt() == 2017);
        CHECK(ma->get(6)->asInt() == 123456789);
        CHECK(ma->get(7)->asInt() == -123456789);
        CHECK(ma->get(8)->asString() == "Hot dog"_sl);
        CHECK(ma->get(9)->asFloat() == float(M_PI));
        CHECK(ma->get(10)->asDouble() == M_PI);
        CHECK(ma->get(11)->asUnsigned() == std::numeric_limits<uint64_t>::max());
        CHECK(ma->get(12)->asInt() == 0x100000000LL);
        CHECK(ma->get(13)->asUnsigned() == 0x100000000ULL);
        CHECK(ma->get(14)->asInt() == std::numeric_limits<int64_t>::min());
        CHECK(ma->get(15)->asInt() == 9223372036854775807LL);
        CHECK(ma->get(16)->asInt() == -9223372036854775807LL);

        {
            MutableArray::iterator i(ma);
            for (int n = 0; n < kSize; ++n) {
                std::cerr << "Item " << n << ": " << (void*)i.value() << "\n";
                CHECK(i);
                CHECK(i.value() != nullptr);
                CHECK(i.value()->type() == kExpectedTypes[n]);
                ++i;
            }
            CHECK(!i);
        }

        CHECK(ma->asArray()->toJSON() == "[null,false,true,0,-123,2017,123456789,-123456789,\"Hot dog\",3.1415927,"
              "3.141592653589793,18446744073709551615,4294967296,4294967296,"
              "-9223372036854775808,9223372036854775807,-9223372036854775807]"_sl);

        ma->remove(3, 5);
        CHECK(ma->count() == 12);
        CHECK(ma->get(2)->type() == kBoolean);
        CHECK(ma->get(2)->asBool() == true);
        CHECK(ma->get(3)->type() == kString);

        ma->insert(1, 2);
        CHECK(ma->count() == 14);
        REQUIRE(ma->get(1)->type() == kNull);
        REQUIRE(ma->get(2)->type() == kNull);
        CHECK(ma->get(3)->type() == kBoolean);
        CHECK(ma->get(3)->asBool() == false);

#if 0 // Enable again if this feature becomes an option
        // Check that FP values are stored as ints when possible:
        ma->set(0, (float)12345);
        CHECK(ma->get(0)->isInteger());
        CHECK(ma->get(0)->asInt() == 12345);
        ma->set(0, (double)12345);
        CHECK(ma->get(0)->isInteger());
        CHECK(ma->get(0)->asInt() == 12345);
#endif
    }


    TEST_CASE("MutableArray as Array", "[Mutable]") {
        Retained<MutableArray> ma = MutableArray::newArray();
        const Array *a = ma->asArray();
        CHECK(a->type() == kArray);
        CHECK(a->count() == 0);
        CHECK(a->empty());

        ma->resize(2);
        ma->set(0, 123);
        ma->set(1, 456);

        CHECK(a->count() == 2);
        CHECK(!a->empty());
        CHECK(a->get(0)->asInt() == 123);
        CHECK(a->get(1)->asInt() == 456);

        Array::iterator i(a);
        CHECK(i);
        CHECK(i.value()->asInt() == 123);
        ++i;
        CHECK(i);
        CHECK(i.value()->asInt() == 456);
        ++i;
        CHECK(!i);
    }


    TEST_CASE("MutableArray pointers", "[Mutable]") {
        Retained<MutableArray> ma = MutableArray::newArray();
        ma->resize(2);
        ma->set(0, 123);
        ma->set(1, 456);

        Retained<MutableArray> mb = MutableArray::newArray();
        CHECK(!mb->isChanged());
        mb->append(ma);
        CHECK(mb->isChanged());

        CHECK(mb->get(0) == ma);
        CHECK(mb->getMutableArray(0) == ma);

        Encoder enc;
        enc.beginArray();
        enc << "totoro";
        enc << "catbus";
        enc.endArray();
        auto doc = enc.finishDoc();
        const Array* fleeceArray = doc->asArray();

        CHECK(fleeceArray->asMutable() == nullptr);

        mb->append(fleeceArray);
        CHECK(mb->get(1) == fleeceArray);
        auto mc = mb->getMutableArray(1);
        CHECK(mc != nullptr);
        CHECK(mc == mb->get(1));
        CHECK(mb->get(1)->type() == kArray);

        CHECK(mc->count() == 2);
        CHECK(mc->asArray()->count() == 2);
        CHECK(mc->get(0)->asString() == "totoro"_sl);
        CHECK(mc->get(1)->asString() == "catbus"_sl);
    }


    TEST_CASE("MutableArray copy", "[Mutable]") {
        Retained<MutableArray> ma = MutableArray::newArray(2);
        ma->set(0, 123);
        ma->set(1, "howdy"_sl);

        Retained<MutableArray> mb = MutableArray::newArray(1);
        mb->set(0, ma);
        REQUIRE(mb->get(0) == ma);

        Retained<MutableArray> mc = MutableArray::newArray(1);
        mc->set(0, mb);
        REQUIRE(mc->get(0) == mb);

        Retained<MutableArray> copy = mc->copy();
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get(0) == mc->get(0));              // it's shallow

        copy = mc->copy(kDeepCopy);
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get(0) != mc->get(0));              // it's deep
        CHECK(copy->get(0)->asArray()->get(0) != ma);   // it's so deep you can't get under it
    }


    TEST_CASE("MutableArray copy immutable", "[Mutable]") {
        Retained<Doc> doc = Doc::fromJSON("[123, \"howdy\"]"_sl);
        const Array *a = doc->root()->asArray();

        Retained<MutableArray> copy = MutableArray::newArray(a);
        CHECK(copy->source() == a);
        CHECK(copy->isEqual(a));

        Retained<MutableArray> mb = MutableArray::newArray(1);
        mb->set(0, a);
        REQUIRE(mb->get(0) == a);

        Retained<MutableArray> mc = MutableArray::newArray(1);
        mc->set(0, mb);
        REQUIRE(mc->get(0) == mb);

        copy = mc->copy();
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get(0) == mc->get(0));              // it's shallow

        copy = mc->copy(kDeepCopy);
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get(0) != mc->get(0));              // it's deep
        CHECK(copy->get(0)->asArray()->get(0) == a);    // but the immutable data is the same

        copy = mc->copy(CopyFlags(kDeepCopy | kCopyImmutables));
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get(0) != mc->get(0));              // it's deep
        CHECK(copy->get(0)->asArray()->get(0) != a);   // it's so deep you can't get under it
    }


#pragma mark - MUTABLE DICT:


    TEST_CASE("MutableDict type checking", "[Mutable]") {
        Retained<MutableDict> md = MutableDict::newDict();
        const Dict *d = md;
        CHECK(d->type() == kDict);

        CHECK(d->isMutable());
        CHECK(d->asMutable() == md);

        CHECK(d->type() == kDict);

        CHECK(d->asBool() == true);
        CHECK(d->asInt() == 0);
        CHECK(d->asUnsigned() == 0);
        CHECK(d->asFloat() == 0.0);
        CHECK(d->asDouble() == 0.0);

        CHECK(!d->isInteger());
        CHECK(!d->isUnsigned());
        CHECK(!d->isDouble());

        CHECK(d->asString() == nullslice);
        CHECK(d->asData() == nullslice);
        CHECK(d->toString() == nullslice);
        CHECK(d->asArray() == nullptr);
        CHECK(d->asDict() == d);
    }


    TEST_CASE("MutableDict set values", "[Mutable]") {
        Retained<MutableDict> md = MutableDict::newDict();
        CHECK(md->count() == 0);
        CHECK(md->get("foo"_sl) == nullptr);

        {
            MutableDict::iterator i(md);
            CHECK(!i);
        }

        CHECK(!md->isChanged());

        md->set("null"_sl, nullValue);
        md->set("f"_sl, false);
        md->set("t"_sl, true);
        md->set("z"_sl, 0);
        md->set("-"_sl, -123);
        md->set("+"_sl, 2017);
        md->set("hi"_sl, 123456789);
        md->set("lo"_sl, -123456789);
        md->set("str"_sl, "Hot dog"_sl);
        CHECK(md->count() == 9);

        static const slice kExpectedKeys[9] = {
            "+"_sl, "-"_sl, "f"_sl, "hi"_sl, "lo"_sl, "null"_sl, "str"_sl, "t"_sl, "z"_sl};
        static const valueType kExpectedTypes[9] = {
            kNumber, kNumber, kBoolean, kNumber, kNumber, kNull, kString, kBoolean, kNumber};
        for (int i = 0; i < 9; i++)
            REQUIRE(md->get(kExpectedKeys[i])->type() == kExpectedTypes[i]);

        CHECK(md->get("f"_sl)->asBool() == false);
        CHECK(md->get("t"_sl)->asBool() == true);
        CHECK(md->get("z"_sl)->asInt() == 0);
        CHECK(md->get("-"_sl)->asInt() == -123);
        CHECK(md->get("+"_sl)->asInt() == 2017);
        CHECK(md->get("hi"_sl)->asInt() == 123456789);
        CHECK(md->get("lo"_sl)->asInt() == -123456789);
        CHECK(md->get("str"_sl)->asString() == "Hot dog"_sl);
        CHECK(md->get("foo"_sl) == nullptr);

        {
            bool found[9] = { };
            MutableDict::iterator i(md);
            for (int n = 0; n < 9; ++n) {
                std::cerr << "Item " << n << ": " << i.keyString() << " = " << (void*)i.value() << "\n";
                CHECK(i);
                slice key = i.keyString();
                auto j = std::find(&kExpectedKeys[0], &kExpectedKeys[9], key) - &kExpectedKeys[0];
                REQUIRE(j < 9);
                REQUIRE(found[j] == false);
                found[j] = true;
                CHECK(i.value() != nullptr);
                CHECK(i.value()->type() == kExpectedTypes[j]);
                ++i;
            }
            CHECK(!i);
        }

        md->remove("lo"_sl);
        CHECK(md->get("lo"_sl) == nullptr);
        CHECK(md->count() == 8);

//        CHECK(md->toJSON() == "{\"+\":2017,\"-\":-123,\"f\":false,\"hi\":123456789,\"null\":null,\"str\":\"Hot dog\",\"t\":true,\"z\":0}"_sl);

        md->removeAll();
        CHECK(md->count() == 0);
        {
            MutableDict::iterator i(md);
            CHECK(!i);
        }
    }


    TEST_CASE("MutableDict as Dict", "[Mutable]") {
        Retained<MutableDict> md = MutableDict::newDict();
        const Dict *d = md;
        CHECK(d->type() == kDict);
        CHECK(d->count() == 0);
        CHECK(d->empty());
        CHECK(d->get("foo"_sl) == nullptr);
        {
            Dict::iterator i(d);
            CHECK(!i);
        }

        md->set("null"_sl, nullValue);
        md->set("f"_sl, false);
        md->set("t"_sl, true);
        md->set("z"_sl, 0);
        md->set("-"_sl, -123);
        md->set("+"_sl, 2017);
        md->set("hi"_sl, 123456789);
        md->set("lo"_sl, -123456789);
        md->set("str"_sl, "Hot dog"_sl);

        static const slice kExpectedKeys[9] = {
            "+"_sl, "-"_sl, "f"_sl, "hi"_sl, "lo"_sl, "null"_sl, "str"_sl, "t"_sl, "z"_sl};
        static const valueType kExpectedTypes[9] = {
            kNumber, kNumber, kBoolean, kNumber, kNumber, kNull, kString, kBoolean, kNumber};
        for (int i = 0; i < 9; i++)
            REQUIRE(d->get(kExpectedKeys[i])->type() == kExpectedTypes[i]);

        {
            bool found[9] = { };
            Dict::iterator i(d);
            for (int n = 0; n < 9; ++n) {
                std::cerr << "Item " << n << ": " << i.keyString() << " = " << (void*)i.value() << "\n";
                CHECK(i);
                slice key = i.keyString();
                auto j = std::find(&kExpectedKeys[0], &kExpectedKeys[9], key) - &kExpectedKeys[0];
                REQUIRE(j < 9);
                REQUIRE(found[j] == false);
                found[j] = true;
                CHECK(i.value() != nullptr);
                CHECK(i.value()->type() == kExpectedTypes[j]);
                ++i;
            }
            CHECK(!i);
        }

        md->remove("lo"_sl);
        CHECK(d->get("lo"_sl) == nullptr);

        CHECK(d->toJSON() == "{\"+\":2017,\"-\":-123,\"f\":false,\"hi\":123456789,\"null\":null,\"str\":\"Hot dog\",\"t\":true,\"z\":0}"_sl);

        md->removeAll();
        CHECK(d->count() == 0);
        {
            Dict::iterator i(d);
            CHECK(!i);
        }

    }


    TEST_CASE("Mutable long strings", "[Mutable]") {
        const char *chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        Retained<MutableArray> ma = MutableArray::newArray(50);
        for (int len = 0; len < 50; ++len)
            ma->set(len, slice(chars, len));
        for (int len = 0; len < 50; ++len)
            CHECK(ma->get(len)->asString() == slice(chars, len));
    }


    TEST_CASE("MutableDict copy", "[Mutable]") {
        Retained<MutableDict> ma = MutableDict::newDict();
        ma->set("a"_sl, 123);
        ma->set("b"_sl, "howdy"_sl);

        Retained<MutableDict> mb = MutableDict::newDict();
        mb->set("a"_sl, ma);
        REQUIRE(mb->get("a"_sl) == ma);

        Retained<MutableDict> mc = MutableDict::newDict();
        mc->set("a"_sl, mb);
        REQUIRE(mc->get("a"_sl) == mb);

        Retained<MutableDict> copy = mc->copy();
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get("a"_sl) == mc->get("a"_sl));              // it's shallow

        copy = mc->copy(kDeepCopy);
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get("a"_sl) != mc->get("a"_sl));              // it's deep
        CHECK(copy->get("a"_sl)->asDict()->get("a"_sl) != ma);   // it's so deep you can't get under it
    }


    TEST_CASE("MutableDict copy immutable", "[Mutable]") {
        Retained<Doc> doc = Doc::fromJSON("{\"a\":123,\"b\":\"howdy\"}"_sl);
        const Dict *a = doc->root()->asDict();

        Retained<MutableDict> copy = MutableDict::newDict(a);
        CHECK(copy->source() == a);
        CHECK(copy->isEqual(a));

        Retained<MutableDict> mb = MutableDict::newDict();
        mb->set("a"_sl, a);
        REQUIRE(mb->get("a"_sl) == a);

        Retained<MutableDict> mc = MutableDict::newDict();
        mc->set("a"_sl, mb);
        REQUIRE(mc->get("a"_sl) == mb);

        copy = mc->copy();
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get("a"_sl) == mc->get("a"_sl));              // it's shallow

        copy = mc->copy(kDeepCopy);
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get("a"_sl) != mc->get("a"_sl));              // it's deep
        CHECK(copy->get("a"_sl)->asDict()->get("a"_sl) == a);    // but the immutable data is the same

        copy = mc->copy(CopyFlags(kDeepCopy | kCopyImmutables));
        CHECK(copy != mc);
        CHECK(copy->isEqual(mc));
        CHECK(copy->get("a"_sl) != mc->get("a"_sl));              // it's deep
        CHECK(copy->get("a"_sl)->asDict()->get("a"_sl) != a);   // it's so deep you can't get under it
    }


#pragma mark - ENCODING:


    class FakePersistentSharedKeys : public PersistentSharedKeys {
    protected:
        bool read() override                    {return _persistedData && loadFrom(_persistedData);}
        void write(slice encodedData) override  {_persistedData = encodedData;}
        alloc_slice _persistedData;
    };


    TEST_CASE("Encoding mutable array", "[Mutable]") {
        alloc_slice data;
        {
            Encoder enc;
            enc.beginArray();
            enc << "totoro";
            enc << "catbus";
            enc.endArray();
            data = enc.finish();
        }
        std::cerr << "Original data: " << data << "\n";
        const Array* fleeceArray = Value::fromData(data)->asArray();
        std::cerr << "Contents:      " << fleeceArray->toJSON().asString() << "\n";

        Encoder enc2;
        enc2.setBase(data);
        enc2.beginArray();
        enc2 << fleeceArray->get(1);
        enc2 << fleeceArray->get(0);
        enc2.endArray();
        alloc_slice data2 = enc2.finish();
        std::cerr << "Delta:         " << data2 << "\n";
        REQUIRE(data2.size == 8);      // may change slightly with changes to implementation

        data.append(data2);
        const Array* newArray = Value::fromData(data)->asArray();
        std::cerr << "Contents:      " << newArray->toJSON().asString() << "\n";
    }


    template <class ITER>
    static void CHECKiter(ITER &i, const char *key, const char *value) {
        CHECK(i);
        CHECK(i.keyString() == slice(key));
        CHECK(i.value()->asString() == slice(value));
        ++i;
    }


    static void testEncodingMutableDictWithSharedKeys(SharedKeys *sk) {
        auto psk = dynamic_cast<PersistentSharedKeys*>(sk);
        alloc_slice data;
        if (psk) psk->transactionBegan();
        {
            Encoder enc;
            enc.setSharedKeys(sk);
            enc.beginDictionary();
            enc.writeKey("Asleep");
            enc << "true";
            enc.writeKey("Mood");
            enc << "happy";
            enc.writeKey("Name");
            enc << "totoro";
            enc.writeKey("zzShirt Size");       // will not become a shared key due to space
            enc << "XXXL";
            enc.writeKey("Vehicle");
            enc << "catbus";
            enc.endDictionary();
            data = enc.finish();
        }
        if (psk) {psk->save(); psk->transactionEnded();}

        Retained<Doc> original = new Doc(data, Doc::kTrusted, sk);
        const Dict* originalDict = original->asDict();
        std::cerr << "Contents:      " << originalDict->toJSON().asString() << "\n";
        std::cerr << "Original data: " << data << "\n\n";
        Value::dump(data, std::cerr);

        Retained<MutableDict> update = MutableDict::newDict(originalDict);
        CHECK(update->count() == 5);
        update->set("zFriend"_sl, "catbus"_sl);
        CHECK(update->count() == 6);
        update->set("Vehicle"_sl, "top"_sl);
        CHECK(update->count() == 6);
        update->remove("Asleep"_sl);
        CHECK(update->count() == 5);
        update->remove("Asleep"_sl);
        CHECK(update->count() == 5);
        update->remove("Q"_sl);
        CHECK(update->count() == 5);

        {
            MutableDict::iterator i(update);
            CHECKiter(i, "Mood", "happy");
            CHECKiter(i, "Name", "totoro");
            CHECKiter(i, "Vehicle", "top");
            CHECKiter(i, "zFriend", "catbus");
            CHECKiter(i, "zzShirt Size", "XXXL");
            CHECK(!i);
        }

        { // Try the same thing but with a Dict iterator:
            Dict::iterator i(update);
            CHECKiter(i, "Mood", "happy");
            CHECKiter(i, "Name", "totoro");
            CHECKiter(i, "Vehicle", "top");
            CHECKiter(i, "zFriend", "catbus");
            CHECKiter(i, "zzShirt Size", "XXXL");
            CHECK(!i);
        }

        if (psk) psk->transactionBegan();
        Encoder enc2;
        enc2.setSharedKeys(sk);
        enc2.setBase(data);
        enc2.reuseBaseStrings();
        enc2.writeValue(update);
        alloc_slice delta = enc2.finish();
        if (psk) {psk->save(); psk->transactionEnded();}
        REQUIRE(delta.size == (sk ? 24 : 32));      // may change slightly with changes to implementation

        // Check that removeAll works when there's a base Dict:
        update->removeAll();
        CHECK(update->count() == 0);
        {
            MutableDict::iterator i(update);
            CHECK(!i);
        }

        alloc_slice combinedData(data);
        combinedData.append(delta);
        Scope combined(combinedData, sk);
        const Dict* newDict = Value::fromData(combinedData)->asDict();
        std::cerr << "Delta:         " << delta << "\n\n";
        Value::dump(combinedData, std::cerr);

        CHECK(newDict->get("Name"_sl)->asString() == "totoro"_sl);
        CHECK(newDict->get("zFriend"_sl)->asString() == "catbus"_sl);
        CHECK(newDict->get("Mood"_sl)->asString() == "happy"_sl);
        CHECK(newDict->get("zzShirt Size"_sl)->asString() == "XXXL"_sl);
        CHECK(newDict->get("Vehicle"_sl)->asString() == "top"_sl);
        CHECK(newDict->get("Asleep"_sl) == nullptr);
        CHECK(newDict->get("Q"_sl) == nullptr);

        {
            Dict::iterator i(newDict);
            CHECKiter(i, "Mood", "happy");
            CHECKiter(i, "Name", "totoro");
            CHECKiter(i, "Vehicle", "top");
            CHECKiter(i, "zFriend", "catbus");
            CHECKiter(i, "zzShirt Size", "XXXL");
            CHECK(!i);
        }
        //CHECK(newDict->rawCount() == 4);
        CHECK(newDict->count() == 5);

        std::cerr << "\nContents:      " << newDict->toJSON().asString() << "\n";
    }

    TEST_CASE("Encoding mutable dict", "[Mutable]") {
        testEncodingMutableDictWithSharedKeys(nullptr);
    }

    TEST_CASE("Encoding mutable dict with shared keys", "[Mutable][SharedKeys]") {
        auto sk = make_retained<SharedKeys>();
        testEncodingMutableDictWithSharedKeys(sk);
    }

    TEST_CASE("Encoding mutable dict with persistent shared keys", "[Mutable][SharedKeys]") {
        auto sk = make_retained<FakePersistentSharedKeys>();
        testEncodingMutableDictWithSharedKeys(sk);
    }


    TEST_CASE("Mutable dict with new key and persistent shared keys") {
        // Regression test for <https://github.com/couchbaselabs/couchbase-lite-C/issues/18>
        // MutableDict / HeapDict can't create a new shared key in its setter, because if the
        // SharedKeys are persistent and this is outside a transaction, it'll fail.
        auto psk = retained(new FakePersistentSharedKeys);
        Retained<Doc> doc;
        {
            psk->transactionBegan();
            Encoder enc;
            enc.setSharedKeys(psk);
            enc.beginDictionary();
            enc.writeKey("Asleep");
            enc << "true";
            enc.endDictionary();
            doc = enc.finishDoc();
            psk->save();
            psk->transactionEnded();
        }

        auto root = doc->root()->asDict();
        Retained<MutableDict> mut = MutableDict::newDict(root);

        mut->set("key"_sl, 123);                    // Should not register "key" as a new shared key
        CHECK(mut->get("key"_sl)->asInt() == 123);

        Retained<Doc> doc2;
        {
            psk->transactionBegan();
            Encoder enc;
            enc.setSharedKeys(psk);
            enc.writeValue(mut);                    // This will cause "key" to be registered
            doc2 = enc.finishDoc();
            psk->save();
            psk->transactionEnded();
        }

        auto root2 = doc2->root()->asDict();
        CHECK(root2->get("key"_sl)->asInt() == 123);

        CHECK(mut->get("key"_sl)->asInt() == 123);  // Make sure "key" being shared doesn't confuse mut

        mut->set("key"_sl, 456);                    // Make sure "key" doesn't get added again as an int
        CHECK(mut->count() == 2);
        CHECK(mut->get("key"_sl)->asInt() == 456);
    }


    TEST_CASE("Larger mutable dict", "[Mutable]") {
        auto data = readTestFile("1person.fleece");
        auto doc = Doc::fromFleece(data, Doc::kTrusted);
        auto person = doc->asDict();

        std::cerr << "Original data: " << data << "\n";
        std::cerr << "Contents:      " << person->toJSON().asString() << "\n";
        Value::dump(data, std::cerr);

        Retained<MutableDict> mp = MutableDict::newDict(person);
        mp->set("age"_sl, 31);
        MutableArray *friends = mp->getMutableArray("friends"_sl);
        REQUIRE(friends);
        auto frend = friends->getMutableDict(1);
        REQUIRE(frend);
        frend->set("name"_sl, "Reddy Kill-a-Watt"_sl);

        Encoder enc;
        enc.setBase(data);
        enc.reuseBaseStrings();
        enc.writeValue(mp);
        alloc_slice data2 = enc.finish();

        alloc_slice combined(data);
        combined.append(data2);
        const Dict* newDict = Value::fromData(combined)->asDict();
        std::cerr << "\n\nContents:      " << newDict->toJSON().asString() << "\n";
        std::cerr << "Delta:         " << data2 << "\n\n";
        Value::dump(combined, std::cerr);
    }


    TEST_CASE("Extern Destination", "[Mutable]") {
        Retained<Doc> doc = new Doc(readTestFile("1person.fleece"));
        auto person = doc->asDict();

        Retained<MutableDict> mp = MutableDict::newDict(person);
        mp->set("age"_sl, 666);

        Encoder enc;
        enc.setBase(doc->data(), true);
        enc.reuseBaseStrings();
        enc.writeValue(mp);
        alloc_slice data2 = enc.finish();

        Retained<Doc> newDoc = new Doc(data2, Doc::kTrusted, nullptr, doc->data());
        const Dict* newDict = newDoc->asDict();
        std::cerr << "Contents:      " << newDict->toJSON().asString() << "\n";

        CHECK(newDict->get("age"_sl)->asInt() == 666);
    }


    TEST_CASE("Compaction", "[Mutable]") {
        static constexpr size_t kMaxDataSize = 1000;
        alloc_slice data;
        size_t dataSize = 0;
        Retained<MutableDict> md = MutableDict::newDict();
        md->set("original"_sl, "This data is unchanged"_sl);
        const Dict *dict = nullptr;

        for (int i = 0; i < 1000; ++i) {
            // Change a key:
            md->set("fast"_sl, i);

            // Encode changes as a delta:
            Encoder enc;
            if (data) {
                enc.setBase(data, false, kMaxDataSize - 200);
                enc.reuseBaseStrings();
            }
            enc.writeValue(md);
            alloc_slice data2 = enc.finish();
            md = nullptr;

            // Append live part of old data and the delta, to produce the new data:
            slice dataUsed = enc.baseUsed();
            data = alloc_slice(dataUsed);
            data.append(data2);

            // Look at how the data size changes:
            if (data.size < dataSize)
                std::cerr << i << ": data went from " << dataSize << " to " << data.size << " bytes\n";
            dataSize = data.size;
            CHECK(dataSize < kMaxDataSize);
            //std::cerr << "Data: " << data << "\n";

            // Verify the data is correct:
            auto doc = Doc::fromFleece(data);
            dict = doc->asDict();
            CHECK(dict->get("fast"_sl)->asInt() == i);
            md = MutableDict::newDict(dict);
        }

        std::cerr << "data is now " << data.size << " bytes\n";
        if(getenv("GITHUB_ACTIONS") == nullptr) {

            // WEIRD: This line chokes GitHub actions
            dict->dump(std::cout);
        } else {
            std::cout << "Running on GH Actions, skipping dict dump...";
        }
        std::cerr << "\n";
    }


    TEST_CASE("Compaction-complex", "[Mutable]") {
        static constexpr size_t kMaxDataSize = 4000;
        alloc_slice data;
        size_t dataSize = 0, maxDataSize = 0;
        Retained<MutableDict> md = MutableDict::newDict();
        const Dict *dict = nullptr;

        srandom(4); // make it repeatable

        for (int i = 0; i < 1000; ++i) {
            // Change a key:

            Retained<MutableDict> prop = md;
            do {
                char c = (char)('A' + (random() % 4));
                slice key(&c, 1);
                Retained<MutableDict> p2 = prop->getMutableDict(key);
                if (!p2) {
                    p2 = MutableDict::newDict();
                    prop->set(key, p2);
                }
                prop = p2;
            } while (random() % 2 == 0);
            prop->set("i"_sl, i);

            //std::cerr << i << ": " << md->toJSONString() << "\n";

            // Encode changes as a delta:
            Encoder enc;
            if (data) {
                enc.setBase(data, false, kMaxDataSize - 200);
                enc.reuseBaseStrings();
            }
            enc.writeValue(md);
            alloc_slice data2 = enc.finish();
            md = nullptr;

            // Append live part of old data and the delta, to produce the new data:
            if (kMaxDataSize > 0) {
                slice dataUsed = enc.baseUsed();
                data = alloc_slice(dataUsed);
            }
            data.append(data2);

            // Look at how the data size changes:
//            if (data.size < dataSize)
//                std::cerr << i << ": data went from " << dataSize << " to " << data.size << " bytes\n";
            dataSize = data.size;
            maxDataSize = std::max(maxDataSize, dataSize);
            //CHECK(dataSize < kMaxDataSize);
            //std::cerr << "Data: " << data << "\n";
            //Value::dump(data, std::cerr);

            // Verify the data is correct:
            auto doc = Doc::fromFleece(data);
            dict = doc->asDict();
            //CHECK(dict->get("fast"_sl)->asInt() == i);
            md = MutableDict::newDict(dict);
        }

        std::cerr << "data is now " << data.size << " bytes; max was " << maxDataSize << "\n";
        //dict->dump(std::cerr);
        //std::cerr << "\n";

        Encoder enc;
        enc.writeValue(dict);
        alloc_slice packedData = enc.finish();
        std::cerr << "(Packed data would be " << packedData.size << " bytes)\n";
    }

}
