// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_BOARD_FILE_VIEW_HPP
#define PCBIR_BOARD_FILE_VIEW_HPP

#include "pcbir/board_snapshot.hpp"
#include "pcbir/format_version.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace pcbir {

// A read-only, memory-mapped view over a file produced by
// pcbir::serialize() (schemas/snapshot.fbs): the file's bytes are mapped
// directly into the process's address space (`mmap` on POSIX,
// `MapViewOfFile` on Windows) rather than read into a heap buffer, so
// opening a file and reading its header/index fields costs O(1)-ish
// relative to board size -- unlike deserialize_board(), which
// materializes every entity in every layer into owning containers
// (docs/format-spec.md -- Zero-copy load). Call materialize() to opt into
// that full cost once a caller actually needs more than the header.
class BoardFileView {
public:
  enum class VerifyPolicy : uint8_t {
    // Run FlatBuffers structural verification over the mapped bytes
    // before returning from open() (the default): a corrupt/malformed
    // file is rejected (FormatError) instead of being left to crash or
    // read out of bounds on first access. Verification cost scales with
    // the file's structural size, so this makes open() itself scale with
    // board size, even though header access after a successful open()
    // does not.
    Verify,
    // Skip verification, so open() is itself O(1)-ish regardless of
    // board size. Only safe for input whose provenance is already
    // trusted (e.g. a file this process just wrote) -- an untrusted or
    // externally-sourced file must use Verify.
    Skip,
  };

  [[nodiscard]] static BoardFileView open(const std::filesystem::path& path,
                                          VerifyPolicy policy = VerifyPolicy::Verify);

  ~BoardFileView();
  BoardFileView(BoardFileView&& other) noexcept;
  BoardFileView& operator=(BoardFileView&& other) noexcept;
  BoardFileView(const BoardFileView&) = delete;
  BoardFileView& operator=(const BoardFileView&) = delete;

  // Header/index access. Each of these reads a fixed field or a vector's
  // length directly from the mapped bytes -- none materializes a single
  // entity, so cost does not grow with board size.
  [[nodiscard]] FormatVersion format_version() const;
  [[nodiscard]] size_t geometry_byte_size() const;
  [[nodiscard]] size_t connectivity_byte_size() const;
  [[nodiscard]] size_t stackup_byte_size() const;
  [[nodiscard]] size_t extension_count() const;
  [[nodiscard]] size_t passthrough_blob_count() const;

  // The full, expensive path: materializes every entity in every layer
  // into an owning BoardSnapshot, exactly as deserialize_board() would
  // from a heap-read copy of the same bytes.
  [[nodiscard]] BoardSnapshot materialize() const;

private:
  BoardFileView() = default;
  void release() noexcept;

  void* mapped_ = nullptr;
  size_t mapped_size_ = 0;
#ifdef _WIN32
  // HANDLE, kept as void* so <windows.h> never appears in a public header
  // (docs/architecture.md -- Public-header hygiene).
  void* file_handle_ = nullptr;
  void* mapping_handle_ = nullptr;
#else
  int file_descriptor_ = -1;
#endif
};

} // namespace pcbir

#endif // PCBIR_BOARD_FILE_VIEW_HPP
