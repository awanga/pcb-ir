// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_GEOMETRY_TRACK_HPP
#define PCBIR_GEOMETRY_TRACK_HPP

#include "pcbir/geometry/layer_ref.hpp"
#include "pcbir/geometry/path.hpp"

namespace pcbir::geometry {

// A copper trace: an ordered polyline of segment/arc spans, each with its
// own width, on one layer. Net membership is a connectivity concern
// (pcbir::connectivity) and is not modeled here.
struct Track {
  Path path;
  LayerRef layer;
};

} // namespace pcbir::geometry

#endif // PCBIR_GEOMETRY_TRACK_HPP
