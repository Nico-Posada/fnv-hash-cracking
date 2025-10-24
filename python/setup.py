import sys
import os
import glob
import re

VERSION_PAT = re.compile(r"#define FNVCRACK_VERSION \"(.*?)\"")

if sys.platform == 'win32' and sys.version_info < (3, 12):
    from distutils.core import setup
    from distutils.extension import Extension
else:
    from setuptools import setup, Extension

# Move up to the base dir because setuptools hates ../
os.chdir(os.path.abspath('..'))

# os.makedirs('ext', exist_ok=True)
asan_flags = ['-fsanitize=address', '-fno-omit-frame-pointer', '-g', '-O1']

fnvcrack_extension = Extension(
    'fnvcrack',
    sources=[
        *[src for src in glob.glob('python/*.c')],
        *[src for src in glob.glob('src/*.c') if "main" not in src]
    ],
    include_dirs=[
        'src'
    ],
    libraries=[
        'flint',
        'gmp'
    ],
    extra_compile_args=[
        '-march=native',
        '-O3',
        '-flto',
        '-Wall'
    ],
    extra_link_args=[
        '-flto'
    ]# + asan_flags
)

with open("python/fnvcrack.c", "r") as f:
    version = VERSION_PAT.search(f.read()).group(1)

setup(
    name='fnvcrack',
    version=version,
    description='A high-performance FNV hash cracking library',
    ext_modules=[fnvcrack_extension],
    options={
        'build': {
            'build_lib': './python/ext'
        }
    }
)