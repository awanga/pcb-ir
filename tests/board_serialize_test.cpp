// SPDX-License-Identifier: Apache-2.0
#include "pcbir/board_snapshot.hpp"
#include "pcbir/connectivity/net.hpp"
#include "pcbir/connectivity/serialize.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/entity_domain.hpp"
#include "pcbir/extension.hpp"
#include "pcbir/format_error.hpp"
#include "pcbir/format_version.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/passthrough_blob.hpp"
#include "pcbir/serialize.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::BoardSnapshot;
using pcbir::deserialize_board;
using pcbir::EntityDomain;
using pcbir::Extension;
using pcbir::FormatError;
using pcbir::FormatVersion;
using pcbir::PassthroughBlob;
using pcbir::serialize;
using pcbir::connectivity::ConnectivityWorkspace;
using pcbir::connectivity::Net;
using pcbir::core::EntityId;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Point;
using pcbir::geometry::Via;
using pcbir::stackup::Material;
using pcbir::stackup::StackupWorkspace;

namespace {

// Handles to the entities build_sample_board() creates, so individual
// TEST_CASEs below can look up "the via" etc. after a round trip without
// depending on iteration order.
struct SampleIds {
  EntityId via;
  EntityId net;
  EntityId material;
};

BoardSnapshot build_sample_board(SampleIds& ids) {
  GeometryWorkspace geometry_workspace;
  const auto via = geometry_workspace.insert(Via{.position = Point{.x = 0, .y = 0},
                                                 .drill_diameter_nm = 200000,
                                                 .finished_hole_diameter_nm = 150000,
                                                 .pad_diameter_nm = 350000,
                                                 .start_layer = EntityId{1},
                                                 .end_layer = EntityId{4},
                                                 .pad_number = "1"});
  ids.via = geometry_workspace.table<Via>().id_of(via);

  ConnectivityWorkspace connectivity_workspace;
  const auto net = connectivity_workspace.insert(Net{.name = "GND"});
  ids.net = connectivity_workspace.table<Net>().id_of(net);

  StackupWorkspace stackup_workspace;
  const auto material = stackup_workspace.insert(
      Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000});
  ids.material = stackup_workspace.table<Material>().id_of(material);

  BoardSnapshot board;
  board.geometry = geometry_workspace.commit();
  board.connectivity = connectivity_workspace.commit();
  board.stackup = stackup_workspace.commit();
  board.extensions.push_back(Extension{.domain = EntityDomain::Geometry,
                                       .entity_id = ids.via,
                                       .ext_namespace = "VENDOR_ACME",
                                       .name = "trace_impedance_hint",
                                       .version = 1,
                                       .payload = {0x01, 0x02, 0x03}});
  board.passthrough_blobs.push_back(PassthroughBlob{.domain = EntityDomain::Stackup,
                                                    .entity_id = ids.material,
                                                    .source_format = "KICAD_SEXPR",
                                                    .data = {'(', 'x', ')'}});
  return board;
}

} // namespace

TEST_CASE("A board snapshot round-trip preserves every layer's entity count",
          "[serialize][board]") {
  SampleIds ids;
  const BoardSnapshot original = build_sample_board(ids);
  const std::vector<uint8_t> bytes = serialize(original);
  const BoardSnapshot restored = deserialize_board(bytes);

  REQUIRE(restored.geometry.table<Via>().size() == 1);
  REQUIRE(restored.connectivity.table<Net>().size() == 1);
  REQUIRE(restored.stackup.table<Material>().size() == 1);
}

TEST_CASE("A board snapshot round-trip preserves each layer's entity data", "[serialize][board]") {
  SampleIds ids;
  const BoardSnapshot original = build_sample_board(ids);
  const BoardSnapshot restored = deserialize_board(serialize(original));

  const Via* restored_via =
      restored.geometry.table<Via>().try_get(restored.geometry.table<Via>().find(ids.via));
  REQUIRE(restored_via != nullptr);
  REQUIRE(restored_via->drill_diameter_nm == 200000);

  const Net* restored_net =
      restored.connectivity.table<Net>().try_get(restored.connectivity.table<Net>().find(ids.net));
  REQUIRE(restored_net != nullptr);
  REQUIRE(restored_net->name == "GND");

  const Material* restored_material = restored.stackup.table<Material>().try_get(
      restored.stackup.table<Material>().find(ids.material));
  REQUIRE(restored_material != nullptr);
  REQUIRE(restored_material->dielectric_constant_e6 == 4300000);
}

TEST_CASE("A board snapshot defaults to, and round-trips, the current format version",
          "[serialize][board]") {
  SampleIds ids;
  const BoardSnapshot original = build_sample_board(ids);
  REQUIRE(original.format_version == pcbir::CURRENT_FORMAT_VERSION);

  const BoardSnapshot restored = deserialize_board(serialize(original));
  REQUIRE(restored.format_version == pcbir::CURRENT_FORMAT_VERSION);
}

TEST_CASE("An Extension round-trips its namespace, name, version, and payload, attached to "
          "the right entity and domain",
          "[serialize][board]") {
  SampleIds ids;
  const BoardSnapshot original = build_sample_board(ids);
  const BoardSnapshot restored = deserialize_board(serialize(original));

  REQUIRE(restored.extensions.size() == 1);
  const Extension& extension = restored.extensions.at(0);
  REQUIRE(extension.domain == EntityDomain::Geometry);
  REQUIRE(extension.entity_id == ids.via);
  REQUIRE(extension.ext_namespace == "VENDOR_ACME");
  REQUIRE(extension.name == "trace_impedance_hint");
  REQUIRE(extension.version == 1);
  REQUIRE(extension.payload == std::vector<uint8_t>{0x01, 0x02, 0x03});
}

TEST_CASE("A PassthroughBlob round-trips its source format and raw bytes unchanged",
          "[serialize][board]") {
  SampleIds ids;
  const BoardSnapshot original = build_sample_board(ids);
  const BoardSnapshot restored = deserialize_board(serialize(original));

  REQUIRE(restored.passthrough_blobs.size() == 1);
  const PassthroughBlob& blob = restored.passthrough_blobs.at(0);
  REQUIRE(blob.domain == EntityDomain::Stackup);
  REQUIRE(blob.entity_id == ids.material);
  REQUIRE(blob.source_format == "KICAD_SEXPR");
  REQUIRE(blob.data == std::vector<uint8_t>{'(', 'x', ')'});
}

TEST_CASE("A board with no extensions or passthrough blobs round-trips to empty vectors",
          "[serialize][board]") {
  const BoardSnapshot board;
  const BoardSnapshot restored = deserialize_board(serialize(board));

  REQUIRE(restored.extensions.empty());
  REQUIRE(restored.passthrough_blobs.empty());
}

TEST_CASE("Deserializing a board with an unsupported major format version throws FormatError",
          "[serialize][board]") {
  BoardSnapshot board;
  board.format_version =
      FormatVersion{.major = pcbir::CURRENT_FORMAT_VERSION.major + 1, .minor = 0};

  REQUIRE_THROWS_AS(deserialize_board(serialize(board)), FormatError);
}

TEST_CASE("Deserializing a board with a newer, unrecognized minor version does not throw",
          "[serialize][board]") {
  BoardSnapshot board;
  board.format_version = FormatVersion{.major = pcbir::CURRENT_FORMAT_VERSION.major,
                                       .minor = pcbir::CURRENT_FORMAT_VERSION.minor + 1};

  const BoardSnapshot restored = deserialize_board(serialize(board));
  REQUIRE(restored.format_version.minor == pcbir::CURRENT_FORMAT_VERSION.minor + 1);
}
