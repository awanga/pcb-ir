// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/entity_id.hpp"
#include "pcbir/geometry/point.hpp"
#include "pcbir/geometry/serialize.hpp"
#include "pcbir/geometry/via.hpp"
#include "pcbir/stackup/diagnostics.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"
#include "pcbir/stackup/serialize.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::EntityId;
using pcbir::geometry::GeometryWorkspace;
using pcbir::geometry::Point;
using pcbir::geometry::Via;
using pcbir::stackup::Diagnostic;
using pcbir::stackup::DiagnosticCode;
using pcbir::stackup::ImpedanceProfile;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::Material;
using pcbir::stackup::StackupWorkspace;
using pcbir::stackup::validate;
using pcbir::stackup::validate_via_layer_references;

namespace {

bool has(const std::vector<Diagnostic>& diagnostics, EntityId id, DiagnosticCode code) {
  return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) {
    return diagnostic.id == id && diagnostic.code == code;
  });
}

Layer make_copper_layer(int64_t thickness_nm, int64_t roughness_nm) {
  return Layer{.name = "L1",
               .kind = LayerKind::Copper,
               .thickness_nm = thickness_nm,
               .roughness_nm = roughness_nm,
               .material = EntityId{}};
}

Layer make_dielectric_layer(EntityId material) {
  return Layer{.name = "Core1",
               .kind = LayerKind::Dielectric,
               .thickness_nm = 200000,
               .roughness_nm = 0,
               .material = material};
}

Via make_via(EntityId start_layer, EntityId end_layer) {
  return Via{.position = Point{.x = 0, .y = 0},
             .drill_diameter_nm = 200000,
             .finished_hole_diameter_nm = 250000,
             .pad_diameter_nm = 450000,
             .start_layer = start_layer,
             .end_layer = end_layer};
}

} // namespace

TEST_CASE("Material validation rejects an empty name, non-positive Dk, and negative Df",
          "[stackup][diagnostics]") {
  REQUIRE(validate(Material{.name = "FR4",
                            .dielectric_constant_e6 = 4300000,
                            .loss_tangent_e6 = 20000}) == DiagnosticCode::Valid);
  REQUIRE(
      validate(Material{.name = "", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000}) ==
      DiagnosticCode::EmptyMaterialName);
  REQUIRE(
      validate(Material{.name = "FR4", .dielectric_constant_e6 = 0, .loss_tangent_e6 = 20000}) ==
      DiagnosticCode::NonPositiveDielectricConstant);
  REQUIRE(
      validate(Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = -1}) ==
      DiagnosticCode::NegativeLossTangent);
}

TEST_CASE("Layer validation rejects non-positive thickness, negative roughness, and a missing "
          "dielectric material",
          "[stackup][diagnostics]") {
  REQUIRE(validate(make_copper_layer(35000, 2500)) == DiagnosticCode::Valid);
  REQUIRE(validate(make_copper_layer(0, 2500)) == DiagnosticCode::NonPositiveLayerThickness);
  REQUIRE(validate(make_copper_layer(35000, -1)) == DiagnosticCode::NegativeLayerRoughness);
  REQUIRE(validate(make_dielectric_layer(EntityId{})) == DiagnosticCode::MissingDielectricMaterial);
  REQUIRE(validate(make_dielectric_layer(EntityId{1})) == DiagnosticCode::Valid);
}

TEST_CASE("LayerStack validation rejects empty and duplicate membership",
          "[stackup][diagnostics]") {
  REQUIRE(validate(LayerStack{.name = "D", .layers = {EntityId{1}, EntityId{2}}}) ==
          DiagnosticCode::Valid);
  REQUIRE(validate(LayerStack{.name = "D", .layers = {}}) == DiagnosticCode::EmptyStackup);
  REQUIRE(validate(LayerStack{.name = "D", .layers = {EntityId{1}, EntityId{1}}}) ==
          DiagnosticCode::DuplicateStackupLayer);
}

TEST_CASE("ImpedanceProfile validation rejects an empty class name and a non-positive target",
          "[stackup][diagnostics]") {
  REQUIRE(validate(ImpedanceProfile{.class_name = "USB_DIFF",
                                    .target_ohm_e6 = 90000000,
                                    .actual_ohm_e6 = 91200000}) == DiagnosticCode::Valid);
  REQUIRE(
      validate(ImpedanceProfile{.class_name = "", .target_ohm_e6 = 90000000, .actual_ohm_e6 = 0}) ==
      DiagnosticCode::EmptyImpedanceClassName);
  REQUIRE(validate(
              ImpedanceProfile{.class_name = "USB_DIFF", .target_ohm_e6 = 0, .actual_ohm_e6 = 0}) ==
          DiagnosticCode::NonPositiveImpedanceTarget);
}

TEST_CASE("A fully-connected stackup snapshot reports no diagnostics", "[stackup][diagnostics]") {
  StackupWorkspace workspace;
  const auto fr4 = workspace.insert(
      Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000});
  const EntityId fr4_id = workspace.table<Material>().id_of(fr4);
  const auto core = workspace.insert(make_dielectric_layer(fr4_id));
  const EntityId core_id = workspace.table<Layer>().id_of(core);
  workspace.insert(LayerStack{.name = "1-layer", .layers = {core_id}});

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(diagnostics.empty());
}

TEST_CASE("Validating a snapshot flags a LayerStack member that doesn't exist",
          "[stackup][diagnostics]") {
  StackupWorkspace workspace;
  const auto stack = workspace.insert(LayerStack{.name = "bad", .layers = {EntityId{999}}});
  const EntityId stack_id = workspace.table<LayerStack>().id_of(stack);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, stack_id, DiagnosticCode::DanglingStackupLayerReference));
}

TEST_CASE("Validating a snapshot flags a Layer with a dangling material reference",
          "[stackup][diagnostics]") {
  StackupWorkspace workspace;
  const auto layer = workspace.insert(make_dielectric_layer(EntityId{999}));
  const EntityId layer_id = workspace.table<Layer>().id_of(layer);

  const std::vector<Diagnostic> diagnostics = validate(workspace.commit());
  REQUIRE(has(diagnostics, layer_id, DiagnosticCode::DanglingLayerMaterialReference));
}

TEST_CASE("validate_via_layer_references flags a Via whose layer span references a removed layer",
          "[stackup][diagnostics]") {
  StackupWorkspace stackup_workspace;
  const auto layer = stackup_workspace.insert(make_copper_layer(35000, 2500));
  const EntityId layer_id = stackup_workspace.table<Layer>().id_of(layer);

  GeometryWorkspace geometry_workspace;
  const auto dangling_via = geometry_workspace.insert(make_via(layer_id, EntityId{999}));
  const EntityId dangling_via_id = geometry_workspace.table<Via>().id_of(dangling_via);

  const std::vector<Diagnostic> diagnostics =
      validate_via_layer_references(geometry_workspace.commit(), stackup_workspace.commit());
  REQUIRE(has(diagnostics, dangling_via_id, DiagnosticCode::DanglingViaLayerReference));
}

TEST_CASE("validate_via_layer_references reports no diagnostics when every via layer resolves",
          "[stackup][diagnostics]") {
  StackupWorkspace stackup_workspace;
  const auto top = stackup_workspace.insert(make_copper_layer(35000, 2500));
  const auto bottom = stackup_workspace.insert(make_copper_layer(35000, 2500));
  const EntityId top_id = stackup_workspace.table<Layer>().id_of(top);
  const EntityId bottom_id = stackup_workspace.table<Layer>().id_of(bottom);

  GeometryWorkspace geometry_workspace;
  geometry_workspace.insert(make_via(top_id, bottom_id));

  const std::vector<Diagnostic> diagnostics =
      validate_via_layer_references(geometry_workspace.commit(), stackup_workspace.commit());
  REQUIRE(diagnostics.empty());
}
