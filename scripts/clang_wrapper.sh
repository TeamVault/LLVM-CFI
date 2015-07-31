#!/bin/bash

SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

source "$SCRIPTS_DIR/bash_library.sh"

ENABLE_COMPILER_OPT=$("$SCRIPTS_DIR/config.py" ENABLE_COMPILER_OPT)
if [[ $? -ne 0 ]]; then exit 1; fi

run_compile_command() {
  local -a args=("$@")

  local cInd=$(indexOf "-c" "${args[@]}")
  local let "sInd=${cInd} + 1"
  local src="${args[$sInd]}"

  if [[ $(endswith "$src" ".c") == "true" ]]; then
    local -a new_args=("-x" "c" "${args[@]}")
  else
    local -a new_args=("${args[@]}")
  fi

  local CXX=$("$SCRIPTS_DIR/config.py" CXX)
  local CXX_FLAGS=$("$SCRIPTS_DIR/config.py" CXX_FLAGS)

  >&2 echo "CC: $CXX $CXX_FLAGS ${new_args[@]}"
  $CXX $CXX_FLAGS "${new_args[@]}"
}

run_link_command() {
  local CXX=$("$SCRIPTS_DIR/config.py" CXX)
  local LD=$("$SCRIPTS_DIR/config.py" LD)
  local LD_FLAGS=$("$SCRIPTS_DIR/config.py" LD_FLAGS)
  local LD_PLUGIN=$("$SCRIPTS_DIR/config.py" LD_PLUGIN)
  local SD_LIB_FOLDERS=$("$SCRIPTS_DIR/config.py" SD_LIB_FOLDERS)
  local SD_LIBS=$("$SCRIPTS_DIR/config.py" SD_LIBS)

  local -a args=($($CXX -### -flto "${@}" 2>&1 | tail -n 1 | sed 's/\"//g'))
  >&2 echo "LD: LD_PRELOAD=/lib/x86_64-linux-gnu/libpthread.so.0 gdb -ex run --args  $LD $LD_FLAGS $SD_LIB_FOLDERS ${args[@]:1} $SD_LIBS $LD_PLUGIN"
  $LD $LD_FLAGS $SD_LIB_FOLDERS ${args[@]:1} $SD_LIBS $LD_PLUGIN
}

declare -a sd_args=()

# if we don't enable O2 opt
if [[ ${ENABLE_COMPILER_OPT} == "False" ]]; then
  # remove parameters starting with -O
  for var in "$@"; do
    if [[ $(substring $var "-O") == "true" ]]; then
      continue
    else
      sd_args=("${sd_args[@]}" "$var")
    fi
  done
else
  sd_args=("$@")
fi

if [[ $(containsElement "-c" "${sd_args[@]}") == "1" ]]; then
  run_compile_command "${sd_args[@]}"
else
  run_link_command "${sd_args[@]}"
fi
