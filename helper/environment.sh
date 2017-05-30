#!/bin/bash

DIR="$( cd "$( dirname "$0" )" && pwd )"
export PREFIX=$DIR/prefix

export PATH=$PREFIX/bin:$PATH
export LD_LIBRARY_PATH=$PREFIX/lib
export LLVM_CONFIG=$DIR/source/llvm/scripts/config.py
