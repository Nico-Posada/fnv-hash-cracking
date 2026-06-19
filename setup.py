import glob
import os
import sys

from setuptools import Extension, find_packages, setup
from setuptools.command.build_ext import build_ext


deps_dir = os.environ.get("FNVCRACK_DEPS")
coverage_enabled = os.environ.get("FNVCRACK_COVERAGE") == "1"
extra_compile_args = ["/O2"] if sys.platform == "win32" else ["-O3", "-Wall"]
extra_link_args = []
include_dirs = ["src"]
library_dirs = []

if coverage_enabled and sys.platform != "win32":
    extra_compile_args = ["-O0", "-g", "--coverage", "-Wall"]
    extra_link_args = ["--coverage"]

if deps_dir:
    include_dirs.append(os.path.join(deps_dir, "include"))
    library_dirs.append(os.path.join(deps_dir, "lib"))


def _portable_compiler_args(args):
    blocked_prefixes = ("-march=", "-mtune=", "-mcpu=")
    return [arg for arg in args if not arg.startswith(blocked_prefixes)]


class PortableBuildExt(build_ext):
    def build_extensions(self):
        for attr in ("compiler", "compiler_so", "compiler_cxx"):
            args = getattr(self.compiler, attr, None)
            if args:
                setattr(self.compiler, attr, _portable_compiler_args(args))

        super().build_extensions()


fnvcrack_extension = Extension(
    "fnvcrack._fnvcrack",
    sources=[
        *glob.glob("python/*.c"),
        *[src for src in glob.glob("src/*.c") if "main" not in src],
    ],
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=["flint", "gmp", "mpfr"],
    define_macros=[("FNVCRACK_PYTHON_EXTENSION", "1")],
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)


setup(
    ext_modules=[fnvcrack_extension],
    cmdclass={"build_ext": PortableBuildExt},
    package_dir={"": "python"},
    packages=find_packages("python"),
)
