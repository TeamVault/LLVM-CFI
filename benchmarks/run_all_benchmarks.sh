#!/bin/bash

run_benchmarks() {
  local CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

  local -a benchmarks=('simp0' 'simp1' 'rtti_1' 'ott' 'only_mult' 'only_virt'
  'my_ex1' 'abi_ex' 'single_template' 'member_ptr' 'md_test' 'static_lib')

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
      if [[ $? -ne 0 ]]; then echo "g++ compilation fail"; return 1; fi

      echo "############################################################"
      echo "g++ running $b"

      ./main 2>&1 > /tmp/normal_run.txt
      if [[ $? -ne 0 ]]; then echo "g++ run fail"; return 1; fi

      echo "############################################################"
      echo "sd compiling $b"

      make clean all > /dev/null
      if [[ $? -ne 0 ]]; then echo "sd compilation fail"; return 1; fi

      echo "############################################################"
      echo "sd running $b"

      ./main 2>&1 > /tmp/sd_run.txt
      if [[ $? -ne 0 ]]; then echo "sd run fail"; return 1; fi

      diff /tmp/normal_run.txt /tmp/sd_run.txt
      if [[ $? -ne 0 ]]; then echo "output mismatch"; return 1; fi

      rm -f /tmp/{normal,sd}_run.txt

      if [[ $("$CUR_DIR/../scripts/config.py" ENABLE_CHECKS) == "True" ]]; then
        if [[ `readelf -sW main | grep -vP ' _ZT(V|C)(S|N10__cxxabiv)' | grep -P ' _ZT(V|C)' | wc -l` != "0" ]]; then
            echo "Original vtables remain !!!"
            return 1
          fi

        if [[ `readelf -sW main | grep ' _ZTv' | wc -l` != "0" ]]; then
          echo "Original vthunks remain !!!"
          return 1
        fi
      else
        if [[ `readelf -sW main | grep '_SD_ZTV' | wc -l` != "0" ]]; then
          echo "Compiled without vtbl checks but has _SD_ZTV !!!"
          return 1
        fi
        if [[ `readelf -sW main | grep '_SVT_ZTv' | wc -l` != "0" ]]; then
          echo "Compiled without vtbl checks but has duplicated vthunks !!!"
          return 1
        fi
      fi

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
}

run_benchmarks $@

