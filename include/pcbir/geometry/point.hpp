// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_POINT_HPP
#define PCBIR_GEOMETRY_POINT_HPP

#include "pcbir/geometry/checked_arith.hpp"

#include <cassert>
#include <cstdint>

namespace pcbir::geometry {

// A 2D coordinate in signed 64-bit nanometers (docs/format-spec.md). The
// same type represents both an absolute position and a relative
// displacement, following this project's convention (docs/cpp-style.md) of
// not introducing a separate Vector type for plane math.
//
// Representable range is the full int64_t range, i.e. roughly +/-9.2e9
// meters -- vastly beyond any real board, but the honest bound of the wire
// format's coordinate type. Arithmetic that would overflow that range
// asserts in debug builds rather than silently wrapping (signed overflow is
// undefined behavior in C++); in a release (NDEBUG) build the checked_*
// helpers still avoid triggering that undefined behavior, but the returned
// Point is otherwise unspecified.
struct Point {
  int64_t x = 0;
  int64_t y = 0;

  friend constexpr bool operator==(const Point&, const Point&) = default;
};

[[nodiscard]] inline Point operator+(const Point& a, const Point& b) {
  Point result;
  const bool ok = checked_add(a.x, b.x, result.x) && checked_add(a.y, b.y, result.y);
  assert(ok && "Point addition overflowed int64_t nanometers");
  (void)ok;
  return result;
}

[[nodiscard]] inline Point operator-(const Point& a, const Point& b) {
  Point result;
  const bool ok = checked_sub(a.x, b.x, result.x) && checked_sub(a.y, b.y, result.y);
  assert(ok && "Point subtraction overflowed int64_t nanometers");
  (void)ok;
  return result;
}

[[nodiscard]] inline Point operator*(const Point& p, int64_t scalar) {
  Point result;
  const bool ok = checked_mul(p.x, scalar, result.x) && checked_mul(p.y, scalar, result.y);
  assert(ok && "Point scaling overflowed int64_t nanometers");
  (void)ok;
  return result;
}

// Exact (no floating point) dot product. Returns false without writing
// `out` if an intermediate product or their sum would overflow.
[[nodiscard]] inline bool dot(const Point& a, const Point& b, int64_t& out) {
  int64_t xx = 0;
  int64_t yy = 0;
  return checked_mul(a.x, b.x, xx) && checked_mul(a.y, b.y, yy) && checked_add(xx, yy, out);
}

// Exact (no floating point) 2D cross product (a.x*b.y - a.y*b.x); its sign
// gives orientation and its magnitude twice the parallelogram area. Returns
// false without writing `out` on overflow.
[[nodiscard]] inline bool cross(const Point& a, const Point& b, int64_t& out) {
  int64_t xy = 0;
  int64_t yx = 0;
  return checked_mul(a.x, b.y, xy) && checked_mul(a.y, b.x, yx) && checked_sub(xy, yx, out);
}

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_POINT_HPP
