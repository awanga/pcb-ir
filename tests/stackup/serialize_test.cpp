// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::stackup::deserialize_stackup;
using pcbir::stackup::ImpedanceProfile;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::Material;
using pcbir::stackup::serialize;
using pcbir::stackup::StackupSnapshot;
using pcbir::stackup::StackupWorkspace;

namespace {

// Handles to the entities build_sample_snapshot() creates, by name, so
// individual TEST_CASEs below can look up "the FR4 material" etc. after a
// round trip without depending on iteration order.
struct SampleIds {
  EntityId fr4_material;
  EntityId copper_layer;
  EntityId core_layer;
  EntityId edge_cuts_layer;
  EntityId layer_stack;
  EntityId impedance_profile;
};

StackupSnapshot build_sample_snapshot(SampleIds& ids) {
  StackupWorkspace workspace;

  const auto fr4 = workspace.insert(
      Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000});
  ids.fr4_material = workspace.table<Material>().id_of(fr4);

  const auto copper = workspace.insert(Layer{.name = "L1",
                                             .kind = LayerKind::Copper,
                                             .thickness_nm = 35000,
                                             .roughness_nm = 2500,
                                             .material = EntityId{}});
  ids.copper_layer = workspace.table<Layer>().id_of(copper);

  const auto core = workspace.insert(Layer{.name = "Core1",
                                           .kind = LayerKind::Dielectric,
                                           .thickness_nm = 200000,
                                           .roughness_nm = 0,
                                           .material = ids.fr4_material});
  ids.core_layer = workspace.table<Layer>().id_of(core);

  const auto stack =
      workspace.insert(LayerStack{.name = "2-layer", .layers = {ids.copper_layer, ids.core_layer}});
  ids.layer_stack = workspace.table<LayerStack>().id_of(stack);

  // Not a member of the LayerStack above -- EdgeCuts is not a physical
  // layer, only a board-outline reference target.
  const auto edge_cuts = workspace.insert(Layer{.name = "Edge.Cuts",
                                                .kind = LayerKind::EdgeCuts,
                                                .thickness_nm = 0,
                                                .roughness_nm = 0,
                                                .material = EntityId{}});
  ids.edge_cuts_layer = workspace.table<Layer>().id_of(edge_cuts);

  const auto profile = workspace.insert(ImpedanceProfile{
      .class_name = "USB_DIFF", .target_ohm_e6 = 90000000, .actual_ohm_e6 = 91200000});
  ids.impedance_profile = workspace.table<ImpedanceProfile>().id_of(profile);

  return workspace.commit();
}

StackupSnapshot round_trip(const StackupSnapshot& original) {
  const std::vector<uint8_t> bytes = serialize(original);
  StackupWorkspace restored_workspace = deserialize_stackup(bytes);
  return restored_workspace.commit();
}

} // namespace

TEST_CASE("A stackup snapshot round-trip preserves every entity's count", "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  REQUIRE(restored.table<Material>().size() == original.table<Material>().size());
  REQUIRE(restored.table<Layer>().size() == original.table<Layer>().size());
  REQUIRE(restored.table<LayerStack>().size() == original.table<LayerStack>().size());
  REQUIRE(restored.table<ImpedanceProfile>().size() == original.table<ImpedanceProfile>().size());
}

TEST_CASE("A Material round-trips its dielectric constant and loss tangent",
          "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  const Material* restored_material =
      restored.table<Material>().try_get(restored.table<Material>().find(ids.fr4_material));
  REQUIRE(restored_material != nullptr);
  REQUIRE(restored_material->name == "FR4");
  REQUIRE(restored_material->dielectric_constant_e6 == 4300000);
  REQUIRE(restored_material->loss_tangent_e6 == 20000);
}

TEST_CASE("A Layer round-trips its kind, thickness/roughness, and material reference",
          "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  const Layer* restored_core =
      restored.table<Layer>().try_get(restored.table<Layer>().find(ids.core_layer));
  REQUIRE(restored_core != nullptr);
  REQUIRE(restored_core->kind == LayerKind::Dielectric);
  REQUIRE(restored_core->thickness_nm == 200000);
  REQUIRE(restored_core->material == ids.fr4_material);

  const Layer* restored_copper =
      restored.table<Layer>().try_get(restored.table<Layer>().find(ids.copper_layer));
  REQUIRE(restored_copper != nullptr);
  REQUIRE(restored_copper->kind == LayerKind::Copper);
  REQUIRE(restored_copper->roughness_nm == 2500);
  REQUIRE(restored_copper->material.is_null());
}

TEST_CASE("An EdgeCuts Layer round-trips its kind and zero thickness", "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  const Layer* restored_edge_cuts =
      restored.table<Layer>().try_get(restored.table<Layer>().find(ids.edge_cuts_layer));
  REQUIRE(restored_edge_cuts != nullptr);
  REQUIRE(restored_edge_cuts->kind == LayerKind::EdgeCuts);
  REQUIRE(restored_edge_cuts->thickness_nm == 0);
  REQUIRE(restored_edge_cuts->material.is_null());
}

TEST_CASE("A LayerStack round-trips its member ordering", "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  const LayerStack* restored_stack =
      restored.table<LayerStack>().try_get(restored.table<LayerStack>().find(ids.layer_stack));
  REQUIRE(restored_stack != nullptr);
  REQUIRE(restored_stack->name == "2-layer");
  REQUIRE(restored_stack->layers.size() == 2);
  REQUIRE(restored_stack->layers.at(0) == ids.copper_layer);
  REQUIRE(restored_stack->layers.at(1) == ids.core_layer);
}

TEST_CASE("An ImpedanceProfile round-trips its target and actual values", "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  const ImpedanceProfile* restored_profile = restored.table<ImpedanceProfile>().try_get(
      restored.table<ImpedanceProfile>().find(ids.impedance_profile));
  REQUIRE(restored_profile != nullptr);
  REQUIRE(restored_profile->class_name == "USB_DIFF");
  REQUIRE(restored_profile->target_ohm_e6 == 90000000);
  REQUIRE(restored_profile->actual_ohm_e6 == 91200000);
}

TEST_CASE("Entity ids are preserved, not reassigned, across the round trip",
          "[stackup][serialize]") {
  SampleIds ids;
  const StackupSnapshot original = build_sample_snapshot(ids);
  const StackupSnapshot restored = round_trip(original);

  REQUIRE_FALSE(restored.table<Material>().find(ids.fr4_material).is_null());
  REQUIRE_FALSE(restored.table<Layer>().find(ids.core_layer).is_null());
}
