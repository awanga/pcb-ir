// SPDX-License-Identifier: Apache-2.0
#include "pcbir/core/arena.hpp"
#include "pcbir/core/entity_id.hpp"
#include "pcbir/stackup/impedance_profile.hpp"
#include "pcbir/stackup/layer.hpp"
#include "pcbir/stackup/layer_stack.hpp"
#include "pcbir/stackup/material.hpp"

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using pcbir::core::Arena;
using pcbir::core::EntityId;
using pcbir::stackup::ImpedanceProfile;
using pcbir::stackup::Layer;
using pcbir::stackup::LayerKind;
using pcbir::stackup::LayerStack;
using pcbir::stackup::Material;

TEST_CASE("Material carries dielectric properties as fixed-point integers", "[stackup][entities]") {
  const Material fr4{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000};

  REQUIRE(fr4.name == "FR4");
  REQUIRE(fr4.dielectric_constant_e6 == 4300000);
  REQUIRE(fr4.loss_tangent_e6 == 20000);
}

TEST_CASE("Layer carries kind, thickness/roughness, and a material reference",
          "[stackup][entities]") {
  const Layer layer{.name = "Core1",
                    .kind = LayerKind::Dielectric,
                    .thickness_nm = 200000,
                    .roughness_nm = 0,
                    .material = EntityId{7}};

  REQUIRE(layer.name == "Core1");
  REQUIRE(layer.kind == LayerKind::Dielectric);
  REQUIRE(layer.thickness_nm == 200000);
  REQUIRE(layer.material == EntityId{7});
}

TEST_CASE("A default-constructed Layer is Copper with a null material", "[stackup][entities]") {
  const Layer layer{};

  REQUIRE(layer.kind == LayerKind::Copper);
  REQUIRE(layer.material.is_null());
}

TEST_CASE("LayerStack preserves member ordering", "[stackup][entities]") {
  const LayerStack stack{.name = "4-layer", .layers = {EntityId{1}, EntityId{2}, EntityId{3}}};

  REQUIRE(stack.name == "4-layer");
  REQUIRE(stack.layers.size() == 3);
  REQUIRE(stack.layers.at(0) == EntityId{1});
  REQUIRE(stack.layers.at(1) == EntityId{2});
  REQUIRE(stack.layers.at(2) == EntityId{3});
}

TEST_CASE("ImpedanceProfile carries a target and an actual value", "[stackup][entities]") {
  const ImpedanceProfile profile{
      .class_name = "USB_DIFF", .target_ohm_e6 = 90000000, .actual_ohm_e6 = 91200000};

  REQUIRE(profile.class_name == "USB_DIFF");
  REQUIRE(profile.target_ohm_e6 == 90000000);
  REQUIRE(profile.actual_ohm_e6 == 91200000);
}

TEST_CASE("Stackup entities are distinct component-table types", "[stackup][entities]") {
  Arena<Material> materials;
  Arena<Layer> layers;

  const auto material_handle = materials.insert(
      Material{.name = "FR4", .dielectric_constant_e6 = 4300000, .loss_tangent_e6 = 20000},
      EntityId{1});
  const auto layer_handle = layers.insert(Layer{.name = "L1",
                                                .kind = LayerKind::Copper,
                                                .thickness_nm = 35000,
                                                .roughness_nm = 2500,
                                                .material = EntityId{}},
                                          EntityId{2});

  REQUIRE(materials.get(material_handle).name == "FR4");
  REQUIRE(layers.get(layer_handle).name == "L1");
  static_assert(!std::is_same_v<decltype(material_handle), decltype(layer_handle)>);
}
