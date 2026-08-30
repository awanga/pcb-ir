// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_PAD_HPP
#define PCBIR_GEOMETRY_PAD_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/polygon.hpp"

#include <string>

namespace pcbir::geometry {

// A component/footprint connection point: a copper shape on one layer, at
// a reference position. Distinct from a bare Polygon -- readers can tell
// "this is a pad" without inferring it from shape alone (the Gerber
// problem this project exists to fix). SMD only for now; a plated
// through-hole pad additionally carries drill data, modeled as a Via
// (docs/architecture.md) sharing this pad's position, not as a field here.
struct Pad {
  Point position;
  Polygon outline;
  LayerRef layer;

  // The footprint-local pad identifier (e.g. "1", "A1") a Footprint's
  // `pads` list orders but does not itself name -- needed since pad
  // numbering is not generally sequential (e.g. a BGA's grid names). Empty
  // is legitimate (some pads, e.g. mechanical/fiducial, carry no number).
  std::string pad_number;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_PAD_HPP
