// SPDX-License-Identifier: Apache-2.0
#include "pcbir/version.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("build skeleton is trivially sane", "[smoke]") {
  REQUIRE(1 + 1 == 2);
}

TEST_CASE("version string matches the header macros", "[smoke]") {
  REQUIRE(std::strcmp(pcbir_version_string(), "0.1.0") == 0);
}
