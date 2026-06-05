import glob

from setuptools import Extension, find_packages, setup


fnvcrack_extension = Extension(
    "fnvcrack._fnvcrack",
    sources=[
        *glob.glob("python/*.c"),
        *[src for src in glob.glob("src/*.c") if "main" not in src],
    ],
    include_dirs=["src"],
    libraries=["flint", "gmp"],
    define_macros=[("FNVCRACK_PYTHON_EXTENSION", "1")],
    extra_compile_args=["-march=native", "-O3", "-Wall"],
)


setup(
    ext_modules=[fnvcrack_extension],
    package_dir={"": "python"},
    packages=find_packages("python"),
)
