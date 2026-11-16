#!/bin/sh
# Regenerates the HomeMesh fonts with lv_font_conv (npm install -g lv_font_conv).
# Usage: ./generate.sh <path to LVGL source tree, v9.4.0>
#   Montserrat Medium/Bold (OFL) and Font Awesome 5 (OFL) ship with LVGL.
set -e
LVGL=${1:?path to lvgl}
FONTS=$LVGL/scripts/built_in_font
MEDIUM=$FONTS/Montserrat-Medium.ttf
BOLD=$LVGL/tests/src/test_files/fonts/Montserrat-Bold.ttf
AWESOME=$FONTS/FontAwesome5-Solid+Brands+Regular.woff
cd "$(dirname "$0")"

# Latin-1 (German umlauts, ß, ·, °) plus dashes, quotes, ellipsis and euro sign
TEXT=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC
# LVGL's built-in symbols (LV_SYMBOL_*) plus star, thumbtack, calendar, reply,
# signal, moon, hashtag and double check
SYMBOLS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,61445,61581,61747,62437,61458,61830,62098,62816

for size in 16 20 24 30 40; do
  lv_font_conv --bpp 4 --size $size --format lvgl --force-fast-kern-format \
    --font "$MEDIUM" -r $TEXT --font "$AWESOME" -r $SYMBOLS \
    --lv-font-name hm_font_$size -o hm_font_$size.c
done
for size in 30 40; do
  lv_font_conv --bpp 4 --size $size --format lvgl --force-fast-kern-format \
    --font "$BOLD" -r $TEXT --lv-font-name hm_bold_$size -o hm_bold_$size.c
done
# clock digits only, three sizes: the home screen picks one by how much else it shows
for size in 80 100 130; do
  lv_font_conv --bpp 4 --size $size --format lvgl --font "$BOLD" -r 0x30-0x3A \
    --lv-font-name hm_clock_$size -o hm_clock_$size.c
done
