// SPDX-License-Identifier: Apache-2.0
#include "pcbir/version.h"

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// Token stringizing is a preprocessor-only operation; it has no constexpr
// equivalent.
#define PCBIR_STRINGIZE_(x) #x
#define PCBIR_STRINGIZE(x) PCBIR_STRINGIZE_(x)
// NOLINTEND(cppcoreguidelines-macro-usage)

const char* pcbir_version_string(void) {
  return PCBIR_STRINGIZE(PCBIR_VERSION_MAJOR) "." PCBIR_STRINGIZE(
      PCBIR_VERSION_MINOR) "." PCBIR_STRINGIZE(PCBIR_VERSION_PATCH);
}
