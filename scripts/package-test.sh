#!/bin/sh
# Builds Fomoxa with CMake, installs it into a throwaway prefix, then
# configures tests/package against that prefix and runs what it produced.
#
# Everything here happens from a consumer's seat: tests/package never sees the
# source tree, only the install. That is what makes this catch a header that
# was never installed, a broken exported target, and - on Windows - a missing
# ws2_32 on the interface.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=${TMPDIR:-/tmp}/fomoxa-package-test.$$
prefix=$work/prefix

trap 'rm -rf "$work"' EXIT
mkdir -p "$work"

echo "--- configure and build the library"
cmake -S "$root" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$work/build" --config Release

echo "--- install into $prefix"
cmake --install "$work/build" --config Release

echo "--- the annotation header must not have been installed"
if [ -e "$prefix/include/fomoxa.h" ]; then
    echo "fomoxa.h was installed; it is copy-only and must stay out of the package" >&2
    exit 1
fi

echo "--- configure the consumer against the install only"
cmake -S "$root/tests/package" -B "$work/consumer" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$work/consumer" --config Release

echo "--- run the consumers"
for exe in "$work/consumer/consume_c" "$work/consumer/consume_cpp" \
           "$work/consumer/Release/consume_c.exe" "$work/consumer/Release/consume_cpp.exe" \
           "$work/consumer/consume_c.exe" "$work/consumer/consume_cpp.exe"; do
    if [ -x "$exe" ]; then
        "$exe"
    fi
done

echo "--- package test ok"
