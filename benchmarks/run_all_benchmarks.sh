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


  # TODO: Add  'dyn_link1'
  local -a benchmarks2=('abi_ex'
                       'multiple_secondary'
                       'my_ex1'
                       'only_mult2'
                       'simp1'
                       'virtual_mostly_empty_diamond'
                       'multiple_secondary1'
                       'namespace_1'
                       'only_virt'
                       'simp_cast'
                       'double_virtual_diamond'
                       'multiple_secondary2'
                       'namespace_2'
                       'ott'
                       'simp_virt'
                       'multiple_secondary_diamond'
                       'namespace_3'
                       'partially_empty_virtual_diamond'
                       'single_template'
                       'md_test'
                       'multiple_secondary_partially_virtual_diamond'
                       'nonvirtual_covariant_thunks'
                       'quaternary_diamond'
                       'static_lib'
                       'member_ptr'
                       'multiple_secondary_virtual_diamond'
                       'non_virtual_diamond_with_virtual_ancestor'
                       'rtti_1'
                       'virtual_covariant_thunks'
                       'member_ptr2'
                       'multiple_virtual'
                       'only_mult'
                       'simp0'
                       'virtual_diamond'
                       'virtual_with_virtual_primary_base'
                       'shrink_wrap_paper_example')

  local -a benchmarks=('shrink_wrap_paper_example')
                       #'shrink_wrap_paper_example_overwrite')

  local -a neg_benchs=('bad_cast'
                       'bad_multiple_inheritnace_cast'
                       'bad_mult_inh_sibling_cast'
                       'bad_shrinkwrap_ex' #this contains an invalid cast
                       'bad_sibling_cast_parent_method_call'
                       'bad_cast_info')

  # add the negative benchmarks as well
  #local -a benchmarks=("${benchmarks[@]}" "${neg_benchs[@]}")

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
      if [[ $? -ne 0 ]]; then echo "g++ compilation fail"; continue; fi

      echo "############################################################"
      echo "g++ running $b"

      ./main 2>&1 > /tmp/normal_run.txt
      if [[ $? -ne 0 ]]; then echo "g++ run fail"; continue; fi

      echo "############################################################"
      echo "sd compiling $b"

      make clean all > /dev/null
      if [[ $? -ne 0 ]]; then echo "sd compilation fail"; continue; fi

      echo "############################################################"
      echo "sd running $b"

      containsElement "$b" "${neg_benchs[@]}"
      local isNeg=$?
      if [[ $isNeg -eq 0 ]]; then
        ./main 2>&1 > /dev/null
        if [[ $? -eq 0 ]]; then echo "neg bench $b should fail!"; continue; fi
        python3 analyseEFI.py -w -o analysis.txt main
        popd > /dev/null
        continue
      fi

      ./main 2>&1 > /tmp/sd_run.txt
      if [[ $? -ne 0 ]]; then echo "sd run fail"; continue; fi

      python3 ../analyseELF.py -w -o analysis.txt main

      rm -f /tmp/{normal,sd}_run.txt

      if [[ $("$CUR_DIR/../scripts/config.py" ENABLE_SD) == "True" ]]; then
        if [[ `readelf -sW main | grep -vP ' _ZT(V|C)(S|N10__cxxabiv)' | grep -P ' _ZT(V|C)' | wc -l` != "0" ]]; then
            echo "Original vtables remain !!!"
            continue
          fi

        if [[ `readelf -sW main | grep ' _ZTv' | wc -l` != "0" ]]; then
          echo "Original vthunks remain !!!"
          continue
        fi
      else
        if [[ `readelf -sW main | grep '_SD_ZTV' | wc -l` != "0" ]]; then
          echo "Compiled without vtbl checks but has _SD_ZTV !!!"
          continue
        fi
        if [[ `readelf -sW main | grep '_SVT_ZTv' | wc -l` != "0" ]]; then
          echo "Compiled without vtbl checks but has duplicated vthunks !!!"
          continue
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
