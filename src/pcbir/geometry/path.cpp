// SPDX-License-Identifier: Apache-2.0
#include "pcbir/geometry/path.hpp"

#include "pcbir/geometry/arc.hpp"
#include "pcbir/geometry/contour.hpp"

#include <cstddef>
#include <variant>

namespace pcbir::geometry {

PathValidity validate(const Path& path) {
  const std::size_t count = path.spans.size();
  if (count == 0) {
    return PathValidity::Empty;
  }

  for (std::size_t i = 0; i < count; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const WidthSpan& span = path.spans[i];
    if (span.width_nm <= 0) {
      return PathValidity::NonPositiveWidth;
    }
    if (const auto* arc = std::get_if<Arc>(&span.geometry)) {
      if (!arc->is_valid()) {
        return PathValidity::InvalidArcSpan;
      }
    }
    if (i + 1 < count) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      const WidthSpan& next = path.spans[i + 1];
      if (!(span_end(span.geometry) == span_start(next.geometry))) {
        return PathValidity::Discontinuous;
      }
    }
  }

  return PathValidity::Valid;
}

} // namespace pcbir::geometry
