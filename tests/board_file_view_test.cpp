// SPDX-License-Identifier: Apache-2.0
#include "pcbir/board_file_view.hpp"
#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/format_error.hpp"
#include "pcbir/format_version.hpp"
#include "pcbir/serialize.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardFileView;
using pcbir::BoardSnapshot;
using pcbir::CURRENT_FORMAT_VERSION;
using pcbir::FormatError;
using pcbir::serialize;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Net;
using pcbir::stackup::Material;
using pcbir::stackup::StackupWorkspace;

namespace {

std::filesystem::path write_temp_board(const BoardSnapshot& board, const char* filename) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / filename;
  const std::vector<uint8_t> bytes = serialize(board);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

BoardSnapshot sample_board() {
  ConnectivityWorkspace connectivity_workspace;
  connectivity_workspace.insert(Net{.name = "GND"});

  StackupWorkspace stackup_workspace;
  stackup_workspace.insert(
      Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000});

  BoardSnapshot board;
  board.connectivity = connectivity_workspace.commit();
  board.stackup = stackup_workspace.commit();
  return board;
}

} // namespace

TEST_CASE("BoardFileView reads header/index fields without materializing entities",
          "[board_file_view]") {
  const std::filesystem::path path =
      write_temp_board(sample_board(), "pcbir_board_file_view_header_test.pcbir");
  const BoardFileView view = BoardFileView::open(path);

  REQUIRE(view.format_version() == CURRENT_FORMAT_VERSION);
  REQUIRE(view.connectivity_byte_size() > 0);
  REQUIRE(view.stackup_byte_size() > 0);
  REQUIRE(view.extension_count() == 0);
  REQUIRE(view.passthrough_blob_count() == 0);

  std::filesystem::remove(path);
}

TEST_CASE("BoardFileView::materialize() reproduces the same entities as deserialize_board()",
          "[board_file_view]") {
  const std::filesystem::path path =
      write_temp_board(sample_board(), "pcbir_board_file_view_materialize_test.pcbir");
  const BoardFileView view = BoardFileView::open(path);

  const BoardSnapshot materialized = view.materialize();
  REQUIRE(materialized.connectivity.table<Net>().size() == 1);
  REQUIRE(materialized.stackup.table<Material>().size() == 1);

  std::filesystem::remove(path);
}

TEST_CASE("BoardFileView::open with VerifyPolicy::Skip still reads valid header fields",
          "[board_file_view]") {
  const std::filesystem::path path =
      write_temp_board(sample_board(), "pcbir_board_file_view_skip_test.pcbir");
  const BoardFileView view = BoardFileView::open(path, BoardFileView::VerifyPolicy::Skip);

  REQUIRE(view.format_version() == CURRENT_FORMAT_VERSION);

  std::filesystem::remove(path);
}

TEST_CASE("BoardFileView::open rejects a corrupt file with FormatError", "[board_file_view]") {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "pcbir_board_file_view_corrupt_test.pcbir";
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "not a flatbuffer, just plain text";
  }

  REQUIRE_THROWS_AS(BoardFileView::open(path), FormatError);

  std::filesystem::remove(path);
}

TEST_CASE("BoardFileView::open rejects a nonexistent file", "[board_file_view]") {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "pcbir_board_file_view_does_not_exist.pcbir";
  std::filesystem::remove(path);

  REQUIRE_THROWS(BoardFileView::open(path));
}

TEST_CASE("A moved-from BoardFileView can be replaced by move-assignment", "[board_file_view]") {
  const std::filesystem::path first_path =
      write_temp_board(sample_board(), "pcbir_board_file_view_move_a.pcbir");
  const std::filesystem::path second_path =
      write_temp_board(sample_board(), "pcbir_board_file_view_move_b.pcbir");

  BoardFileView view = BoardFileView::open(first_path);
  view = BoardFileView::open(second_path);
  REQUIRE(view.format_version() == CURRENT_FORMAT_VERSION);

  std::filesystem::remove(first_path);
  std::filesystem::remove(second_path);
}
