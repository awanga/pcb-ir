// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/bbox.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/spatial_index.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::geometry::BBox;
using pcbir::geometry::Point;
using pcbir::geometry::SpatialIndex;

namespace {

std::vector<std::size_t> brute_force_query(const std::vector<BBox>& boxes, const BBox& region) {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access) -- i < boxes.size() by the loop bound
    if (boxes[i].intersects(region)) {
      result.push_back(i);
    }
  }
  return result;
}

} // namespace

TEST_CASE("SpatialIndex::query matches brute force on a randomized corpus",
          "[geometry][spatial_index]") {
  // Fixed seed: reproducible across runs and platforms, not a source of
  // test flakiness.
  std::mt19937 rng(20260724); // NOLINT(bugprone-random-generator-seed)
  std::uniform_int_distribution<int64_t> coord_dist(-100000, 100000);
  std::uniform_int_distribution<int64_t> extent_dist(1, 5000);

  std::vector<BBox> boxes;
  SpatialIndex index(2000);
  for (int i = 0; i < 500; ++i) {
    const int64_t x0 = coord_dist(rng);
    const int64_t y0 = coord_dist(rng);
    const BBox box{.min = Point{.x = x0, .y = y0},
                   .max = Point{.x = x0 + extent_dist(rng), .y = y0 + extent_dist(rng)}};
    const std::size_t assigned = index.insert(box);
    boxes.push_back(box);
    REQUIRE(assigned == boxes.size() - 1);
  }
  REQUIRE(index.size() == boxes.size());

  for (int q = 0; q < 200; ++q) {
    const int64_t x0 = coord_dist(rng);
    const int64_t y0 = coord_dist(rng);
    const BBox region{.min = Point{.x = x0, .y = y0},
                      .max = Point{.x = x0 + extent_dist(rng), .y = y0 + extent_dist(rng)}};

    std::vector<std::size_t> expected = brute_force_query(boxes, region);
    const std::vector<std::size_t> actual = index.query(region);
    std::ranges::sort(expected);
    REQUIRE(actual == expected);
  }
}

TEST_CASE("SpatialIndex::query on an empty index returns nothing", "[geometry][spatial_index]") {
  const SpatialIndex index(1000);
  const BBox region{.min = Point{.x = -1000, .y = -1000}, .max = Point{.x = 1000, .y = 1000}};
  REQUIRE(index.query(region).empty());
}

TEST_CASE("SpatialIndex::query with an empty region returns nothing", "[geometry][spatial_index]") {
  SpatialIndex index(1000);
  index.insert(BBox{.min = Point{.x = 0, .y = 0}, .max = Point{.x = 10, .y = 10}});
  REQUIRE(index.query(BBox{}).empty());
}

TEST_CASE("A box spanning many cells is still found by a query touching only one of them",
          "[geometry][spatial_index]") {
  SpatialIndex index(100);
  const std::size_t wide =
      index.insert(BBox{.min = Point{.x = -5000, .y = 0}, .max = Point{.x = 5000, .y = 10}});

  const BBox far_corner{.min = Point{.x = 4990, .y = 0}, .max = Point{.x = 5000, .y = 10}};
  const std::vector<std::size_t> result = index.query(far_corner);
  REQUIRE(result == std::vector<std::size_t>{wide});
}

TEST_CASE("Cell coordinates around zero are contiguous for negative coordinates",
          "[geometry][spatial_index]") {
  SpatialIndex index(100);
  const std::size_t left =
      index.insert(BBox{.min = Point{.x = -150, .y = -150}, .max = Point{.x = -140, .y = -140}});
  const std::size_t right =
      index.insert(BBox{.min = Point{.x = 140, .y = 140}, .max = Point{.x = 150, .y = 150}});

  const BBox spanning{.min = Point{.x = -200, .y = -200}, .max = Point{.x = 200, .y = 200}};
  std::vector<std::size_t> result = index.query(spanning);
  std::ranges::sort(result);
  REQUIRE(result == std::vector<std::size_t>{left, right});
}
