// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_STACKUP_MATERIAL_HPP
#define PCBIR_STACKUP_MATERIAL_HPP

#include <cstdint>
#include <string>

namespace pcbir::stackup {

// A dielectric material a Layer can be made of (e.g. FR4, polyimide,
// prepreg). dielectric_constant_e6/loss_tangent_e6 are the material's
// relative permittivity (Dk) and loss tangent (Df) -- both dimensionless
// physical constants -- scaled by 1e6 and stored as integers, since the
// wire format never carries floating point (docs/format-spec.md). A Dk of
// 4.3 is stored as 4300000; a Df of 0.02 is stored as 20000.
struct Material {
  std::string name;
  int64_t dielectric_constant_e6 = 0;
  int64_t loss_tangent_e6 = 0;
};

} // namespace pcbir::stackup

#endif // PCBIR_STACKUP_MATERIAL_HPP
