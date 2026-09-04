#pragma once

#include "animgraph/core/types.hpp"
#include "animgraph/core/expected.hpp"

#include <array>

namespace animgraph {

struct Vec3 {
  float x{};
  float y{};
  float z{};
  auto operator<=>(const Vec3&) const = default;
};

[[nodiscard]] Vec3 operator+(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 operator-(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 operator-(Vec3 value) noexcept;
[[nodiscard]] Vec3 operator*(Vec3 value, float scalar) noexcept;
[[nodiscard]] Vec3 operator*(float scalar, Vec3 value) noexcept;
[[nodiscard]] Vec3 operator/(Vec3 value, float scalar) noexcept;
[[nodiscard]] Vec3 hadamard(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] float length_squared(Vec3 value) noexcept;
[[nodiscard]] float length(Vec3 value) noexcept;
[[nodiscard]] bool finite(Vec3 value) noexcept;
[[nodiscard]] Expected<Vec3, Error> normalize(Vec3 value) noexcept;

struct Quat {
  float x{};
  float y{};
  float z{};
  float w{};
  [[nodiscard]] static constexpr Quat identity() noexcept { return {0, 0, 0, 1}; }
  auto operator<=>(const Quat&) const = default;
};

[[nodiscard]] bool finite(Quat value) noexcept;
[[nodiscard]] float dot(Quat a, Quat b) noexcept;
[[nodiscard]] Expected<Quat, Error> normalize(Quat value) noexcept;
[[nodiscard]] Quat conjugate(Quat value) noexcept;
[[nodiscard]] Expected<Quat, Error> inverse(Quat value) noexcept;
[[nodiscard]] Quat multiply(Quat a, Quat b) noexcept;
[[nodiscard]] Vec3 rotate(Quat rotation, Vec3 value) noexcept;
[[nodiscard]] Quat nlerp(Quat a, Quat b, float t) noexcept;
[[nodiscard]] Quat slerp(Quat a, Quat b, float t) noexcept;
[[nodiscard]] Expected<Quat, Error> from_axis_angle(Vec3 axis, float radians) noexcept;
[[nodiscard]] Expected<Quat, Error> from_to_rotation(Vec3 from, Vec3 to) noexcept;
[[nodiscard]] float angular_distance(Quat a, Quat b) noexcept;

struct Transform {
  Vec3 translation{};
  Quat rotation{};
  Vec3 scale{};
  [[nodiscard]] static constexpr Transform identity() noexcept {
    return {{0, 0, 0}, Quat::identity(), {1, 1, 1}};
  }
  auto operator<=>(const Transform&) const = default;
};

using Matrix4 = std::array<float, 16>;

[[nodiscard]] bool finite(const Transform& value) noexcept;
[[nodiscard]] Vec3 transform_point(const Transform& transform, Vec3 point) noexcept;
[[nodiscard]] Transform compose(const Transform& parent, const Transform& child) noexcept;
[[nodiscard]] Expected<Transform, Error> inverse(const Transform& value) noexcept;
[[nodiscard]] Matrix4 to_matrix(const Transform& value) noexcept;

}  // namespace animgraph
