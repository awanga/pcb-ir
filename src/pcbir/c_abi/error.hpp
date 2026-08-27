// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_C_ABI_ERROR_HPP
#define PCBIR_C_ABI_ERROR_HPP

#include "pcbir/format_error.hpp"
#include "pcbir/pcbir.h"

#include <exception>
#include <utility>

// Internal to the C ABI implementation -- never installed as a public
// header (docs/rfcs/0001-stable-c-abi.md).
namespace pcbir::c_abi {

void set_last_error(const char* message);

// Runs `fn` (expected to return a pcbir_status_t itself for validation
// failures it detects directly, e.g. PCBIR_ERROR_INVALID_ARGUMENT) and
// translates any exception thrown out of it into a status code plus a
// last-error message, so no C++ exception ever crosses the extern "C"
// boundary. This is the one place that chain is written; every ABI
// function wraps its body in this instead of hand-copying the catch
// clauses (docs/rfcs/0001-stable-c-abi.md).
template <typename Fn> pcbir_status_t translate_exceptions(Fn&& fn) {
  try {
    return std::forward<Fn>(fn)();
  } catch (const FormatError& error) {
    set_last_error(error.what());
    return error.reason() == FormatError::Reason::UnsupportedVersion
               ? PCBIR_ERROR_UNSUPPORTED_VERSION
               : PCBIR_ERROR_CORRUPT_BUFFER;
  } catch (const std::exception& error) {
    set_last_error(error.what());
    return PCBIR_ERROR_UNKNOWN;
  } catch (...) {
    set_last_error("unknown error");
    return PCBIR_ERROR_UNKNOWN;
  }
}

} // namespace pcbir::c_abi

#endif // PCBIR_C_ABI_ERROR_HPP
