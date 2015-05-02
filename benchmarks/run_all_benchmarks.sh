#!/bin/bash

set -e

declare -a benchmarks=('simp0' 'simp1' 'rtti_1' 'ott' 'only_mult')

# if an argument is not given, run all the benchmarks
# otherwise run the given ones
if [[ $# -gt 0 ]]; then
  declare -a benchmarks=($@)
fi

for b in ${benchmarks[@]}; do
  if [[ -d $b ]]; then
    pushd $b > /dev/null

    echo "############################################################"
    echo "compiling $b"

    make clean all

    echo "############################################################"
    echo "running $b"

    ./main

    popd > /dev/null
  else
    echo "############################################################"
    echo "$b does not exist"
  fi
done

echo
echo "############################################################"
echo "DONE !!!"
echo "############################################################"
