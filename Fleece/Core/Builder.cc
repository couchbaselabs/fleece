//
// Builder.cc
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

#include "Builder.hh"
#include "FleeceException.hh"
#include "JSON5.hh"
#include "MutableDict.hh"
#include "MutableArray.hh"
#include "NumConversion.hh"
#include "slice_stream.hh"
#include <ctype.h>
#include <sstream>

#ifdef __APPLE__
#include "fleece/Fleece+CoreFoundation.h"
#endif

namespace fleece::impl::builder {
    using namespace std;


    class Builder {
    public:

        Builder(slice formatString, va_list args)
        :_format(formatString)
        ,_in(_format)
        {
            va_copy(_args, args);
        }


        // Parses the format, interpolates args, and returns a new mutable Array or Dict.
        RetainedConst<Value> buildValue() {
            switch (peekToken()) {
                case '[': {
                    Retained<MutableArray> array = MutableArray::newArray();
                    _buildInto(array);
                    finished();
                    return array.get();
                }
                case '{': {
                    Retained<MutableDict> dict = MutableDict::newDict();
                    _buildInto(dict);
                    finished();
                    return dict.get();
                }
                default:
                    fail("only '{...}' or '[...]' allowed at top level");
            }
        }


        void buildInto(MutableDict *dict) {
            if (peekToken() != '{')
                fail("expected '{'");
            _buildInto(dict);
        }


        void buildInto(MutableArray *array) {
            if (peekToken() != '[')
                fail("expected '['");
            _buildInto(array);
        }


    protected:
        // Parses a Fleece value from the input and stores it in the ValueSlot.
        // Recognizes a '%' specifier, and calls `putParameter` to read the value from the args.
        const bool _buildValue(ValueSlot &inSlot) {
            switch (peekToken()) {
                case '[': {
                    Retained<MutableArray> array = MutableArray::newArray();
                    _buildInto(array);
                    inSlot.set(array);
                    break;
                }
                case '{': {
                    Retained<MutableDict> dict = MutableDict::newDict();
                    _buildInto(dict);
                    inSlot.set(dict);
                    break;
                }
                case 'n':
                    readIdentifier("null");
                    inSlot.set(nullValue);
                    break;
                case 't':
                    readIdentifier("true");
                    inSlot.set(true);
                    break;
                case 'f':
                    readIdentifier("false");
                    inSlot.set(false);
                    break;
                case '-':
                case '+':
                case '.':
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    readLiteralNumber(inSlot);
                    break;
                case '"':
                case '\'':
                    inSlot.set(readLiteralString());
                    break;
                case '%':
                    get();
                    return putParameter(inSlot);
                default:
                    fail("invalid start of value");
            }
            return true;
        }


        // Parses a JSON5 object from the input and adds its entries to `dict`.
        void _buildInto(MutableDict *dict) {
            get();      // skip the opening '{' *without verifying*
            char c;
            while ('}' != (c = peekToken())) {
                // Scan key:
                string key;
                if (c == '"' || c == '\'') {
                    // Key string:
                    key = readLiteralString();
                } else if (isalpha(c) || c == '_' || c == '$') {
                    // JSON5 unquoted key:
                    key = string(readIdentifier());
                } else {
                    fail("expected dict key");
                }

                if (peekToken() != ':')
                    fail("expected ':' after dict key");
                get();

                // Value:
                if (!_buildValue(dict->setting(key)))
                    dict->remove(key);

                if (peekToken() == ',')         // Note: JSON5 allows trailing `,` before `}`
                    get();
                else if (peekToken() != '}')
                    fail("unexpected token after dict item");
            }
            get(); // eat close bracket/brace
        }


        // Parses a JSON5 array from the input and adds its entries to `dict`.
        void _buildInto(MutableArray *array) {
            get();      // skip the opening '[' *without verifying*
            while (peekToken() != ']') {
                if (!_buildValue(array->appending()))
                    array->remove(array->count() - 1, 1);

                if (peekToken() == ',')         // Note: JSON5 allows trailing `,` before `]`
                    get();
                else if (peekToken() != ']')
                    fail("unexpected token after array item");
            }
            get(); // eat close bracket/brace
        }


#pragma mark - PARAMETER SUBSTITUTION:


        // This is where those crazy printf format specs get parsed.
        bool putParameter(ValueSlot &inSlot) {
            char c = get();
            // `-` means to skip this arg if it has a default value:
            bool skipDefault = (c == '-');
            if (skipDefault)
                c = get();

            // Size specifier:
            char size = ' ';
            if (c == 'l' || c == 'q' || c == 'z') {
                size = c;
                c = get();
                if (size == 'l' && c == 'l') {
                    size = 'q';
                    c = get();
                }
            }

            switch (c) {
                case 'c': case 'b': {
                    // Bool:
                    bool param = va_arg(_args, int) != 0;
                    if (skipDefault && !param)
                        return false;
                    inSlot.set(param);
                    break;
                }
                case 'd': case 'i': {
                    // Signed integers:
                    int64_t param;
                    if (size == 'q')
                        param = va_arg(_args, long long);
                    else if (size == 'z')
                        param = va_arg(_args, ptrdiff_t);
                    else if (size == 'l')
                        param = va_arg(_args, long);
                    else
                        param = va_arg(_args, int);
                    if (skipDefault && param == 0)
                        return false;
                    inSlot.set(param);
                    break;
                }
                case 'u': {
                    // Unsigned integers:
                    uint64_t param;
                    if (size == 'q')
                        param = va_arg(_args, unsigned long long);
                    else if (size == 'z')
                        param = va_arg(_args, size_t);
                    else if (size == 'l')
                        param = va_arg(_args, unsigned long);
                    else
                        param = va_arg(_args, unsigned int);
                    if (skipDefault && param == 0)
                        return false;
                    inSlot.set(param);
                    break;
                }
                case 'f': {
                    // Floats:
                    double param = va_arg(_args, double);
                    if (skipDefault && param == 0.0)
                        return false;
                    inSlot.set(param);
                    break;
                }
                case 's': {
                    // C string:
                    slice param(va_arg(_args, const char*));
                    if (!param || (skipDefault && param.empty()))
                        return false;
                    inSlot.set(param);
                    break;
                }
                case '.': {
                    // Slice ("%.*s") -- takes 2 args: the start and size (see FMTSLICE() macro)
                    if (get() != '*' || get() != 's')
                        fail("'.' qualifier only supported in '%.*s'");
                    int len = va_arg(_args, int);
                    auto str = va_arg(_args, void*);
                    if (!str || (skipDefault && len == 0))
                        return false;
                    inSlot.set(slice(str, len));
                    break;
                }
                case 'p': {
                    // "%p" is a Fleece value:
                    auto param = va_arg(_args, const Value*);
                    if (!param)
                        return false;
                    inSlot.set(param);
                    break;
                }
#if __APPLE__
                case '@': {
                    // "%@" substitutes an Objective-C or CoreFoundation object.
                    auto param = va_arg(_args, CFTypeRef);
                    if (!param)
                        return false;
                    FLSlot_SetCFValue(FLSlot(&inSlot), param);
                    return true;
                }
#endif
                default:
                    fail("unknown '%' format specifier");
            }
            return true;
        }


#pragma mark - LITERALS:


        // Reads a numeric literal, storing it in the ValueSlot.
        void readLiteralNumber(ValueSlot &inSlot) {
            // Scan to the end of the number:
            // (If the NumConversion.hh API used slice_istream I wouldn't have to do the scan)
            auto start = _in.next();
            bool isNegative = (peek() == '-');
            if (isNegative || peek() == '+')
                get();
            bool isFloat = false;

            char c;
            do {
                c = get();
                if (c == '.' || c == 'e' || c == 'E')
                    isFloat = true;
            } while (isdigit(c) || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+');
            unget();
            auto numStr = string(slice(start, _in.next()));

            if (isFloat) {
                double n;
                if (ParseDouble(numStr.c_str(), n, false)) {
                    inSlot.set(n);
                    return;
                }
            } else if (isNegative) {
                int64_t i;
                if (ParseInteger(numStr.c_str(), i, false)) {
                    inSlot.set(i);
                    return;
                }
            } else {
                uint64_t u;
                if (ParseUnsignedInteger(numStr.c_str(), u, false)) {
                    inSlot.set(u);
                    return;
                }
            }
            fail(("Invalid numeric literal " + numStr).c_str());
        }


        // Reads a string literal in JSON5 format, returning its value
        string readLiteralString() {
            string out;
            const char quote = get();               // single or double-quote
            char c;
            while (quote != (c = get())) {
                if (c == '\\') {
                    switch ((c = get())) {
                        case 'n':   c = '\n'; break;
                        case 'r':   c = '\r'; break;
                        case 't':   c = '\n'; break;
                        case 'u':   fail("Unicode escapes not supported");
                        // default is to leave c alone
                    }
                } else if (c < ' ') {
                    fail("control character in string literal");
                }
                out += c;
            }
            return out;
        }


#pragma mark - LEXER:


        // Reads alphanumeric characters, returning the identifier as a string.
        // (The 1st char is accepted even if not alphanumeric, on the assumption the caller already
        // peeked at and approved it.)
        slice readIdentifier() {
            auto start = _in.next();
            get(); // consume the char the caller peeked
            while (true) {
                char c = peek();
                if (isalnum(c) || c == '_')
                    get();
                else
                    break;
            }
            return slice(start, _in.next());
        }


        // Reads an identifier and fails if it isn't equal to `expected`.
        void readIdentifier(slice expected) {
            if (readIdentifier() != expected)
                fail("unknown identifier");
        }


        // Reads & ignores a JSON5 comment.
        void skipComment() {
            char c;
            get(); // consume initial '/'
            switch (get()) {
                case '/':
                    do {
                        c = peek();
                        if (c)
                            get();
                    } while (c != 0 && c != '\n' && c != '\r');
                    break;
                case '*': {
                    bool star;
                    c = 0;
                    do {
                        star = (c == '*');
                        c = get();
                    } while (!(star && c=='/'));
                    break;
                }
                default:
                    fail("syntax error");
            }
        }


        // Fails if anything remains in the input but whitespace.
        void finished() {
            if (peekToken() != 0)
                fail("unexpected characters after end of spec");
        }


        // Skips any whitespace and JSON5 comments, then returns a peek at the next character.
        char peekToken() {
            while (true) {
                char c = peek();
                if (c == 0) {
                    return c; // EOF
                } else if (isspace(c)) {
                    get(); // skip whitespace
                } else if (c == '/') {
                    skipComment();
                } else {
                    return c;
                }
            }
        }


        // Returns the next character from the input without consuming it, or 0 at EOF.
        char peek() {
            return _in.peekByte();
        }


        // Reads the next character from the input. Fails if input is at EOF.
        char get() {
            if (_in.eof())
                fail("unexpected end");
            return _in.readByte();
        }


        void unget() {
            _in.unreadByte();
        }


        // Throws an exception.
        [[noreturn]]
        void fail(const char *error) {
            slice prefix = _format.upTo(_in.next()), suffix = _format.from(_in.next());
            FleeceException::_throw(InvalidData, "Build(): %s in format: %.*s💥%.*s",
                                    error, FMTSLICE(prefix), FMTSLICE(suffix));
        }


    private:
        slice const     _format;    // The entire format string
        slice_istream   _in;        // Stream for reading _format
        va_list         _args;      // The caller-provided arguments
    };


#pragma mark - PUBLIC API:


    RetainedConst<Value> VBuild(const char *format, va_list args) {
        return Builder(format, args).buildValue();
    }

    RetainedConst<Value> VBuild(slice format, va_list args) {
        return Builder(format, args).buildValue();
    }


    RetainedConst<Value> Build(const char *format, ...) {
        va_list args;
        va_start(args, format);
        auto result = VBuild(format, args);
        va_end(args);
        return result;
    }


#ifdef __APPLE__
    RetainedConst<Value> BuildCF(CFStringRef cfFormat, ...) {
        va_list args;
        va_start(args, cfFormat);
        auto result = VBuild(nsstring_slice(cfFormat), args);
        va_end(args);
        return result;
    }
#endif


    void Put(MutableArray *array, const char *format, ...) {
        va_list args;
        va_start(args, format);
        Builder(format, args).buildInto(array);
        va_end(args);
    }


    void Put(MutableDict *dict, const char *format, ...) {
        va_list args;
        va_start(args, format);
        Builder(format, args).buildInto(dict);
        va_end(args);
    }

    void VPut(Value *v, const char *format, va_list args) {
        Builder builder(format, args);
        if (const Dict *dict = v->asDict()) {
            MutableDict *mutableDict = dict->asMutable();
            assert(mutableDict);
            builder.buildInto(mutableDict);
        } else {
            MutableArray *mutableArray = v->asArray()->asMutable();
            assert(mutableArray);
            builder.buildInto(mutableArray);
        }
    }

}
