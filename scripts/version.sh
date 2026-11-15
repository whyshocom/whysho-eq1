#!/usr/bin/env bash
#
# version.sh — derive the project version for whysho EQ1 packaging.
#
# Resolution order:
#   1. $WHYSHO_VERSION environment variable, if set (CI may pass it through).
#   2. The version in CMakeLists.txt (`project(... VERSION X.Y.Z)`): the one
#      place the version is written, and what the plugin binary reports.
#
# With --check-tag, additionally fail if HEAD is on a `v*` tag that doesn't
# match, so a release can never ship an installer labelled differently from
# the plugin inside it.
#
# A leading `v` is stripped so the output is a bare semantic version
# (e.g. `1.2.3`), suitable for installer filenames and Inno Setup / pkgbuild.
#
# Usage:
#   VERSION="$(scripts/version.sh)"
#
# This script is intentionally dependency-free and safe to run from any
# directory inside the repository (it does not change the working directory).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMAKE_VERSION="$(sed -nE 's/^project\(.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "${SCRIPT_DIR}/../CMakeLists.txt" | head -n1)"
if [[ -z "${CMAKE_VERSION}" ]]; then
    echo "version.sh: could not read the version from CMakeLists.txt" >&2
    exit 1
fi

strip_v() {
    # Remove a single leading 'v' or 'V' if present.
    printf '%s' "${1#[vV]}"
}

# 1. Explicit override via environment.
if [[ -n "${WHYSHO_VERSION:-}" ]]; then
    strip_v "${WHYSHO_VERSION}"
    exit 0
fi

# 2. CMakeLists.txt, optionally checked against the tag on HEAD.
if [[ "${1:-}" == "--check-tag" ]] && command -v git >/dev/null 2>&1; then
    if tag="$(git describe --tags --exact-match --match 'v*' 2>/dev/null)"; then
        if [[ "$(strip_v "${tag}")" != "${CMAKE_VERSION}" ]]; then
            echo "version.sh: tag ${tag} does not match CMakeLists.txt version ${CMAKE_VERSION}" >&2
            exit 1
        fi
    fi
fi

printf '%s' "${CMAKE_VERSION}"
