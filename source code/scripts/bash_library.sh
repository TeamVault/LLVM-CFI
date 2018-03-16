containsElement () {
  local e
  for e in "${@:2}"; do
    if [[ "$e" == "$1" ]]; then
      echo "1"
      return
    fi
  done
  echo "0"
}

indexOf() {
  local value="$1"
  local -a array=("${@:2}")
  local i

  for (( i = 0; i < ${#array[@]}; i++ )); do
     if [ "${array[$i]}" = "${value}" ]; then
         echo $i;
         return
     fi
  done
  echo "-1"
}

endswith() {
  local str="$1"
  local suffix="$2"

  if [[ "${str}" == *"${suffix}" ]]; then
      echo true
  else
      echo false
  fi
}

substring() {
  local str="$1"
  local suffix="$2"

  if [[ "${str}" == *"${suffix}"* ]]; then
    echo true
  else
    echo false
  fi
}

relative_to_full() {
  local FILE="$1"
  local DIR=$(dirname $FILE)
  local BASE=$(basename $FILE)
  pushd $DIR > /dev/null
  local FULL_DIR=$(pwd)
  popd > /dev/null
  echo "$FULL_DIR/$BASE"
}
