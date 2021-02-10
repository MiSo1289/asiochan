import re

from conans import CMake, ConanFile, tools


def get_version():
    try:
        content = tools.load("CMakeLists.txt")
        version = re.search("set\\(ASIOCHAN_VERSION (.*)\\)", content).group(1)
        return version.strip()
    except OSError:
        return None


class AsioChan(ConanFile):
    name = "asiochan"
    version = get_version()
    revision_mode = "scm"
    description = "C++20 coroutine channels for ASIO"
    homepage = "https://github.com/MiSo1289/asiochan"
    url = "https://github.com/MiSo1289/asiochan"
    license = "MIT"
    generators = "cmake"
    settings = ("os", "compiler", "arch", "build_type")
    exports_sources = (
        "examples/*",
        "include/*",
        "tests/*",
        "CMakeLists.txt",
    )
    build_requires = (
        # Unit-test framework
        "catch2/2.13.3",
    )
    options = {
        "asio": ["boost", "standalone"]
    }
    default_options = {
        "asio": "boost",
    }

    def requirements(self):
        if self.options.asio == "boost":
            self.requires("boost/1.75.0")
        else:
            self.requires("asio/1.18.1")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["ASIOCHAN_USE_STANDALONE_ASIO"] = self.options.asio == "standalone"

        cmake.configure()
        cmake.build()

        if tools.get_env("CONAN_RUN_TESTS", True):
            cmake.test()

    def package(self):
        self.copy("*.hpp", dst="include", src="include")

    def package_id(self):
        self.info.header_only()

    def package_info(self):
        if self.options.asio == "standalone":
            self.cpp_info.defines = ["ASIOCHAN_USE_STANDALONE_ASIO"]
