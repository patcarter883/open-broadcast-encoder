#!/usr/bin/env bash
#
# package-linux.sh — build the self-extracting single-file Linux bundle
# (open-broadcast-encoder-linux-x86_64.run).
#
# The .run is a bash self-extractor: a launcher header followed by a gzipped
# tar of the executable. On first run (or a version bump) it extracts to
# ~/.cache/open-broadcast-encoder and exec's the binary; GStreamer and the NDI
# SDK are expected on the target system (see the header text below).
#
# Usage:
#   ./package-linux.sh                 # package the existing build/ binary
#   ./package-linux.sh --build         # configure + build Release first, then package
#   ./package-linux.sh --strip         # strip the binary before bundling (smaller)
#   ./package-linux.sh --bin PATH      # bundle a specific binary
#   ./package-linux.sh --build-dir DIR # build/look for the binary in DIR (default: build)
#   ./package-linux.sh --output FILE   # write to a specific .run path
#
set -euo pipefail

APPNAME="open-broadcast-encoder"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Keep the bundle version in sync with the project manifest.
VERSION="$(sed -n 's/.*"version-semver"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
    "${REPO_DIR}/vcpkg.json")"
VERSION="${VERSION:-0.0.0}"

BUILD_DIR="${REPO_DIR}/build"
BIN=""
DO_BUILD=0
DO_STRIP=0
OUTPUT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --build)     DO_BUILD=1 ;;
        --strip)     DO_STRIP=1 ;;
        --bin)       BIN="$2"; shift ;;
        --build-dir) BUILD_DIR="$2"; shift ;;
        --output)    OUTPUT="$2"; shift ;;
        -h|--help)
            # Print the leading comment block (skip the shebang, stop at the
            # first non-comment line) with the "# " prefix stripped.
            awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' \
                "${BASH_SOURCE[0]}"
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

: "${OUTPUT:=${REPO_DIR}/${APPNAME}-linux-x86_64.run}"
: "${BIN:=${BUILD_DIR}/${APPNAME}}"

if [ "${DO_BUILD}" -eq 1 ]; then
    echo ">> Configuring and building Release in ${BUILD_DIR}"
    cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -D CMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}" -j "$(nproc)"
fi

if [ ! -x "${BIN}" ]; then
    echo "ERROR: binary not found at ${BIN}" >&2
    echo "       run with --build, or point --bin at the executable." >&2
    exit 1
fi

# ---- Stage the binary under the name the launcher exec's --------------------
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT
cp "${BIN}" "${STAGE}/${APPNAME}"
chmod +x "${STAGE}/${APPNAME}"
if [ "${DO_STRIP}" -eq 1 ]; then
    strip --strip-unneeded "${STAGE}/${APPNAME}"
fi

# ---- Emit the self-extractor header -----------------------------------------
# Literal heredoc (quoted delimiter): nothing here is expanded by THIS script;
# every ${...}/$0/"$@" below is shell syntax for the generated .run itself.
# __APPVERSION__ is the sole placeholder, substituted afterwards.
HEADER="$(mktemp)"
cat > "${HEADER}" <<'EOF_HEADER'
#!/bin/bash
# open-broadcast-encoder — self-extracting single-file bundle
#
# Requirements on target CachyOS system:
#   pacman -S gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad
#   NDI SDK must be installed separately (provides libndi.so.6)
#
# Usage: ./open-broadcast-encoder-linux-x86_64.run [args...]
#        ./open-broadcast-encoder-linux-x86_64.run --reinstall   (force re-extract)

set -e

APPNAME="open-broadcast-encoder"
VERSION="__APPVERSION__"
CACHE_DIR="${HOME}/.cache/${APPNAME}"

if [ "${1}" = "--reinstall" ]; then
    rm -rf "${CACHE_DIR}"
    shift
fi

if [ ! -f "${CACHE_DIR}/.version" ] || [ "$(cat "${CACHE_DIR}/.version" 2>/dev/null)" != "${VERSION}" ]; then
    echo "Extracting ${APPNAME} ${VERSION}..."
    rm -rf "${CACHE_DIR}"
    mkdir -p "${CACHE_DIR}"
    ARCHIVE_START=$(awk '/^# __ARCHIVE_START__/{print NR+1; exit}' "$0")
    tail -n +"${ARCHIVE_START}" "$0" | tar -xzf - -C "${CACHE_DIR}"
    echo "${VERSION}" > "${CACHE_DIR}/.version"
fi

exec "${CACHE_DIR}/${APPNAME}" "$@"
exit 0
# __ARCHIVE_START__
EOF_HEADER
sed -i "s/__APPVERSION__/${VERSION}/" "${HEADER}"

# ---- Assemble: header + gzipped tarball -------------------------------------
ARCHIVE="$(mktemp)"
tar -czf "${ARCHIVE}" -C "${STAGE}" "./${APPNAME}"
cat "${HEADER}" "${ARCHIVE}" > "${OUTPUT}"
chmod +x "${OUTPUT}"
rm -f "${HEADER}" "${ARCHIVE}"

# ---- Verify the bundle round-trips to the exact binary we packaged ----------
START="$(awk '/^# __ARCHIVE_START__/{print NR+1; exit}' "${OUTPUT}")"
EXPECTED="$(sha256sum "${STAGE}/${APPNAME}" | cut -d' ' -f1)"
ACTUAL="$(tail -n +"${START}" "${OUTPUT}" | tar -xzO "./${APPNAME}" | sha256sum | cut -d' ' -f1)"
if [ "${EXPECTED}" != "${ACTUAL}" ]; then
    echo "ERROR: package verification failed (sha256 mismatch)" >&2
    exit 1
fi

echo ">> Built ${OUTPUT}"
echo "   version : ${VERSION}"
echo "   size    : $(du -h "${OUTPUT}" | cut -f1)"
echo "   binary  : $(du -h "${STAGE}/${APPNAME}" | cut -f1) (sha256 ${EXPECTED:0:12}…, verified)"
