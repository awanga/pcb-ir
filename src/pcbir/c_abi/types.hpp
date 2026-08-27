// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_C_ABI_TYPES_HPP
#define PCBIR_C_ABI_TYPES_HPP

#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/diagnostics.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/geometry/diagnostics.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/pcbir.h"
#include "pcbir/stackup/diagnostics.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <vector>

// The real definitions behind the opaque types pcbir.h forward-declares.
// Never installed as a public header -- a C consumer only ever sees the
// incomplete `typedef struct pcbir_x pcbir_x_t;` in pcbir.h itself
// (docs/rfcs/0001-stable-c-abi.md).

struct pcbir_board {
  pcbir::BoardSnapshot snapshot;
};

struct pcbir_geometry_snapshot {
  pcbir::geometry::GeometrySnapshot snapshot;
};

struct pcbir_connectivity_snapshot {
  pcbir::connectivity::ConnectivitySnapshot snapshot;
};

struct pcbir_stackup_snapshot {
  pcbir::stackup::StackupSnapshot snapshot;
};

struct pcbir_connectivity_workspace {
  pcbir::connectivity::ConnectivityWorkspace workspace;
};

struct pcbir_geometry_diagnostics {
  std::vector<pcbir::geometry::Diagnostic> diagnostics;
};

struct pcbir_connectivity_diagnostics {
  std::vector<pcbir::connectivity::Diagnostic> diagnostics;
};

struct pcbir_stackup_diagnostics {
  std::vector<pcbir::stackup::Diagnostic> diagnostics;
};

#endif // PCBIR_C_ABI_TYPES_HPP
