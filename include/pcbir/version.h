/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PCBIR_VERSION_H
#define PCBIR_VERSION_H

// Must stay preprocessor macros: consumers use them in `#if` feature checks,
// which constexpr/enum constants cannot participate in.
// NOLINTBEGIN(cppcoreguidelines-macro-usage, cppcoreguidelines-macro-to-enum,
// modernize-macro-to-enum)
#define PCBIR_VERSION_MAJOR 0
#define PCBIR_VERSION_MINOR 1
#define PCBIR_VERSION_PATCH 0
// NOLINTEND(cppcoreguidelines-macro-usage, cppcoreguidelines-macro-to-enum,
// modernize-macro-to-enum)

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the library version as "major.minor.patch". The returned pointer
 * is valid for the lifetime of the program. */
const char* pcbir_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* PCBIR_VERSION_H */
