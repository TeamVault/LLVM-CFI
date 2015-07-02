#!/bin/bash

containsElement () {
  local e
  for e in "${@:2}"; do
    [[ "$e" == "$1" ]] && return 0
  done
  return 1
}

run_benchmarks() {
  local CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

  local -a benchmarks=('simp0' 'simp1' 'rtti_1' 'ott' 'only_mult' 'only_virt'
  'my_ex1' 'abi_ex' 'single_template' 'member_ptr' 'md_test' 'static_lib'
  'nonvirtual_covariant_thunks' 'virtual_covariant_thunks'
  'namespace_1' 'namespace_2' 'namespace_3')

  local -a neg_benchs=('bad_cast')

  # add the negative benchmarks as well
  local -a benchmarks=("${benchmarks[@]}" "${neg_benchs[@]}")

  # if an argument is not given, run all the benchmarks
  # otherwise run the given ones
  if [[ $# -gt 0 ]]; then
    local -a benchmarks=($@)
  fi

  local b
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

      containsElement "$b" "${neg_benchs[@]}"
      local isNeg=$?
      if [[ $isNeg -eq 0 ]]; then
        ./main 2>&1 > /dev/null
        if [[ $? -eq 0 ]]; then echo "neg bench $b should fail!"; return 1; fi
        popd > /dev/null
        continue
      fi

      ./main 2>&1 > /tmp/sd_run.txt
      if [[ $? -ne 0 ]]; then echo "sd run fail"; return 1; fi

      diff /tmp/normal_run.txt /tmp/sd_run.txt
      if [[ $? -ne 0 ]]; then echo "output mismatch"; return 1; fi

      rm -f /tmp/{normal,sd}_run.txt

      if [[ $("$CUR_DIR/../scripts/config.py" ENABLE_SD) == "True" ]]; then
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

