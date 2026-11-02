// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file box.hpp
/// Box<T> / OptionalBox<T>: heap indirection used by generated code where XDR
/// types are recursively defined (e.g. SCSpecTypeOption contains SCSpecTypeDef).
///
/// Box always logically holds a value; a moved-from Box reads as a default
/// value instead of dereferencing null. OptionalBox is the recursive variant
/// of XDR `T*` (present/absent).

#include <memory>
#include <optional>
#include <utility>

namespace stellar::inline v1::xdr {

template <typename T>
class Box {
 public:
  Box() : ptr_(std::make_unique<T>()) {}
  explicit Box(const T& v) : ptr_(std::make_unique<T>(v)) {}
  explicit Box(T&& v) : ptr_(std::make_unique<T>(std::move(v))) {}
  Box(const Box& o) : ptr_(o.ptr_ ? std::make_unique<T>(*o.ptr_) : nullptr) {}
  Box(Box&&) noexcept = default;
  Box& operator=(const Box& o) {
    if (this != &o) ptr_ = o.ptr_ ? std::make_unique<T>(*o.ptr_) : nullptr;
    return *this;
  }
  Box& operator=(Box&&) noexcept = default;
  ~Box() = default;

  [[nodiscard]] T& get() {
    if (!ptr_) ptr_ = std::make_unique<T>();
    return *ptr_;
  }
  [[nodiscard]] const T& get() const noexcept { return ptr_ ? *ptr_ : default_instance(); }
  [[nodiscard]] T& operator*() noexcept { return get(); }
  [[nodiscard]] const T& operator*() const noexcept { return get(); }
  [[nodiscard]] T* operator->() noexcept { return &get(); }
  [[nodiscard]] const T* operator->() const noexcept { return &get(); }

  bool operator==(const Box& o) const { return get() == o.get(); }

 private:
  static T& default_instance() {
    static T* kDefault = new T();
    return *kDefault;
  }
  std::unique_ptr<T> ptr_;
};

/// `T*` where T participates in a type recursion.
template <typename T>
using OptionalBox = std::optional<Box<T>>;

}  // namespace stellar::inline v1::xdr
