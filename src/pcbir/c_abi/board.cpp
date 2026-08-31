// SPDX-License-Identifier: Apache-2.0
#include "pcbir/board_snapshot.hpp"
#include "pcbir/c_abi/error.hpp"
#include "pcbir/c_abi/types.hpp"
#include "pcbir/pcbir.h"
#include "pcbir/serialize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> read_file(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error(std::string("failed to open file: ") + path);
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const char* path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error(std::string("failed to open file for writing: ") + path);
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error(std::string("failed to write file: ") + path);
  }
}

} // namespace

pcbir_status_t pcbir_board_load_file(const char* path, pcbir_board_t** out_board) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (path == nullptr || out_board == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    std::vector<uint8_t> bytes;
    try {
      bytes = read_file(path);
    } catch (const std::exception& error) {
      pcbir::c_abi::set_last_error(error.what());
      return PCBIR_ERROR_IO;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_board = new pcbir_board{pcbir::deserialize_board(bytes)};
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_load_bytes(const uint8_t* data, size_t size, pcbir_board_t** out_board) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if ((data == nullptr && size != 0) || out_board == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_board = new pcbir_board{pcbir::deserialize_board(std::span<const uint8_t>(data, size))};
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_save_file(const pcbir_board_t* board, const char* path) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || path == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const std::vector<uint8_t> bytes = pcbir::serialize(board->snapshot);
    try {
      write_file(path, bytes);
    } catch (const std::exception& error) {
      pcbir::c_abi::set_last_error(error.what());
      return PCBIR_ERROR_IO;
    }
    return PCBIR_OK;
  });
}

pcbir_status_t
pcbir_board_save_bytes(const pcbir_board_t* board, uint8_t** out_data, size_t* out_size) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || out_data == nullptr || out_size == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    const std::vector<uint8_t> bytes = pcbir::serialize(board->snapshot);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* buffer = new uint8_t[bytes.size()];
    std::ranges::copy(bytes, buffer);
    *out_data = buffer;
    *out_size = bytes.size();
    return PCBIR_OK;
  });
}

// A non-const pointer is the deliberate, idiomatic choice for a free-style
// function -- matching free()/delete[] itself, not a const-correctness gap.
// NOLINTNEXTLINE(readability-non-const-parameter)
void pcbir_bytes_free(uint8_t* data) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete[] data;
}

void pcbir_board_free(pcbir_board_t* board) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete board;
}

pcbir_status_t
pcbir_board_format_version(const pcbir_board_t* board, uint32_t* out_major, uint32_t* out_minor) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || out_major == nullptr || out_minor == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    *out_major = board->snapshot.format_version.major;
    *out_minor = board->snapshot.format_version.minor;
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_geometry(const pcbir_board_t* board,
                                    pcbir_geometry_snapshot_t** out_snapshot) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || out_snapshot == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_snapshot = new pcbir_geometry_snapshot{board->snapshot.geometry};
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_connectivity(const pcbir_board_t* board,
                                        pcbir_connectivity_snapshot_t** out_snapshot) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || out_snapshot == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_snapshot = new pcbir_connectivity_snapshot{board->snapshot.connectivity};
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_stackup(const pcbir_board_t* board,
                                   pcbir_stackup_snapshot_t** out_snapshot) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (board == nullptr || out_snapshot == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_snapshot = new pcbir_stackup_snapshot{board->snapshot.stackup};
    return PCBIR_OK;
  });
}

pcbir_status_t pcbir_board_with_connectivity(const pcbir_board_t* original,
                                             const pcbir_connectivity_snapshot_t* new_connectivity,
                                             pcbir_board_t** out_board) {
  return pcbir::c_abi::translate_exceptions([&]() -> pcbir_status_t {
    if (original == nullptr || new_connectivity == nullptr || out_board == nullptr) {
      pcbir::c_abi::set_last_error("null argument");
      return PCBIR_ERROR_INVALID_ARGUMENT;
    }
    pcbir::BoardSnapshot updated = original->snapshot;
    updated.connectivity = new_connectivity->snapshot;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out_board = new pcbir_board{std::move(updated)};
    return PCBIR_OK;
  });
}

void pcbir_geometry_snapshot_free(pcbir_geometry_snapshot_t* snapshot) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete snapshot;
}

void pcbir_connectivity_snapshot_free(pcbir_connectivity_snapshot_t* snapshot) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete snapshot;
}

void pcbir_stackup_snapshot_free(pcbir_stackup_snapshot_t* snapshot) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete snapshot;
}
