#!/bin/bash

set -e

declare -a benchmarks=('simp0' 'simp1' 'rtti_1' 'ott' 'only_mult' 'only_virt'
'my_ex1' 'abi_ex' 'single_template' 'member_ptr')

# if an argument is not given, run all the benchmarks
# otherwise run the given ones
if [[ $# -gt 0 ]]; then
  declare -a benchmarks=($@)
fi

for b in ${benchmarks[@]}; do
  if [[ -d $b ]]; then
    pushd $b > /dev/null

    echo "############################################################"
    echo "g++ compiling $b"

    NO_LTO=OK make clean all > /dev/null

    echo "############################################################"
    echo "g++ running $b"

    ./main 2>&1 > /tmp/normal_run.txt

    echo "############################################################"
    echo "sd compiling $b"

    make clean all > /dev/null

    echo "############################################################"
    echo "sd running $b"

    ./main 2>&1 > /tmp/sd_run.txt

    diff /tmp/normal_run.txt /tmp/sd_run.txt

    rm -f /tmp/{normal,sd}_run.txt

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
