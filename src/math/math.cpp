#include "animgraph/math/math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace animgraph {
namespace {

constexpr float epsilon = 1.0e-8F;

Quat normalized_or_identity(Quat value) noexcept {
  const auto result = normalize(value);
  return result ? *result : Quat::identity();
}

}  // namespace

Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator-(Vec3 value) noexcept { return {-value.x, -value.y, -value.z}; }
Vec3 operator*(Vec3 value, float scalar) noexcept { return {value.x * scalar, value.y * scalar, value.z * scalar}; }
Vec3 operator*(float scalar, Vec3 value) noexcept { return value * scalar; }
Vec3 operator/(Vec3 value, float scalar) noexcept { return value * (1.0F / scalar); }
Vec3 hadamard(Vec3 a, Vec3 b) noexcept { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float length_squared(Vec3 value) noexcept { return dot(value, value); }
float length(Vec3 value) noexcept { return std::sqrt(length_squared(value)); }
bool finite(Vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
std::expected<Vec3, Error> normalize(Vec3 value) noexcept {
  const float squared = length_squared(value);
  if (!finite(value) || !std::isfinite(squared)) {
    return std::unexpected(Error{ErrorCode::non_finite, "vector is not finite"});
  }
  if (squared <= epsilon) {
    return std::unexpected(Error{ErrorCode::zero_length, "vector length is zero"});
  }
  return value / std::sqrt(squared);
}

bool finite(Quat value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}
float dot(Quat a, Quat b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
std::expected<Quat, Error> normalize(Quat value) noexcept {
  const float squared = dot(value, value);
  if (!finite(value) || !std::isfinite(squared)) {
    return std::unexpected(Error{ErrorCode::non_finite, "quaternion is not finite"});
  }
  if (squared <= epsilon) {
    return std::unexpected(Error{ErrorCode::zero_length, "quaternion length is zero"});
  }
  const float reciprocal = 1.0F / std::sqrt(squared);
  return Quat{value.x * reciprocal, value.y * reciprocal, value.z * reciprocal,
              value.w * reciprocal};
}
Quat conjugate(Quat value) noexcept { return {-value.x, -value.y, -value.z, value.w}; }
std::expected<Quat, Error> inverse(Quat value) noexcept {
  const float squared = dot(value, value);
  if (!finite(value) || !std::isfinite(squared)) {
    return std::unexpected(Error{ErrorCode::non_finite, "quaternion is not finite"});
  }
  if (squared <= epsilon) {
    return std::unexpected(Error{ErrorCode::zero_length, "quaternion length is zero"});
  }
  const Quat result = conjugate(value);
  return Quat{result.x / squared, result.y / squared, result.z / squared, result.w / squared};
}
Quat multiply(Quat a, Quat b) noexcept {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Vec3 rotate(Quat rotation, Vec3 value) noexcept {
  const Quat unit = normalized_or_identity(rotation);
  const Vec3 qv{unit.x, unit.y, unit.z};
  const Vec3 twice = 2.0F * cross(qv, value);
  return value + unit.w * twice + cross(qv, twice);
}
Quat nlerp(Quat a, Quat b, float t) noexcept {
  if (t <= 0.0F) return a;
  if (t >= 1.0F) return b;
  if (dot(a, b) < 0.0F) b = {-b.x, -b.y, -b.z, -b.w};
  return normalized_or_identity({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                 a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}
Quat slerp(Quat a, Quat b, float t) noexcept {
  if (t <= 0.0F) return a;
  if (t >= 1.0F) return b;
  float cosine = dot(a, b);
  if (cosine < 0.0F) {
    b = {-b.x, -b.y, -b.z, -b.w};
    cosine = -cosine;
  }
  cosine = std::clamp(cosine, -1.0F, 1.0F);
  if (cosine > 0.9995F) return nlerp(a, b, t);
  const float angle = std::acos(cosine);
  const float sine = std::sin(angle);
  if (std::abs(sine) <= epsilon) return nlerp(a, b, t);
  const float first = std::sin((1.0F - t) * angle) / sine;
  const float second = std::sin(t * angle) / sine;
  return normalized_or_identity({a.x * first + b.x * second, a.y * first + b.y * second,
                                 a.z * first + b.z * second, a.w * first + b.w * second});
}
std::expected<Quat, Error> from_axis_angle(Vec3 axis, float radians) noexcept {
  if (!std::isfinite(radians)) {
    return std::unexpected(Error{ErrorCode::non_finite, "angle is not finite"});
  }
  const auto unit = normalize(axis);
  if (!unit) return std::unexpected(unit.error());
  const float half = radians * 0.5F;
  const float sine = std::sin(half);
  return normalize(Quat{unit->x * sine, unit->y * sine, unit->z * sine, std::cos(half)});
}
std::expected<Quat, Error> from_to_rotation(Vec3 from, Vec3 to) noexcept {
  const auto a = normalize(from);
  const auto b = normalize(to);
  if (!a) return std::unexpected(a.error());
  if (!b) return std::unexpected(b.error());
  const float cosine = std::clamp(dot(*a, *b), -1.0F, 1.0F);
  if (cosine > 0.999999F) return Quat::identity();
  if (cosine < -0.999999F) {
    Vec3 axis = cross(*a, Vec3{1, 0, 0});
    if (length_squared(axis) <= epsilon) axis = cross(*a, Vec3{0, 1, 0});
    return from_axis_angle(axis, std::numbers::pi_v<float>);
  }
  const Vec3 axis = cross(*a, *b);
  return normalize(Quat{axis.x, axis.y, axis.z, 1.0F + cosine});
}
float angular_distance(Quat a, Quat b) noexcept {
  const Quat na = normalized_or_identity(a);
  const Quat nb = normalized_or_identity(b);
  return 2.0F * std::acos(std::clamp(std::abs(dot(na, nb)), 0.0F, 1.0F));
}

bool finite(const Transform& value) noexcept {
  return finite(value.translation) && finite(value.rotation) && finite(value.scale);
}
Vec3 transform_point(const Transform& transform, Vec3 point) noexcept {
  return transform.translation + rotate(transform.rotation, hadamard(transform.scale, point));
}
Transform compose(const Transform& parent, const Transform& child) noexcept {
  return {transform_point(parent, child.translation),
          normalized_or_identity(multiply(parent.rotation, child.rotation)),
          hadamard(parent.scale, child.scale)};
}
std::expected<Transform, Error> inverse(const Transform& value) noexcept {
  if (!finite(value) || std::abs(value.scale.x) <= epsilon ||
      std::abs(value.scale.y) <= epsilon || std::abs(value.scale.z) <= epsilon) {
    return std::unexpected(Error{ErrorCode::invalid_argument, "transform cannot be inverted"});
  }
  const auto inverse_rotation = inverse(value.rotation);
  if (!inverse_rotation) return std::unexpected(inverse_rotation.error());
  const Vec3 inverse_scale{1.0F / value.scale.x, 1.0F / value.scale.y, 1.0F / value.scale.z};
  return Transform{hadamard(rotate(*inverse_rotation, -value.translation), inverse_scale),
                   *inverse_rotation, inverse_scale};
}
Matrix4 to_matrix(const Transform& value) noexcept {
  const Quat q = normalized_or_identity(value.rotation);
  const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  return {(1.0F - 2.0F * (yy + zz)) * value.scale.x,
          (2.0F * (xy + wz)) * value.scale.x,
          (2.0F * (xz - wy)) * value.scale.x, 0.0F,
          (2.0F * (xy - wz)) * value.scale.y,
          (1.0F - 2.0F * (xx + zz)) * value.scale.y,
          (2.0F * (yz + wx)) * value.scale.y, 0.0F,
          (2.0F * (xz + wy)) * value.scale.z,
          (2.0F * (yz - wx)) * value.scale.z,
          (1.0F - 2.0F * (xx + yy)) * value.scale.z, 0.0F,
          value.translation.x, value.translation.y, value.translation.z, 1.0F};
}

}  // namespace animgraph
