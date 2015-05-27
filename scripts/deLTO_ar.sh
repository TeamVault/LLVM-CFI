#!/bin/bash

set -e

# scripts dir
CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CONFIG="$CUR_DIR/config.py"

if [[ $# -ne 1 ]]; then
  >&2 echo "usage: $(basename $0) <static lib>"
  exit -1
fi

# full path of the library
LIB_DIR=$(cd "$(dirname "$1")" && pwd)
LIB="$LIB_DIR/$(basename $1)"

# tmp dir to create the archive
TMP_DIR=`mktemp -d`

# full path of "ar"
AR="$CUR_DIR/ar"

# llvm linker
LLVM_BIN_DIR="$($CONFIG LLVM_BUILD_DIR)/Release+Asserts/bin"
LLC="${LLVM_BIN_DIR}/llc"

pushd $TMP_DIR > /dev/null

# get the files inside the archive
FILES=$($AR tP $LIB)

# lower each bitcode into machine code
for obj in $FILES; do
  >&2 echo "LLC: $obj"
  BN=$(basename $obj)
  $LLC -filetype=obj $obj -o=${BN}.sd.o
done

# create the new library with the .a.sd extension
NEW_LIB="${LIB}.sd"
/usr/bin/ar rcs "$NEW_LIB" *.sd.o

popd > /dev/null

# remove the temporary directory
rm -rf $TMP_DIR

