#!/bin/sh
# Builds the release files of all module variants into dist/:
#   lgal_doser-<V>.bin       app image: OTA update and update manifest
#   lgal_doser-<V>-full.bin  bootloader, partition table, OTA data and app
#                                in one file, for the first USB flash at 0x0
#   manifest.json                update manifest (tools/make_manifest.py)
#   LICENSE, THIRD-PARTY-NOTICES.txt
#
#   tools/release.sh                          all variants, version from version.txt
#   VARIANTS="N16R8" tools/release.sh         only some
#   NOTES=https://…/releases/tag/v0.2.0 tools/release.sh
#
# Needs the ESP-IDF environment (export.sh). Each variant builds in its own
# directory build-<V>/ with its own sdkconfig.
set -eu
cd "$(dirname "$0")/.."
VARIANTS="${VARIANTS:-N8R8 N16R8 N32R8}"
VERSION="$(tr -d ' \n' < version.txt)"
rm -rf dist
mkdir dist
for v in $VARIANTS; do
    echo "=== $v"
    idf.py -B "build-$v" -DVARIANT="$v" build
    grep -q "^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=" "build-$v/sdkconfig"
    cp "build-$v/lgal_doser.bin" "dist/lgal_doser-$v.bin"
    idf.py -B "build-$v" -DVARIANT="$v" merge-bin -o "$PWD/dist/lgal_doser-$v-full.bin"
done
python3 tools/make_manifest.py dist $VARIANTS --version "$VERSION" ${NOTES:+--notes "$NOTES"}
cp LICENSE THIRD-PARTY-NOTICES.txt dist/
ls -l dist
