// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/spatial_index.hpp"

#include "pcbir/geometry/bbox.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace pcbir::geometry {

namespace {

// Floor division (rounds toward negative infinity), unlike C++'s built-in
// `/` which truncates toward zero -- needed so cell coordinates are
// contiguous across zero for the full signed coordinate range.
int64_t floor_div(int64_t a, int64_t b) {
  const int64_t q = a / b;
  const int64_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) {
    return q - 1;
  }
  return q;
}

} // namespace

std::size_t SpatialIndex::CellCoordHash::operator()(const CellCoord& coord) const {
  // A standard 64-bit hash-combine (boost::hash_combine's constant) of the
  // two cell coordinates; this is a query-time-only bucket key, never
  // part of any serialized/canonical output, so hash quality/stability
  // across versions doesn't matter the way it would on the wire.
  const std::size_t h1 = std::hash<int64_t>{}(coord.cx);
  const std::size_t h2 = std::hash<int64_t>{}(coord.cy);
  return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
}

SpatialIndex::SpatialIndex(int64_t cell_size_nm) : cell_size_nm_(cell_size_nm) {
  assert(cell_size_nm_ > 0 && "SpatialIndex cell size must be positive");
}

std::size_t SpatialIndex::insert(const BBox& bounds) {
  const std::size_t index = bounds_.size();
  bounds_.push_back(bounds);
  if (bounds.is_empty()) {
    return index;
  }

  const int64_t cx0 = floor_div(bounds.min.x, cell_size_nm_);
  const int64_t cx1 = floor_div(bounds.max.x, cell_size_nm_);
  const int64_t cy0 = floor_div(bounds.min.y, cell_size_nm_);
  const int64_t cy1 = floor_div(bounds.max.y, cell_size_nm_);
  for (int64_t cx = cx0; cx <= cx1; ++cx) {
    for (int64_t cy = cy0; cy <= cy1; ++cy) {
      cells_[CellCoord{.cx = cx, .cy = cy}].push_back(index);
    }
  }
  return index;
}

std::vector<std::size_t> SpatialIndex::query(const BBox& region) const {
  std::vector<std::size_t> result;
  if (region.is_empty()) {
    return result;
  }

  const int64_t cx0 = floor_div(region.min.x, cell_size_nm_);
  const int64_t cx1 = floor_div(region.max.x, cell_size_nm_);
  const int64_t cy0 = floor_div(region.min.y, cell_size_nm_);
  const int64_t cy1 = floor_div(region.max.y, cell_size_nm_);

  std::vector<std::size_t> candidates;
  for (int64_t cx = cx0; cx <= cx1; ++cx) {
    for (int64_t cy = cy0; cy <= cy1; ++cy) {
      const auto cell = cells_.find(CellCoord{.cx = cx, .cy = cy});
      if (cell == cells_.end()) {
        continue;
      }
      candidates.insert(candidates.end(), cell->second.begin(), cell->second.end());
    }
  }
  std::ranges::sort(candidates);
  candidates.erase(std::ranges::unique(candidates).begin(), candidates.end());

  result.reserve(candidates.size());
  // idx came from this index's own cells_, always < bounds_.size().
  for (const std::size_t idx : candidates) {
    // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
    if (bounds_[idx].intersects(region)) {
      result.push_back(idx);
    }
  }
  return result;
}

} // namespace pcbir::geometry
