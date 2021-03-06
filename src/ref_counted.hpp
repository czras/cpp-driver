/*
  Copyright 2014 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef __CASS_REF_COUNTED_HPP_INCLUDED__
#define __CASS_REF_COUNTED_HPP_INCLUDED__

#include "common.hpp"
#include "macros.hpp"

#include <boost/atomic.hpp>

#include <assert.h>
#include <new>

#ifdef TESTING_DIRECTIVE
#include <stdexcept>
#endif

namespace cass {

struct RefCountedBase {};

template <class T>
class RefCounted : public RefCountedBase {
public:
  RefCounted()
      : ref_count_(0) {}

  int ref_count() const {
    return ref_count_.load(boost::memory_order_acquire);
  }

  void inc_ref() const {
    ref_count_.fetch_add(1, boost::memory_order_relaxed);
  }

  void dec_ref() const {
    int new_ref_count = ref_count_.fetch_sub(1, boost::memory_order_release);
    assert(new_ref_count >= 1);
    if (new_ref_count == 1) {
      boost::atomic_thread_fence(boost::memory_order_acquire);
      delete static_cast<const T*>(this);
    }
  }

private:
  mutable boost::atomic<int> ref_count_;
  DISALLOW_COPY_AND_ASSIGN(RefCounted);
};

class RefBuffer : public RefCounted<RefBuffer> {
public:
  static RefBuffer* create(size_t size) {
#if defined(WIN32) || defined(_WIN32)
#pragma warning(push)
#pragma warning(disable: 4291) //Invalid warning thrown RefBuffer has a delete function
#endif
    return new (size) RefBuffer();
#if defined(WIN32) || defined(_WIN32)
#pragma warning(pop)
#endif
  }

  char* data() {
    return copy_cast<RefBuffer*, char*>(this) + sizeof(RefBuffer);
  }

  void operator delete(void* ptr) {
    ::operator delete(ptr);
  }

private:
  RefBuffer() {}

  void* operator new(size_t size, size_t extra) {
    return ::operator new(size + extra);
  }

  DISALLOW_COPY_AND_ASSIGN(RefBuffer);
};

template<class T>
class SharedRefPtr {
public:
  explicit SharedRefPtr(T* ptr = NULL)
       : ptr_(ptr) {
    if (ptr_ != NULL) {
      ptr_->inc_ref();
    }
  }

  SharedRefPtr(const SharedRefPtr<T>& ref)
    : ptr_(NULL) {
    copy<T>(ref.ptr_);
  }

  template<class S>
  SharedRefPtr(const SharedRefPtr<S>& ref)
    : ptr_(NULL) {
    copy<S>(ref.ptr_);
  }

  SharedRefPtr<T>& operator=(const SharedRefPtr<T>& ref) {
    copy<T>(ref.ptr_);
    return *this;
  }

  template<class S>
  SharedRefPtr<S>& operator=(const SharedRefPtr<S>& ref) {
    copy<S>(ref.ptr_);
    return *this;
  }

  ~SharedRefPtr() {
    if (ptr_ != NULL) {
      ptr_->dec_ref();
    }
  }

  bool operator==(const T* ptr) {
    return ptr_ == ptr;
  }

  bool operator==(const SharedRefPtr<T>& ref) {
    return ptr_ == ref.ptr_;
  }

  void reset(T* ptr = NULL) {
    copy<T>(ptr);
  }

  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }
  operator bool() const { return ptr_ != NULL; }

private:
  template<class S>
  void copy(S* ptr) {
    if (ptr != NULL) {
      ptr->inc_ref();
    }
    T* temp = ptr_;
    ptr_ = static_cast<S*>(ptr);
    if (temp != NULL) {
      temp->dec_ref();
    }
  }

  T* ptr_;
};

template<class T>
class ScopedRefPtr {
public:
  typedef T type;

  explicit ScopedRefPtr(type* ptr = NULL)
       : ptr_(ptr) {
    if (ptr_ != NULL) {
      ptr_->inc_ref();
    }
  }

  ~ScopedRefPtr() {
    if (ptr_ != NULL) {
      ptr_->dec_ref();
    }
  }

  void reset(type* ptr = NULL) {
    if (ptr != NULL) {
      ptr->inc_ref();
    }
    type* temp = ptr_;
    ptr_ = ptr;
    if (temp != NULL) {
      temp->dec_ref();
    }
  }

  type* get() const { return ptr_; }
  type& operator*() const { return *ptr_; }
  type* operator->() const { return ptr_; }
  operator bool() const { return ptr_ != NULL; }

private:
  type* ptr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ScopedRefPtr);
};

} // namespace cass

#endif
