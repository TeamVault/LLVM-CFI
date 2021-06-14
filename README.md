# About
This is the GitHub repository of the LLVM-CFI tool. This tool can be used to assess CFI policies and compare them against each other.

## Installation

1. get the llvm gold plugin and follow the installation instructions: https://llvm.org/docs/GoldPlugin.html
2. install ld.gold to ```/usr/bin/ld.gold```
3. compile LLVM-CFI with ```-DLLVM_BINUTILS_INCDIR=/path/to/binutils/include```
4. set environment variables:
```bash
    export PREFIX="/path/to/built/reckon"
    export CC="$PREFIX/bin/clang -flto -femit-vtbl-checks"
    export CXX="$PREFIX/bin/clang++ -flto -femit-vtbl-checks"
    export AR="$PREFIX/bin/ar"
    export NM="$PREFIX/bin/nm-new"
    export RANLIB=/bin/true
    export LDFLAGS="-fuse-ld=gold -Wl,-plugin-opt=sd-return"
```
## Usage

Compile any project with LLVM-CFI. If autotooled, make sure that the C/C++ compilers and flags outlined above are used.
LLVM-CFI will generate folders called "SDOutput" which contains the analysis results. The folders are created in the directory the
linker was called in. Use 
```bash
    find . -type d -name "SDOutput"
```
to find the data.
