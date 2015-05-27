#!/bin/bash

set -e

pushd /tmp/dot

rm -f *.png

for dot in *.dot; do
	dot -Tpng $dot -o ${dot}.png
done

popd

xdg-open $(ls /tmp/dot/*.png | head -n 1) &
