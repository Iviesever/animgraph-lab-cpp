#pragma once

#if __has_include(<expected>)
#include <expected>
#endif

#include <optional>
#include <utility>
#include <variant>

namespace animgraph {

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

template <class T, class E>
using Expected = std::expected<T, E>;

template <class E>
[[nodiscard]] auto make_unexpected(E error) {
  return std::unexpected<E>{std::move(error)};
}

#else

template <class E>
struct Unexpected {
  E error;
};

template <class E>
[[nodiscard]] Unexpected<E> make_unexpected(E error) {
  return Unexpected<E>{std::move(error)};
}

template <class T, class E>
class Expected {
 public:
  Expected(const T& value) : storage_(value) {}
  Expected(T&& value) : storage_(std::move(value)) {}
  Expected(Unexpected<E> error) : storage_(std::move(error.error)) {}

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] T& value() & { return std::get<0>(storage_); }
  [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
  [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }
  [[nodiscard]] E& error() & { return std::get<1>(storage_); }
  [[nodiscard]] const E& error() const& { return std::get<1>(storage_); }
  [[nodiscard]] T& operator*() & { return value(); }
  [[nodiscard]] const T& operator*() const& { return value(); }
  [[nodiscard]] T* operator->() { return &value(); }
  [[nodiscard]] const T* operator->() const { return &value(); }

 private:
  std::variant<T, E> storage_;
};

template <class E>
class Expected<void, E> {
 public:
  Expected() = default;
  Expected(Unexpected<E> error) : error_(std::move(error.error)) {}

  [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  void value() const {}
  [[nodiscard]] E& error() & { return *error_; }
  [[nodiscard]] const E& error() const& { return *error_; }

 private:
  std::optional<E> error_;
};

#endif

}  // namespace animgraph
