//
// HeapDict.cc
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

#include "HeapDict.hh"
#include "HeapArray.hh"
#include "ValueSlot.hh"
#include "MutableDict.hh"

namespace fleece { namespace internal {

    HeapDict::HeapDict(const Dict *d)
    :HeapCollection(kDictTag)
    ,_source(d)
    ,_count(d ? d->count() : 0)
    { }


    void HeapDict::markChanged() {
        setChanged(true);
        _iterable = nullptr;
    }


    ValueSlot* HeapDict::_findValueFor(slice key) const noexcept {
        auto it = _map.find(key);
        if (it == _map.end())
            return nullptr;
        return const_cast<ValueSlot*>(&it->second);
    }


    ValueSlot& HeapDict::_makeValueFor(slice key) {
        // Look in my map first:
        auto it = _map.find(key);
        if (it != _map.end())
            return it->second;
        // If not in map, add it as an empty value:
        alloc_slice allocedKey(key);
        _backingSlices.push_back(allocedKey);
        return _map[allocedKey];                // creates a new value
    }


    ValueSlot& HeapDict::_mutableValueToSetFor(slice key) {
        auto &val = _makeValueFor(key);
        if (!val && !(_source && _source->get(key)))
            ++_count;
        markChanged();
        return val;
    }


    const Value* HeapDict::get(slice key) const noexcept {
        ValueSlot* val = _findValueFor(key);
        if (val)
            return val->asValue();
        else
            return _source ? _source->get(key) : nullptr;
    }


    HeapCollection* HeapDict::getMutable(slice key, tags ifType) {
        Retained<HeapCollection> result;
        ValueSlot* mval = _findValueFor(key);
        if (mval) {
            result = mval->makeMutable(ifType);
        } else if (_source) {
            result = HeapCollection::mutableCopy(_source->get(key), ifType);
            if (result)
                _map.emplace(key, result);
        }
        if (result)
            markChanged();
        return result;
    }


    void HeapDict::remove(slice key) {
        if (_source && _source->get(key)) {
            auto &val = _makeValueFor(key);
            if (_usuallyFalse(!val))
                return;                             // already removed
            val = ValueSlot();
        } else {
            if (_usuallyFalse(!_map.erase(key)))    //OPT: Should remove it from _backingSlices too
                return;
        }
        --_count;
        markChanged();
    }


    void HeapDict::removeAll() {
        if (_count == 0)
            return;
        _map.clear();
        _backingSlices.clear();
        if (_source) {
            for (Dict::iterator i(_source); i; ++i)
                _makeValueFor(i.keyString());    // override source with empty values
        }
        _count = 0;
        markChanged();
    }


    HeapArray* HeapDict::kvArray() {
        if (!_iterable) {
            _iterable = new HeapArray(2*count());
            uint32_t n = 0;
            for (iterator i(this); i; ++i) {
                _iterable->set(n++, i.keyString());
                _iterable->set(n++, i.value());
            }
            assert(n == 2*_count);
        }
        return _iterable.get();
    }


#pragma mark - ITERATOR:


    HeapDict::iterator::iterator(const HeapDict *dict) noexcept
    :_sourceIter(dict->_source)
    ,_newIter(dict->_map.begin())
    ,_newEnd(dict->_map.end())
    ,_count(dict->count() + 1)
    {
        getSource();
        getNew();
        ++(*this);
    }

    HeapDict::iterator::iterator(const MutableDict *dict) noexcept
    :iterator((HeapDict*)HeapCollection::asHeapValue(dict))
    { }

    void HeapDict::iterator::getSource() {
        _sourceActive = (bool)_sourceIter;
        if (_usuallyTrue(_sourceActive))
            _sourceKey = _sourceIter.keyString();
    }

    void HeapDict::iterator::getNew() {
        _newActive = _newIter != _newEnd;
    }


    HeapDict::iterator& HeapDict::iterator::operator++() {
        // Since _source and _map are both sorted, this is basically just an array merge.
        // Special cases: both items might be equal, or the item from _map might be a tombstone.
        --_count;
        while (_usuallyTrue(_sourceActive || _newActive)) {
            if (!_newActive || (_sourceActive && _sourceKey < _newIter->first)) {
                // Key from _source is lower, so add its pair:
                _key = _sourceKey;
                _value = _sourceIter.value();
                ++_sourceIter;
                getSource();
                return *this;
            } else {
                bool exists = !!(_newIter->second);
                if (_usuallyTrue(exists)) {
                    // Key from _map is lower or equal, and its value exists, so add its pair:
                    _key = _newIter->first;
                    _value = _newIter->second.asValue();
                }
                if (_sourceActive && _sourceKey == _newIter->first) {
                    ++_sourceIter;
                    getSource();
                }
                ++_newIter;
                getNew();
                if (_usuallyTrue(exists))
                    return *this;
                // If the value doesn't exist, go around again to get one that does...
            }
        }
        // Value didn't exist, but no more values:
        _value = nullptr;
        return *this;
    }

} }