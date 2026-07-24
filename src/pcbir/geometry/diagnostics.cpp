// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/diagnostics.hpp"

#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/copper_pour.hpp"
#include "pcbir/geometry/drill_hit.hpp"
#include "pcbir/geometry/keepout.hpp"
#include "pcbir/geometry/mask_opening.hpp"
#include "pcbir/geometry/pad.hpp"
#include "pcbir/geometry/path.hpp"
#include "pcbir/geometry/polygon.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/silkscreen_graphic.hpp"
#include "pcbir/geometry/track.hpp"
#include "pcbir/geometry/via.hpp"

#include <vector>

namespace pcbir::geometry {

namespace {

DiagnosticCode from_polygon_validity(PolygonValidity validity) {
  switch (validity) {
  case PolygonValidity::Valid:
    return DiagnosticCode::Valid;
  case PolygonValidity::InvalidOutline:
    return DiagnosticCode::InvalidOutline;
  case PolygonValidity::OutlineWrongOrientation:
    return DiagnosticCode::OutlineWrongOrientation;
  case PolygonValidity::InvalidHole:
    return DiagnosticCode::InvalidHole;
  case PolygonValidity::HoleWrongOrientation:
    return DiagnosticCode::HoleWrongOrientation;
  case PolygonValidity::HoleOutsideOutline:
    return DiagnosticCode::HoleOutsideOutline;
  }
  return DiagnosticCode::InvalidOutline;
}

DiagnosticCode from_path_validity(PathValidity validity) {
  switch (validity) {
  case PathValidity::Valid:
    return DiagnosticCode::Valid;
  case PathValidity::Empty:
    return DiagnosticCode::EmptyPath;
  case PathValidity::Discontinuous:
    return DiagnosticCode::DiscontinuousPath;
  case PathValidity::InvalidArcSpan:
    return DiagnosticCode::InvalidArcSpan;
  case PathValidity::NonPositiveWidth:
    return DiagnosticCode::NonPositiveSpanWidth;
  }
  return DiagnosticCode::EmptyPath;
}

template <typename T> void collect(const GeometrySnapshot& snapshot, std::vector<Diagnostic>& out) {
  snapshot.template table<T>().for_each([&out](core::EntityId id, const T& entity) {
    const DiagnosticCode code = validate(entity);
    if (code != DiagnosticCode::Valid) {
      out.push_back(Diagnostic{.id = id, .code = code});
    }
  });
}

} // namespace

DiagnosticCode validate(const Pad& pad) {
  return from_polygon_validity(validate(pad.outline));
}

DiagnosticCode validate(const Via& via) {
  if (via.drill_diameter_nm <= 0) {
    return DiagnosticCode::NonPositiveDrillDiameter;
  }
  if (via.finished_hole_diameter_nm <= 0) {
    return DiagnosticCode::NonPositiveFinishedHoleDiameter;
  }
  if (via.pad_diameter_nm <= 0) {
    return DiagnosticCode::NonPositivePadDiameter;
  }
  if (via.annular_ring_nm() <= 0) {
    return DiagnosticCode::NonPositiveAnnularRing;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const Track& track) {
  return from_path_validity(validate(track.path));
}

DiagnosticCode validate(const CopperPour& pour) {
  return from_polygon_validity(validate(pour.outline));
}

DiagnosticCode validate(const Keepout& keepout) {
  return from_polygon_validity(validate(keepout.outline));
}

DiagnosticCode validate(const DrillHit& hit) {
  if (hit.diameter_nm <= 0) {
    return DiagnosticCode::NonPositiveDrillDiameter;
  }
  return DiagnosticCode::Valid;
}

DiagnosticCode validate(const MaskOpening& opening) {
  return from_polygon_validity(validate(opening.outline));
}

DiagnosticCode validate(const SilkscreenGraphic& graphic) {
  return from_path_validity(validate(graphic.path));
}

std::vector<Diagnostic> validate(const GeometrySnapshot& snapshot) {
  std::vector<Diagnostic> result;
  collect<Pad>(snapshot, result);
  collect<Via>(snapshot, result);
  collect<Track>(snapshot, result);
  collect<CopperPour>(snapshot, result);
  collect<Keepout>(snapshot, result);
  collect<DrillHit>(snapshot, result);
  collect<MaskOpening>(snapshot, result);
  collect<SilkscreenGraphic>(snapshot, result);
  return result;
}

} // namespace pcbir::geometry
