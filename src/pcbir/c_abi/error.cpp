// SPDX-License-Identifier: Apache-2.0
#include "pcbir/c_abi/error.hpp"

#include <string>

namespace pcbir::c_abi {

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local std::string g_last_error;
} // namespace

void set_last_error(const char* message) {
  g_last_error = message != nullptr ? message : "";
}

} // namespace pcbir::c_abi

const char* pcbir_last_error(void) {
  return pcbir::c_abi::g_last_error.c_str();
}
