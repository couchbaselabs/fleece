//
// BuilderTests.cc
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

#include "FleeceTests.hh"
#include "Builder.hh"
#include <limits.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace fleece::impl;


TEST_CASE("Builder Empty", "[Builder]") {
    auto v = builder::Build("{}");
    REQUIRE(v);
    CHECK(v->toJSONString() == "{}");

    v = builder::Build("[]");
    REQUIRE(v);
    CHECK(v->toJSONString() == "[]");

    v = builder::Build(" \t{  \n }  ");
    REQUIRE(v);
    CHECK(v->toJSONString() == "{}");

    v = builder::Build(" [ ]  ");
    REQUIRE(v);
    CHECK(v->toJSONString() == "[]");
}


TEST_CASE("Builder Literals", "[Builder]") {
    auto v = builder::Build("[null, false, true, 0, 1, -12, +123, 123.5, -123.5, +123.5, 123e-4]");
    REQUIRE(v);
    CHECK(v->toJSONString() == "[null,false,true,0,1,-12,123,123.5,-123.5,123.5,0.0123]");
}


TEST_CASE("Builder String Literals", "[Builder]") {
    auto v = builder::Build(R"({a : 'foo\'', $b : "bar\"rab", _c_ : "", _ : "\r\\"})");
    REQUIRE(v);
    std::string expected = R"({"$b":"bar\"rab","_":"\r\\","_c_":"","a":"foo'"})";
    CHECK(v->toJSONString() == expected);
}


TEST_CASE("Builder Basic Dict", "[Builder]") {
    auto v = builder::Build("{name:%s, size:%d, weight:%f}",
                            "Zegpold", 12, 3.14);
    auto dict = v->asDict();
    REQUIRE(dict);
    CHECK(dict->get("name")->asString() == "Zegpold");
    CHECK(dict->get("size")->asInt() == 12);
    CHECK(dict->get("weight")->asDouble() == 3.14);
    CHECK(v->toJSONString() == R"({"name":"Zegpold","size":12,"weight":3.14})");
}


TEST_CASE("Builder Basic Array", "[Builder]") {
    auto v = builder::Build("[%s, %d, %f]",
                            "Zegpold", 12, 3.14);
    auto array = v->asArray();
    REQUIRE(array);
    CHECK(array->get(0)->asString() == "Zegpold");
    CHECK(array->get(1)->asInt() == 12);
    CHECK(array->get(2)->asDouble() == 3.14);
    CHECK(v->toJSONString() == R"(["Zegpold",12,3.14])");
}


TEST_CASE("Builder Nesting", "[Builder]") {
    auto v = builder::Build("{name:%s, coords:[%d, %d], info:{nickname:%s}}",
                            "Zegpold", 4, 5, "Zeggy");
    CHECK(v->toJSONString() == R"({"coords":[4,5],"info":{"nickname":"Zeggy"},"name":"Zegpold"})");
}


TEST_CASE("Builder Bool Params", "[Builder]") {
    bool t = true, f = false;
    auto v = builder::Build("[%c,%c]", char(t), char(f));
    CHECK(v->toJSONString() == R"([true,false])");
}


TEST_CASE("Builder Integer Params", "[Builder]") {
    int i0 = INT_MIN, i1 = INT_MAX;
    unsigned u = UINT_MAX;
    long l0 = LONG_MIN, l1 = LONG_MAX;
    unsigned long ul = ULONG_MAX;
    long long ll0 = LLONG_MIN, ll1 = LLONG_MAX;
    unsigned long long ull = ULLONG_MAX;
    ptrdiff_t p0 = PTRDIFF_MIN, p1 = PTRDIFF_MAX;
    size_t z1 = SIZE_MAX;
    auto v = builder::Build("[[%d, %d, %u], [%ld,%ld,%lu], [%lld,%lld,%llu], [%zd,%zd,%zu]]",
                               i0, i1, u,     l0, l1, ul,    ll0, ll1, ull,    p0, p1, z1);
    std::string expected32 = "[-2147483648,2147483647,4294967295]";
    std::string expected64 = "[-9223372036854775808,9223372036854775807,18446744073709551615]";
    std::string expected =
        "[" + (sizeof(int)       == 8 ? expected64 : expected32) + ","
            + (sizeof(long)      == 8 ? expected64 : expected32) + ","
            + (sizeof(long long) == 8 ? expected64 : expected32) + ","
            + (sizeof(size_t)    == 8 ? expected64 : expected32) + "]";
    CHECK(v->toJSONString() == expected);
}


TEST_CASE("Builder Value Params", "[Builder]") {
    auto v1 = builder::Build("[%s, %d, %f]",
                             "Zegpold", 12, 3.14);
    auto v2 = builder::Build("{v1: %p, v2: %p}", v1.get(), v1.get());
    CHECK(v2->toJSONString() == R"({"v1":["Zegpold",12,3.14],"v2":["Zegpold",12,3.14]})");
}


TEST_CASE("Builder Empty Strings", "[Builder]") {
    const char *str = "";
    slice sl(str);
    auto v = builder::Build("{a:%s, b:%.*s, d:[%s, %.*s]}",
                            str, FMTSLICE(sl), str, FMTSLICE(sl));
    CHECK(v->toJSONString() == R"({"a":"","b":"","d":["",""]})");
}


TEST_CASE("Builder Null Args", "[Builder]") {
    const char *str = nullptr;
    slice sl = nullslice;
    const Value *val = nullptr;
    auto v = builder::Build("{a:%s, b:%.*s, c:%p, d:[%s, %.*s, %p]}",
                            str, FMTSLICE(sl), val, str, FMTSLICE(sl), val);
    CHECK(v->toJSONString() == R"({"d":[]})");
}


TEST_CASE("Builder Default Suppression", "[Builder]") {
    const char *str = "";
    slice sl(str);
    auto v = builder::Build("[%-c, %-d, %-f, %-s, %-.*s]",
                            char(false), 0, 0.0, str, FMTSLICE(sl));
    CHECK(v->toJSONString() == R"([])");
}


#ifdef __APPLE__
TEST_CASE("Builder CoreFoundation Params", "[Builder]") {
    CFStringRef str = CFSTR("Zegpold");
    int i = 12345678;
    CFNumberRef n = CFNumberCreate(nullptr, kCFNumberIntType, &i);
    auto v = builder::BuildCF(CFSTR("[%@, %@]"),
                              str, n);
    CHECK(v->toJSONString() == R"(["Zegpold",12345678])");
}
#endif // __APPLE__
