#!/bin/bash

SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

source "$SCRIPTS_DIR/bash_library.sh"

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
  local LD_PLUGIN=$("$SCRIPTS_DIR/config.py" LD_PLUGIN)
  local SD_LIB_FOLDERS=$("$SCRIPTS_DIR/config.py" SD_LIB_FOLDERS)
  local SD_LIBS=$("$SCRIPTS_DIR/config.py" SD_LIBS)

  local -a args=($($CXX -### -flto "${@}" 2>&1 | tail -n 1 | sed 's/\"//g'))
  >&2 echo "LD: $LD $SD_LIB_FOLDERS ${args[@]:1} $SD_LIBS $LD_PLUGIN"
  $LD $SD_LIB_FOLDERS ${args[@]:1} $SD_LIBS $LD_PLUGIN
}

IS_COMPILE=$(containsElement "-c" "${@}")

if [[ "$IS_COMPILE" == "1" ]]; then
  run_compile_command "${@}"
else
  run_link_command "${@}"
fi
