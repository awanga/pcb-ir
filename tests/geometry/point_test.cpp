// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/bbox.hpp"
#include "pcbir/geometry/checked_arith.hpp"
#include "pcbir/geometry/point.hpp"

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::BBox;
using pcbir::geometry::checked_add;
using pcbir::geometry::checked_mul;
using pcbir::geometry::checked_sub;
using pcbir::geometry::cross;
using pcbir::geometry::dot;
using pcbir::geometry::Point;

namespace {
constexpr int64_t MAX_INT64 = std::numeric_limits<int64_t>::max();
constexpr int64_t MIN_INT64 = std::numeric_limits<int64_t>::min();
} // namespace

TEST_CASE("checked_add reports overflow instead of wrapping", "[geometry][checked_arith]") {
  int64_t out = 0;

  REQUIRE(checked_add(2, 3, out));
  REQUIRE(out == 5);

  REQUIRE(checked_add(MAX_INT64, 0, out));
  REQUIRE(out == MAX_INT64);

  REQUIRE_FALSE(checked_add(MAX_INT64, 1, out));
  REQUIRE_FALSE(checked_add(MIN_INT64, -1, out));
}

TEST_CASE("checked_sub reports overflow instead of wrapping", "[geometry][checked_arith]") {
  int64_t out = 0;

  REQUIRE(checked_sub(5, 3, out));
  REQUIRE(out == 2);

  REQUIRE_FALSE(checked_sub(MIN_INT64, 1, out));
  REQUIRE_FALSE(checked_sub(MAX_INT64, -1, out));
}

TEST_CASE("checked_mul reports overflow instead of wrapping", "[geometry][checked_arith]") {
  int64_t out = 0;

  REQUIRE(checked_mul(3, 4, out));
  REQUIRE(out == 12);

  REQUIRE(checked_mul(MIN_INT64, 0, out));
  REQUIRE(out == 0);

  REQUIRE(checked_mul(MIN_INT64, 1, out));
  REQUIRE(out == MIN_INT64);

  REQUIRE(checked_mul(-3, -4, out));
  REQUIRE(out == 12);

  REQUIRE(checked_mul(5, -3, out));
  REQUIRE(out == -15);

  // Negating INT64_MIN overflows int64_t (its magnitude is max+1).
  REQUIRE_FALSE(checked_mul(MIN_INT64, -1, out));
  REQUIRE_FALSE(checked_mul(-1, MIN_INT64, out));

  REQUIRE_FALSE(checked_mul(MAX_INT64, 2, out));
  REQUIRE_FALSE(checked_mul(MAX_INT64, -2, out));
  REQUIRE_FALSE(checked_mul(MIN_INT64 / 2, -3, out));
}

TEST_CASE("Point arithmetic operators", "[geometry][point]") {
  constexpr Point a{.x = 10, .y = 20};
  constexpr Point b{.x = 3, .y = 4};

  REQUIRE((a + b) == Point{.x = 13, .y = 24});
  REQUIRE((a - b) == Point{.x = 7, .y = 16});
  REQUIRE((b * 3) == Point{.x = 9, .y = 12});
}

TEST_CASE("dot/cross compute exact results for simple vectors", "[geometry][point]") {
  int64_t result = 0;

  REQUIRE(dot(Point{.x = 1, .y = 0}, Point{.x = 0, .y = 1}, result));
  REQUIRE(result == 0);

  REQUIRE(dot(Point{.x = 3, .y = 4}, Point{.x = 3, .y = 4}, result));
  REQUIRE(result == 25);

  REQUIRE(cross(Point{.x = 1, .y = 0}, Point{.x = 0, .y = 1}, result));
  REQUIRE(result == 1);

  REQUIRE(cross(Point{.x = 0, .y = 1}, Point{.x = 1, .y = 0}, result));
  REQUIRE(result == -1);
}

TEST_CASE("dot/cross report overflow rather than produce a wrong exact result",
          "[geometry][point]") {
  int64_t result = 0;
  REQUIRE_FALSE(
      dot(Point{.x = MAX_INT64, .y = MAX_INT64}, Point{.x = MAX_INT64, .y = MAX_INT64}, result));
  REQUIRE_FALSE(
      cross(Point{.x = MAX_INT64, .y = MAX_INT64}, Point{.x = MAX_INT64, .y = MAX_INT64}, result));
}

TEST_CASE("A default-constructed BBox is empty", "[geometry][bbox]") {
  const BBox box;
  REQUIRE(box.is_empty());
  REQUIRE_FALSE(box.contains(Point{.x = 0, .y = 0}));
}

TEST_CASE("BBox::from_point contains exactly that point", "[geometry][bbox]") {
  const BBox box = BBox::from_point(Point{.x = 5, .y = 5});

  REQUIRE_FALSE(box.is_empty());
  REQUIRE(box.contains(Point{.x = 5, .y = 5}));
  REQUIRE_FALSE(box.contains(Point{.x = 6, .y = 5}));
}

TEST_CASE("BBox::union_with grows to cover both boxes", "[geometry][bbox]") {
  const BBox a{.min = Point{.x = 0, .y = 0}, .max = Point{.x = 10, .y = 10}};
  const BBox b{.min = Point{.x = 5, .y = -5}, .max = Point{.x = 20, .y = 8}};

  const BBox merged = a.union_with(b);

  REQUIRE(merged.min == Point{.x = 0, .y = -5});
  REQUIRE(merged.max == Point{.x = 20, .y = 10});
}

TEST_CASE("BBox::union_with an empty box returns the other box unchanged", "[geometry][bbox]") {
  const BBox real{.min = Point{.x = 1, .y = 1}, .max = Point{.x = 2, .y = 2}};
  const BBox empty;

  REQUIRE(real.union_with(empty).min == real.min);
  REQUIRE(real.union_with(empty).max == real.max);
  REQUIRE(empty.union_with(real).min == real.min);
  REQUIRE(empty.union_with(real).max == real.max);
}

TEST_CASE("BBox::intersects detects overlap and separation", "[geometry][bbox]") {
  const BBox a{.min = Point{.x = 0, .y = 0}, .max = Point{.x = 10, .y = 10}};
  const BBox overlapping{.min = Point{.x = 5, .y = 5}, .max = Point{.x = 15, .y = 15}};
  const BBox disjoint{.min = Point{.x = 20, .y = 20}, .max = Point{.x = 30, .y = 30}};

  REQUIRE(a.intersects(overlapping));
  REQUIRE_FALSE(a.intersects(disjoint));
}
