#!/bin/bash

if [[ $# -ne 2 ]]; then
	echo "Prints the extra vtables/functions that the 2nd IR files have"
	echo "usage: $FUNCNAME <IR file 1> <IR file 2>"
	exit 1
fi

SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPTS_DIR/bash_library.sh"

FILE1=$(relative_to_full "$1")
FILE2=$(relative_to_full "$2")

if [[ ! -e $FILE1 || ! -e $FILE2 ]]; then
	echo "One of the files doesn't exists"
	exit 1
fi

pushd /tmp > /dev/null

grep -P '^@_ZTV.*=' $FILE1 | \
	sed 's/^@\(_ZTV[^ ]*\) =.*/\1/' | \
	sort > vtables1.txt
grep -P '^@_ZTV.*=' $FILE2 | \
	sed 's/^@\(_ZTV[^ ]*\) =.*/\1/' | \
	sort > vtables2.txt

EXTRA_VTBLS=$(comm -13 vtables1.txt vtables2.txt)
if [[ -n "$EXTRA_VTBLS" ]]; then
	echo "Extra vtables:"
	echo $EXTRA_VTBLS
fi

# extract the function names
grep -P '^define.*{' $FILE1 | \
	sed 's/^.*@\([^()]*\)(.*/\1/' | \
	sort > functions1.txt
grep -P '^define.*{' $FILE2 | \
	sed 's/^.*@\([^()]*\)(.*/\1/' | \
	sort > functions2.txt

EXTRA_FUNCS=$(comm -13 functions1.txt functions2.txt)

if [[ -n "$EXTRA_FUNCS" ]]; then
	echo "Extra functions:"
	echo "$EXTRA_FUNCS"
fi

popd > /dev/null
