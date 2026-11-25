#!/bin/bash
# Simulateur PC de l'interface de la centrale : rend chaque page en PNG, sans la carte.
# Même code (firmware/central/src/ui.cpp), même LVGL 8.3.11, mêmes polices.
#
# Prérequis : gcc/g++, git, python3 + Pillow.
# Usage : tools/ui-sim/build.sh   → images dans tools/ui-sim/out/
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
CENTRAL=$ROOT/firmware/central
[ -d lvgl ] || git clone -q --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl lvgl
mkdir -p conf obj out
# Même configuration que la carte, sauf l'horloge (fournie par le simulateur).
sed 's/#define LV_TICK_CUSTOM 1/#define LV_TICK_CUSTOM 0/' "$CENTRAL/include/lv_conf.h" > conf/lv_conf.h
CF="-O1 -w -Iconf -Ilvgl -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE"
if [ ! -f obj/liblvgl.a ]; then
  echo "Compilation de LVGL (une seule fois)…"
  find lvgl/src -name "*.c" | while read f; do gcc $CF -c "$f" -o "obj/$(echo "$f" | md5sum | cut -c1-12).o"; done
  ar rcs obj/liblvgl.a obj/*.o
fi
for f in "$CENTRAL"/src/fonts/*.c; do gcc $CF -c "$f" -o "obj/font_$(basename "$f" .c).fo"; done
g++ -std=gnu++11 $CF -Istub -I"$CENTRAL/src" sim.cpp "$CENTRAL/src/ui.cpp" "$CENTRAL/src/camera_view.cpp" obj/*.fo obj/liblvgl.a -o sim
./sim
python3 - <<'PY'
from PIL import Image
import glob
for f in glob.glob('out/*.ppm'):
    Image.open(f).save(f[:-4] + '.png')
PY
rm -f out/*.ppm
echo "Images : $(pwd)/out"
