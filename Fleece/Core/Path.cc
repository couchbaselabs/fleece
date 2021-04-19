//
// Path.cc
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

#include "Path.hh"
#include "SharedKeys.hh"
#include "FleeceException.hh"
#include "PlatformCompat.hh"
#include "slice_stream.hh"
#include <iostream>
#include <sstream>

using namespace std;

namespace fleece { namespace impl {

    void Path::addComponents(slice components) {
        forEachComponent(components, _path.empty(), [&](char token, slice component, int32_t index) {
            if (token == '.')
                _path.emplace_back(component);
            else
                _path.emplace_back(index);
            return true;
        });
    }


    void Path::addProperty(slice key) {
        throwIf(key.size == 0, PathSyntaxError, "Illegal empty property name");
        _path.emplace_back(key);
    }


    void Path::addIndex(int index) {
        _path.emplace_back(index);
    }

    
    Path& Path::operator += (const Path &other) {
        _path.reserve(_path.size() + other.size());
        for (auto &elem : other._path)
            _path.push_back(elem);
        return *this;
    }


    void Path::drop(size_t startAt) {
        _path.erase(_path.begin(), _path.begin() + startAt);
    }


    bool Path::operator== (const Path &other) const {
        return _path == other._path;
    }


#pragma mark - ENCODING:


    Path::operator std::string() {
        stringstream out;
        writeTo(out);
        return out.str();
    }


    void Path::writeTo(std::ostream &out) const {
        bool first = true;
        for (auto &element : _path) {
            if (element.isKey())
                writeProperty(out, element.key().string(), first);
            else
                writeIndex(out, element.index());
            first = false;
        }
    }


    void Path::writeProperty(std::ostream &out, slice key, bool first) {
        if (first) {
            if (key.hasPrefix('$'))
                out << '\\';
        } else {
            out << '.';
        }
        const uint8_t *toQuote;
        while (nullptr != (toQuote = key.findAnyByteOf(".[\\"_sl))) {
            out.write((const char *)key.buf, toQuote - (const uint8_t*)key.buf);
            out << '\\' << *toQuote;
            key.setStart(toQuote + 1);
        }
        out.write((const char *)key.buf, key.size);
    }


    void Path::writeIndex(std::ostream &out, int index) {
        out << '[' << index << ']';
    }


#pragma mark - EVALUATION:


    const Value* Path::eval(const Value *root) const noexcept {
        const Value *item = root;
        if (_usuallyFalse(!item))
            return nullptr;
        for (auto &e : _path) {
            item = e.eval(item);
            if (!item)
                break;
        }
        return item;
    }


    /*static*/ const Value* Path::eval(slice specifier, const Value *root) {
        const Value *item = root;
        if (_usuallyFalse(!item))
            return nullptr;
        forEachComponent(specifier, true, [&](char token, slice component, int32_t index) {
            item = Element::eval(token, component, index, item);
            return (item != nullptr);
        });
        return item;
    }


    /*static*/ const Value* Path::evalJSONPointer(slice specifier, const Value *root)
    {
        slice_istream in(specifier);
        auto current = root;
        throwIf(in.readByte() != '/', PathSyntaxError, "JSONPointer does not start with '/'");
        while (!in.eof()) {
            if (!current)
                return nullptr;

            auto slash = in.findByteOrEnd('/');
            slice_istream param(in.buf, slash);

            switch(current->type()) {
                case kArray: {
                    auto i = param.readDecimal();
                    if (_usuallyFalse(param.size > 0 || i > INT32_MAX))
                        FleeceException::_throw(PathSyntaxError, "Invalid array index in JSONPointer");
                    current = ((const Array*)current)->get((uint32_t)i);
                    break;
                }
                case kDict: {
                    string key = param.asString();
                    current = ((const Dict*)current)->get(key);
                    break;
                }
                default:
                    current = nullptr;
                    break;
            }

            if (slash == in.end())
                break;
            in.setStart(slash+1);
        }
        return current;
    }


#pragma mark - PARSING:


    // Parses a path expression, calling the callback for each property or array index.
    void Path::forEachComponent(slice specifier, bool atStart, eachComponentCallback callback) {
        slice_istream in(specifier);
        throwIf(in.size == 0, PathSyntaxError, "Empty path");
        throwIf(in[in.size-1] == '\\', PathSyntaxError, "'\\' at end of string");

        uint8_t token = in.peekByte();
        if (token == '$') {
            // Starts with "$." or "$["
            throwIf(!atStart, PathSyntaxError, "Illegal $ in path");
            in.skip(1);
            if (in.size == 0)
                return;                 // Just "$" means the root
            token = in.readByte();
            throwIf(token != '.' && token != '[', PathSyntaxError, "Invalid path delimiter after $");
        } else if (token == '[' || token == '.') {
            // Starts with "[" or "."
            in.skip(1);
        } else if (token == '\\') {
            // First character of path is escaped (probably a '$' or '.' or '[')
            if (in[1] == '$')
                in.skip(1);
            token = '.';
        } else {
            // Else starts with a property name
            token = '.';
        }

        if (in.size == 0 && token == '.')
            return;                     // "." or "" mean the root

        while (true) {
            // Read parameter (property name or array index):
            const uint8_t* next;
            slice param;
            alloc_slice unescaped;
            int32_t index = 0;

            if (token == '.') {
                // Find end of property name:
                next = in.findAnyByteOf(".[\\"_sl);
                if (next == nullptr) {
                    param = in;
                    next = (const uint8_t*)in.end();
                } else if (*next != '\\') {
                    param = slice(in.buf, next);
                } else {
                    // Name contains escapes -- need to unescape it:
                    unescaped.reset(in.size);
                    auto dst = (uint8_t*)unescaped.buf;
                    for (next = (const uint8_t*)in.buf; next < in.end(); ++next) {
                        uint8_t c = *next;
                        if (c == '\\') {
                            c = *++next;
                        } else if(c == '.' || c == '[') {
                            break;
                        }

                        *dst++ = c;
                    }
                    param = slice(unescaped.buf, dst);
                }

            } else if (token == '[') {
                // Find end of array index:
                next = in.findByteOrEnd(']');
                if (!next)
                    FleeceException::_throw(PathSyntaxError, "Missing ']'");
                param = slice(in.buf, next++);
                // Parse array index:
                slice_istream n = param;
                int64_t i = n.readSignedDecimal();
                throwIf(param.size == 0 || n.size > 0 || i > INT32_MAX || i < INT32_MIN,
                        PathSyntaxError, "Invalid array index");
                index = (int32_t)i;
            } else {
                FleeceException::_throw(PathSyntaxError, "Invalid path component");
            }

            if (param.size > 0) {
                // Invoke the callback:
                if (_usuallyFalse(!callback(token, param, index)))
                    return;
            }

            // Did we read the whole expression?
            if (next >= in.end())
                break;              // LOOP EXIT

            // Read the next token and go round again…
            token = *next;
            in.setStart(next+1);
        }
    }


#pragma mark - ELEMENT CLASS:


    Path::Element::Element(slice property)
    :_keyBuf(property)
    ,_key(new Dict::key(_keyBuf))
    { }


    Path::Element::Element(const Element &other)
    :_keyBuf(other._keyBuf)
    ,_index(other._index)
    {
        if (other._key)
            _key.reset(new Dict::key(_keyBuf));
    }


    bool Path::Element::operator== (const Element &e) const {
        return _key ? (_key == e._key) : (_index == e._index);
    }


    const Value* Path::Element::eval(const Value *item) const noexcept {
        if (_key) {
            auto d = item->asDict();
            if (_usuallyFalse(!d))
                return nullptr;
            return d->get(*_key);
        } else {
            return getFromArray(item, _index);
        }
    }

    /*static*/ const Value* Path::Element::eval(char token, slice comp, int32_t index,
                                                const Value *item) noexcept {
        if (token == '.') {
            auto d = item->asDict();
            if (_usuallyFalse(!d))
                return nullptr;
            return d->get(comp);
        } else {
            return getFromArray(item, index);
        }
    }


    const Value* Path::Element::getFromArray(const Value* item, int32_t index) noexcept {
        auto a = item->asArray();
        if (_usuallyFalse(!a))
            return nullptr;
        if (index < 0) {
            uint32_t count = a->count();
            if (_usuallyFalse((uint32_t)-index > count))
                return nullptr;
            index += count;
        }
        return a->get((uint32_t)index);
    }

} }
