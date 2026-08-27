// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_FORMAT_ERROR_HPP
#define PCBIR_FORMAT_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace pcbir {

// Thrown when a buffer fails FlatBuffers structural verification, or a
// BoardSnapshot's format_version carries an unsupported major version
// (docs/format-spec.md -- Versioning policy). One exception type across
// every layer's deserialize_*()/deserialize_board(), so a caller has a
// single thing to catch -- per CLAUDE.md's "prefer visible failure over
// silent fallback": a malformed buffer is reported, never silently
// accepted or left to crash the process.
class FormatError : public std::runtime_error {
public:
  // Which of the two conditions above was hit -- lets a caller (e.g. the C
  // ABI's error-translation shim, docs/c-abi.md) distinguish "corrupt data"
  // from "future file, upgrade the reader" without parsing what().
  enum class Reason : uint8_t {
    CorruptBuffer,
    UnsupportedVersion,
  };

  // Every existing call site reports a structural-verification failure, so
  // this single-argument constructor keeps defaulting to CorruptBuffer
  // rather than requiring every one of them to name a reason explicitly.
  explicit FormatError(const std::string& message)
      : std::runtime_error(message), reason_(Reason::CorruptBuffer) {}
  FormatError(Reason reason, const std::string& message)
      : std::runtime_error(message), reason_(reason) {}

  [[nodiscard]] Reason reason() const { return reason_; }

private:
  Reason reason_;
};

} // namespace pcbir

#endif // PCBIR_FORMAT_ERROR_HPP
