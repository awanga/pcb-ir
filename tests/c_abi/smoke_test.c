/* SPDX-License-Identifier: Apache-2.0 */
/* Genuine C11, compiled and linked against only pcbir_c_abi -- proves
 * pcbir.h is valid, self-contained C and that the scoped-in MVP surface
 * (docs/rfcs/0001-stable-c-abi.md) actually works end to end: load a
 * board, read a Via/Material/Pin, run geometry diagnostics, edit
 * connectivity (insert a Net, commit, read it back), and fold the edit
 * back into a saveable board. */
#include "pcbir/pcbir.h"

#include <stdio.h>
#include <string.h>

#define CHECK(call)                                                                                \
  do {                                                                                             \
    pcbir_status_t status_ = (call);                                                               \
    if (status_ != PCBIR_OK) {                                                                     \
      fprintf(stderr, "%s failed: status=%d error=%s\n", #call, (int)status_, pcbir_last_error()); \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <fixture-path>\n", argc > 0 ? argv[0] : "smoke_test");
    return 1;
  }

  pcbir_board_t* board = NULL;
  CHECK(pcbir_board_load_file(argv[1], &board));

  uint32_t major = 0;
  uint32_t minor = 0;
  CHECK(pcbir_board_format_version(board, &major, &minor));
  if (major != 1) {
    fprintf(stderr, "unexpected format major version: %u\n", major);
    return 1;
  }

  /* ---- geometry: Via + diagnostics ---- */
  pcbir_geometry_snapshot_t* geometry = NULL;
  CHECK(pcbir_board_geometry(board, &geometry));

  size_t via_count = 0;
  CHECK(pcbir_geometry_via_count(geometry, &via_count));
  if (via_count != 1) {
    fprintf(stderr, "expected 1 via, got %zu\n", via_count);
    return 1;
  }

  pcbir_via_handle_t via_handle;
  CHECK(pcbir_geometry_via_at(geometry, 0, &via_handle));

  int64_t x = 0;
  int64_t y = 0;
  CHECK(pcbir_geometry_via_position(geometry, via_handle, &x, &y));
  if (x != 1000000 || y != 2000000) {
    fprintf(stderr, "unexpected via position: (%lld, %lld)\n", (long long)x, (long long)y);
    return 1;
  }

  int64_t drill_diameter_nm = 0;
  CHECK(pcbir_geometry_via_drill_diameter_nm(geometry, via_handle, &drill_diameter_nm));
  if (drill_diameter_nm != 200000) {
    fprintf(stderr, "unexpected drill diameter: %lld\n", (long long)drill_diameter_nm);
    return 1;
  }

  int64_t annular_ring_nm = 0;
  CHECK(pcbir_geometry_via_annular_ring_nm(geometry, via_handle, &annular_ring_nm));
  if (annular_ring_nm != 100000) {
    fprintf(stderr, "unexpected annular ring: %lld\n", (long long)annular_ring_nm);
    return 1;
  }

  uint64_t start_layer_entity_id = 0;
  uint64_t end_layer_entity_id = 0;
  CHECK(pcbir_geometry_via_start_layer_entity_id(geometry, via_handle, &start_layer_entity_id));
  CHECK(pcbir_geometry_via_end_layer_entity_id(geometry, via_handle, &end_layer_entity_id));
  if (start_layer_entity_id != 1 || end_layer_entity_id != 2) {
    fprintf(stderr,
            "unexpected via layer entity ids: %llu, %llu\n",
            (unsigned long long)start_layer_entity_id,
            (unsigned long long)end_layer_entity_id);
    return 1;
  }

  pcbir_geometry_diagnostics_t* geometry_diagnostics = NULL;
  CHECK(pcbir_geometry_validate(geometry, &geometry_diagnostics));
  size_t geometry_diagnostics_count = 0;
  CHECK(pcbir_geometry_diagnostics_count(geometry_diagnostics, &geometry_diagnostics_count));
  if (geometry_diagnostics_count != 0) {
    fprintf(stderr, "expected 0 geometry diagnostics, got %zu\n", geometry_diagnostics_count);
    return 1;
  }
  pcbir_geometry_diagnostics_free(geometry_diagnostics);
  pcbir_geometry_snapshot_free(geometry);

  /* ---- stackup: Material ---- */
  pcbir_stackup_snapshot_t* stackup = NULL;
  CHECK(pcbir_board_stackup(board, &stackup));

  size_t material_count = 0;
  CHECK(pcbir_stackup_material_count(stackup, &material_count));
  if (material_count != 1) {
    fprintf(stderr, "expected 1 material, got %zu\n", material_count);
    return 1;
  }

  pcbir_material_handle_t material_handle;
  CHECK(pcbir_stackup_material_at(stackup, 0, &material_handle));
  const char* material_name = NULL;
  size_t material_name_length = 0;
  CHECK(
      pcbir_stackup_material_name(stackup, material_handle, &material_name, &material_name_length));
  if (material_name_length != 3 || strncmp(material_name, "FR4", 3) != 0) {
    fprintf(stderr, "unexpected material name\n");
    return 1;
  }

  int64_t dielectric_constant_e6 = 0;
  CHECK(pcbir_stackup_material_dielectric_constant_e6(
      stackup, material_handle, &dielectric_constant_e6));
  if (dielectric_constant_e6 != 4300000) {
    fprintf(stderr, "unexpected dielectric constant: %lld\n", (long long)dielectric_constant_e6);
    return 1;
  }
  pcbir_stackup_snapshot_free(stackup);

  /* ---- connectivity: Pin, then a workspace edit/commit loop ---- */
  pcbir_connectivity_snapshot_t* connectivity = NULL;
  CHECK(pcbir_board_connectivity(board, &connectivity));

  size_t pin_count = 0;
  CHECK(pcbir_connectivity_pin_count(connectivity, &pin_count));
  if (pin_count != 1) {
    fprintf(stderr, "expected 1 pin, got %zu\n", pin_count);
    return 1;
  }

  pcbir_pin_handle_t pin_handle;
  CHECK(pcbir_connectivity_pin_at(connectivity, 0, &pin_handle));
  uint64_t pad_entity_id = 0;
  uint64_t net_entity_id = 0;
  CHECK(pcbir_connectivity_pin_pad_entity_id(connectivity, pin_handle, &pad_entity_id));
  CHECK(pcbir_connectivity_pin_net_entity_id(connectivity, pin_handle, &net_entity_id));
  if (pad_entity_id != 100 || net_entity_id == 0) {
    fprintf(stderr,
            "unexpected pin pad/net entity ids: %llu, %llu\n",
            (unsigned long long)pad_entity_id,
            (unsigned long long)net_entity_id);
    return 1;
  }

  pcbir_connectivity_workspace_t* workspace = NULL;
  CHECK(pcbir_connectivity_workspace_create_from_snapshot(connectivity, &workspace));
  pcbir_connectivity_snapshot_free(connectivity);

  pcbir_net_handle_t new_net_handle;
  CHECK(pcbir_connectivity_workspace_insert_net(workspace, "USB_D+", 6, &new_net_handle));

  pcbir_connectivity_snapshot_t* new_connectivity = NULL;
  CHECK(pcbir_connectivity_workspace_commit(workspace, &new_connectivity));
  pcbir_connectivity_workspace_free(workspace);

  const char* new_net_name = NULL;
  size_t new_net_name_length = 0;
  CHECK(pcbir_connectivity_net_name(
      new_connectivity, new_net_handle, &new_net_name, &new_net_name_length));
  if (new_net_name_length != 6 || strncmp(new_net_name, "USB_D+", 6) != 0) {
    fprintf(stderr, "unexpected new net name\n");
    return 1;
  }

  size_t new_net_total = 0;
  CHECK(pcbir_connectivity_net_count(new_connectivity, &new_net_total));
  if (new_net_total != 2) {
    fprintf(stderr, "expected 2 nets after the edit (GND + USB_D+), got %zu\n", new_net_total);
    return 1;
  }

  /* ---- fold the edit back into a saveable board ---- */
  pcbir_board_t* updated_board = NULL;
  CHECK(pcbir_board_with_connectivity(board, new_connectivity, &updated_board));
  pcbir_connectivity_snapshot_free(new_connectivity);
  pcbir_board_free(board);

  uint8_t* saved_bytes = NULL;
  size_t saved_size = 0;
  CHECK(pcbir_board_save_bytes(updated_board, &saved_bytes, &saved_size));
  if (saved_size == 0) {
    fprintf(stderr, "expected non-empty saved bytes\n");
    return 1;
  }

  pcbir_board_t* reloaded_board = NULL;
  CHECK(pcbir_board_load_bytes(saved_bytes, saved_size, &reloaded_board));
  pcbir_bytes_free(saved_bytes);

  pcbir_connectivity_snapshot_t* reloaded_connectivity = NULL;
  CHECK(pcbir_board_connectivity(reloaded_board, &reloaded_connectivity));
  size_t reloaded_net_count = 0;
  CHECK(pcbir_connectivity_net_count(reloaded_connectivity, &reloaded_net_count));
  if (reloaded_net_count != 2) {
    fprintf(stderr,
            "expected 2 nets after a save_bytes/load_bytes round trip, got %zu\n",
            reloaded_net_count);
    return 1;
  }
  pcbir_connectivity_snapshot_free(reloaded_connectivity);
  pcbir_board_free(reloaded_board);

  pcbir_board_free(updated_board);

  printf("c_abi smoke test passed\n");
  return 0;
}
