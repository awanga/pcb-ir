// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_SPATIAL_INDEX_HPP
#define PCBIR_GEOMETRY_SPATIAL_INDEX_HPP

#include "pcbir/geometry/bbox.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pcbir::geometry {

// A uniform grid over axis-aligned bounding boxes for region queries
// (docs/architecture.md: "custom grid/BVH ... no new dependency"). Each
// inserted BBox is bucketed into every grid cell it overlaps (a box may
// span several cells); a query collects the union of candidates from
// every cell overlapping the query region, then narrows to just the boxes
// that actually intersect it -- so `query` always returns exactly the set
// a brute-force scan over every inserted BBox would, never an
// over-approximation.
//
// `cell_size_nm` should be on the order of a typical inserted box's
// extent. Too small wastes memory on many sparsely-populated cells; too
// large degrades toward a single bucket (i.e. brute force). A box much
// larger than the cell size (e.g. a board-spanning copper pour indexed
// alongside vias) is bucketed into proportionally many cells -- such
// outliers should use a coarser index, or their own, rather than skewing
// one cell size for everything else.
class SpatialIndex {
public:
  explicit SpatialIndex(int64_t cell_size_nm);

  // Adds `bounds` and returns its assigned index (0-based, in insertion
  // order) -- the same index `query` reports to identify it.
  std::size_t insert(const BBox& bounds);

  // Returns the sorted, duplicate-free indices of every inserted BBox
  // that intersects `region`, per BBox::intersects (touching counts).
  [[nodiscard]] std::vector<std::size_t> query(const BBox& region) const;

  [[nodiscard]] std::size_t size() const { return bounds_.size(); }

private:
  struct CellCoord {
    int64_t cx = 0;
    int64_t cy = 0;

    friend bool operator==(const CellCoord&, const CellCoord&) = default;
  };

  struct CellCoordHash {
    std::size_t operator()(const CellCoord& coord) const;
  };

  int64_t cell_size_nm_;
  std::vector<BBox> bounds_;
  std::unordered_map<CellCoord, std::vector<std::size_t>, CellCoordHash> cells_;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_SPATIAL_INDEX_HPP
