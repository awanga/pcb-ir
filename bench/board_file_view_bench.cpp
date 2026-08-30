// SPDX-License-Identifier: Apache-2.0
#include "pcbir/board_file_view.hpp"
#include "pcbir/board_snapshot.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/serialize.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

// Verifies the "Zero-copy load" property (docs/format-spec.md): reading a
// BoardFileView's header should cost O(1)-ish relative to board size,
// unlike deserialize_board()'s full materialization. Run with
// PCBIR_BUILD_BENCH=ON; not wired into ctest/CI.

namespace {

using pcbir::BoardFileView;
using pcbir::BoardSnapshot;
using pcbir::deserialize_board;
using pcbir::serialize;
using pcbir::core::EntityId;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Point;
using pcbir::geometry::Via;

std::filesystem::path write_board_with_n_vias(int64_t count) {
  GeometryWorkspace workspace;
  for (int64_t i = 0; i < count; ++i) {
    workspace.insert(Via{.position = Point{.x = i, .y = i},
                         .drill_diameter_nm = 200000,
                         .finished_hole_diameter_nm = 150000,
                         .pad_diameter_nm = 350000,
                         .start_layer = EntityId{1},
                         .end_layer = EntityId{4},
                         .pad_number = "1"});
  }

  BoardSnapshot board;
  board.geometry = workspace.commit();

  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("pcbir_bench_board_" + std::to_string(count) + ".pcbir");
  const std::vector<uint8_t> bytes = serialize(board);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

// Zero-copy header access: opens the file (VerifyPolicy::Skip, since this
// benchmark trusts a file it just wrote itself) and reads only the format
// version. No entity is materialized -- should not scale with `count`.
void bench_board_file_view_header(benchmark::State& state) {
  const int64_t count = state.range(0);
  const std::filesystem::path path = write_board_with_n_vias(count);

  for (auto _ : state) {
    const BoardFileView view = BoardFileView::open(path, BoardFileView::VerifyPolicy::Skip);
    benchmark::DoNotOptimize(view.format_version());
  }

  std::filesystem::remove(path);
}

// The expensive path: full materialization into an owning BoardSnapshot,
// exactly what BoardFileView::materialize() and deserialize_board() from a
// heap buffer both cost. Should scale with `count`.
void bench_deserialize_board(benchmark::State& state) {
  const int64_t count = state.range(0);
  const std::filesystem::path path = write_board_with_n_vias(count);

  std::ifstream file(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

  for (auto _ : state) {
    BoardSnapshot board = deserialize_board(bytes);
    benchmark::DoNotOptimize(board);
  }

  std::filesystem::remove(path);
}

} // namespace

BENCHMARK(bench_board_file_view_header)->Arg(10)->Arg(1000)->Arg(100000);
BENCHMARK(bench_deserialize_board)->Arg(10)->Arg(1000)->Arg(100000);

BENCHMARK_MAIN();
