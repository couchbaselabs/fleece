//
// RefCounted.hh
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

#pragma once
#include "PlatformCompat.hh"
#include <algorithm>
#include <atomic>
#include <utility>

namespace fleece {

    /** Simple thread-safe ref-counting implementation.
        `RefCounted` objects should be managed by \ref Retained smart-pointers:
        `Retained<Foo> foo = new Foo(...)` or `auto foo = make_retained<Foo>(...)`.
        \note The ref-count starts at 0, so you must call retain() on an instance, or assign it
        to a Retained, right after constructing it. */
    class RefCounted {
    public:
        RefCounted()                            =default;
        
        int refCount() const FLPURE             {return _refCount;}

    protected:
        RefCounted(const RefCounted &)          { }

        /** Destructor is accessible only so that it can be overridden.
            **Never call `delete`**, only `release`! Overrides should be made protected or private. */
        virtual ~RefCounted();

    private:
        template <typename T>
        friend T* retain(T*) noexcept;
        friend void release(const RefCounted*) noexcept;
        friend void assignRef(RefCounted* &dst, RefCounted *src) noexcept;

#if DEBUG
        void _retain() const noexcept           {_careful_retain();}
        void _release() const noexcept          {_careful_release();}
#else
        ALWAYS_INLINE void _retain() const noexcept   { ++_refCount; }
        void _release() const noexcept;
#endif

        static constexpr int32_t kCarefulInitialRefCount = -6666666;
        void _careful_retain() const noexcept;
        void _careful_release() const noexcept;

        mutable std::atomic<int32_t> _refCount
#if DEBUG
                                               {kCarefulInitialRefCount};
#else
                                               {0};
#endif
    };


    /** Retains a RefCounted object and returns the object. Does nothing given a null pointer.
        (See also `retain(Retained&&)`, below.)
        \warning Manual retain/release is error prone. This function is intended mostly for interfacing
        with C code that can't use \ref Retained. */
    template <typename REFCOUNTED>
    ALWAYS_INLINE REFCOUNTED* retain(REFCOUNTED *r) noexcept {
        if (r) r->_retain();
        return r;
    }

    /** Releases a RefCounted object. Does nothing given a null pointer.
        \warning Manual retain/release is error prone. This function is intended mostly for interfacing
        with C code that can't use \ref Retained. */
    NOINLINE void release(const RefCounted *r) noexcept;

    
    // Used internally by Retained's operator=. Marked noinline to prevent code bloat.
    NOINLINE void assignRef(RefCounted* &holder, RefCounted *newValue) noexcept;

    // Makes `assignRef` polymorphic with RefCounted subclasses.
    template <typename T>
    static inline void assignRef(T* &holder, RefCounted *newValue) noexcept {
        assignRef((RefCounted*&)holder, newValue);
    }



    /** A smart pointer that retains the RefCounted instance it holds. */
    template <typename T>
    class Retained {
    public:
        Retained() noexcept                             :_ref(nullptr) { }
        Retained(T *t) noexcept                         :_ref(retain(t)) { }

        Retained(const Retained &r) noexcept            :_ref(retain(r._ref)) { }
        Retained(Retained &&r) noexcept                 :_ref(std::move(r).detach()) { }

        template <typename U>
        Retained(const Retained<U> &r) noexcept         :_ref(retain(r._ref)) { }

        template <typename U>
        Retained(Retained<U> &&r) noexcept              :_ref(std::move(r).detach()) { }

        ~Retained()                                     {release(_ref);}

        operator T* () const & noexcept FLPURE STEPOVER {return _ref;}
        T* operator-> () const noexcept FLPURE STEPOVER {return _ref;}
        T* get() const noexcept FLPURE STEPOVER         {return _ref;}

        explicit operator bool () const FLPURE          {return (_ref != nullptr);}

        Retained& operator=(T *t) noexcept              {assignRef(_ref, t); return *this;}

        Retained& operator=(const Retained &r) noexcept {return *this = r._ref;}

        template <typename U>
        Retained& operator=(const Retained<U> &r) noexcept {return *this = r._ref;}

        Retained& operator= (Retained &&r) noexcept {
            // Unexpectedly, the simplest & most efficient way to implement this is by simply
            // swapping the refs, instead of the commented-out code below.
            // The reason this works is that `r` is going to get destructed anyway when it goes
            // out of scope in the caller's stack frame, and at that point it will contain my
            // previous `_ref`, ensuring it gets cleaned up.
            std::swap(_ref, r._ref);
            // Older code:
            //   auto oldRef = _ref;
            //   _ref = std::move(r).detach();
            //   release(oldRef);
            return *this;
        }

        template <typename U>
        Retained& operator= (Retained<U> &&r) noexcept {
            auto oldRef = _ref;
            if (oldRef != r._ref) { // necessary to avoid premature release
                _ref = std::move(r).detach();
                release(oldRef);
            }
            return *this;
        }

        /// Converts a Retained into a raw pointer with a +1 reference that must be released.
        /// Used in C++ functions that bridge to C and return C references.
        T* detach() && noexcept                  {auto r = _ref; _ref = nullptr; return r;}

        // The operator below is often a dangerous mistake, so it's deliberately made impossible.
        // It happens in these sorts of contexts, where it can produce a dangling pointer to a
        // deleted object:
        //      Retained<Foo> createFoo();
        //      ...
        //      Foo *foo = createFoo();     // ☠️
        // or:
        //      return createFoo();         // ☠️
        //
        // However, it _is_ valid if you're passing the Retained r-value as a function parameter,
        // since it will not be released until after the function returns:
        //      void handleFoo(Foo*);
        //      ...
        //      handleFoo( createFoo() );           // Would be OK, but prohibited due to the above
        // In this case you can use an explicit `get()` to work around the prohibition:
        //      handleFoo( createFoo().get() );     // OK!
        // ...or promote it to an l-value:
        //      Retained<Foo> foo = createFoo();
        //      handleFoo(foo);                     // OK!
        // ...or change `handleFoo`s parameter to Retained:
        //      void handleFoo(Retained<Foo>);
        //      ...
        //      handleFoo( createFoo() );           // OK!
        operator T* () const && =delete; // see above^

    private:
        template <class U> friend class Retained;
        template <class U> friend class RetainedConst;
        template <class U> friend Retained<U> adopt(U*) noexcept;

        Retained(T *t, bool) noexcept                   :_ref(t) { } // private no-retain ctor

        T *_ref;
    };


    /** Same as Retained, but when you only have a const pointer to the object. */
    template <typename T>
    class RetainedConst {
    public:
        RetainedConst() noexcept                        :_ref(nullptr) { }
        RetainedConst(const T *t) noexcept              :_ref(retain(t)) { }
        RetainedConst(const RetainedConst &r) noexcept  :_ref(retain(r._ref)) { }
        RetainedConst(RetainedConst &&r) noexcept       :_ref(std::move(r).detach()) { }
        RetainedConst(const Retained<T> &r) noexcept    :_ref(retain(r._ref)) { }
        RetainedConst(Retained<T> &&r) noexcept         :_ref(std::move(r).detach()) { }
        ALWAYS_INLINE ~RetainedConst()                  {release(_ref);}

        operator const T* () const & noexcept FLPURE STEPOVER   {return _ref;}
        const T* operator-> () const noexcept FLPURE STEPOVER   {return _ref;}
        const T* get() const noexcept FLPURE STEPOVER           {return _ref;}

        RetainedConst& operator=(const T *t) noexcept {
            auto oldRef = _ref;
            _ref = retain(t);
            release(oldRef);
            return *this;
        }

        RetainedConst& operator=(const RetainedConst &r) noexcept {
            return *this = r._ref;
        }

        RetainedConst& operator= (RetainedConst &&r) noexcept {
            std::swap(_ref, r._ref);
            return *this;
        }

        template <typename U>
        RetainedConst& operator=(const Retained<U> &r) noexcept {
            return *this = r._ref;
        }

        template <typename U>
        RetainedConst& operator= (Retained<U> &&r) noexcept {
            std::swap(_ref, r._ref);
            return *this;
        }

        const T* detach() && noexcept                   {auto r = _ref; _ref = nullptr; return r;}

        operator const T* () const && =delete; // Usually a mistake; see above under Retained

    private:
        const T *_ref;
    };


    /** Easy instantiation of a ref-counted object: `auto f = retained(new Foo());`*/
    template <typename REFCOUNTED>
    inline Retained<REFCOUNTED> retained(REFCOUNTED *r) noexcept {
        return Retained<REFCOUNTED>(r);
    }

    /** Easy instantiation of a const ref-counted object: `auto f = retained(new Foo());`*/
    template <typename REFCOUNTED>
    inline RetainedConst<REFCOUNTED> retained(const REFCOUNTED *r) noexcept {
        return RetainedConst<REFCOUNTED>(r);
    }

    /** Converts a raw pointer with a +1 reference into a Retained object.
        This has no effect on the object's ref-count; the existing +1 ref will be released when the
        Retained destructs. */
    template <typename REFCOUNTED>
    inline Retained<REFCOUNTED> adopt(REFCOUNTED *r) noexcept {
        return Retained<REFCOUNTED>(r, false);
    }



    /** make_retained<T>(...) is equivalent to `std::make_unique` and `std::make_shared`.
        It constructs a new RefCounted object, passing params to the constructor, returning a `Retained`. */
    template<class T, class... _Args>
    static inline Retained<T>
    make_retained(_Args&&... __args) {
        return Retained<T>(new T(std::forward<_Args>(__args)...));
    }


    /** Extracts the pointer from a Retained. It must later be released via `release`.
        This is used in bridging functions that return a direct pointer for a C API. */
    template <typename REFCOUNTED>
    ALWAYS_INLINE REFCOUNTED* retain(Retained<REFCOUNTED> &&retained) noexcept {
        return std::move(retained).detach();
    }

    /** Extracts the pointer from a RetainedConst. It must later be released via `release`.
        This is used in bridging functions that return a direct pointer for a C API. */
    template <typename REFCOUNTED>
    ALWAYS_INLINE const REFCOUNTED* retain(RetainedConst<REFCOUNTED> &&retained) noexcept {
        return std::move(retained).detach();
    }

}
