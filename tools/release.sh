#!/bin/sh
# Builds the release files of every board and module variant into dist/:
#   lgal_doser-<B>-<V>.bin       app image: OTA update and update manifest
#   lgal_doser-<B>-<V>-full.bin  bootloader, partition table, OTA data and app
#                                in one file, for the first USB flash at 0x0
#   manifest.json                update manifest (tools/make_manifest.py)
#   LICENSE, THIRD-PARTY-NOTICES.txt
#
#   tools/release.sh                          all boards and variants, version from version.txt
#   BOARDS="d5v1" VARIANTS="N16R8" tools/release.sh   only some
#   NOTES=https://…/releases/tag/v0.2.0 tools/release.sh
#
# Needs the ESP-IDF environment (export.sh). Each build uses its own
# directory build-<B>-<V>/ with its own sdkconfig.
set -eu
cd "$(dirname "$0")/.."
BOARDS="${BOARDS:-$(for f in boards/*.cmake; do basename "$f" .cmake; done)}"
VARIANTS="${VARIANTS:-N8R8 N16R8 N32R8}"
VERSION="$(tr -d ' \n' < version.txt)"
rm -rf dist
mkdir dist
BUILDS=""
for b in $BOARDS; do
    for v in $VARIANTS; do
        echo "=== $b $v"
        idf.py -B "build-$b-$v" -DBOARD="$b" -DVARIANT="$v" build
        grep -q "^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=" "build-$b-$v/sdkconfig"
        cp "build-$b-$v/lgal_doser.bin" "dist/lgal_doser-$b-$v.bin"
        idf.py -B "build-$b-$v" -DBOARD="$b" -DVARIANT="$v" merge-bin -o "$PWD/dist/lgal_doser-$b-$v-full.bin"
        BUILDS="$BUILDS $b-$v"
    done
done
python3 tools/make_manifest.py dist $BUILDS --version "$VERSION" ${NOTES:+--notes "$NOTES"}
cp LICENSE THIRD-PARTY-NOTICES.txt dist/
ls -l dist
