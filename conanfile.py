# SPDX-License-Identifier: Apache-2.0
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class PcbIrConan(ConanFile):
    name = "pcbir"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"

    # Mirror the CMake PCBIR_BUILD_* flags (see top-level CMakeLists.txt) so
    # `conan install` only pulls what the requested build actually needs.
    options = {
        "with_python": [True, False],
        "with_viz": [True, False],
        "with_bench": [True, False],
        "with_fuzz": [True, False],
    }
    default_options = {
        "with_python": False,
        "with_viz": False,
        "with_bench": False,
        "with_fuzz": False,
    }

    def requirements(self):
        # Lean core: every build pulls exactly these two, regardless of
        # option settings.
        self.requires("flatbuffers/25.12.19")
        self.requires("clipper2/2.0.1")

        if self.options.with_python:
            self.requires("pybind11/3.0.1")
        if self.options.with_bench:
            self.requires("benchmark/1.9.5")
        if self.options.with_viz:
            self.requires("bgfx/1.129.8930-495")
            # wgpu-native has no ConanCenter recipe as of writing. The
            # PCBIR_BUILD_VIZ path will need a vendored or FetchContent
            # source when GPU visualization work starts (Post-MVP).
        # with_fuzz uses the compiler's bundled libFuzzer (via
        # -fsanitize=fuzzer), not a Conan package.

    def build_requirements(self):
        self.test_requires("catch2/3.15.2")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        toolchain = CMakeToolchain(self)
        toolchain.generate()

    def layout(self):
        cmake_layout(self)
