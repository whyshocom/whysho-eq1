#!/usr/bin/env bash
#
# version.sh — derive the project version for Whysho EQ1 packaging.
#
# Resolution order:
#   1. $WHYSHO_VERSION environment variable, if set (CI may pass it through).
#   2. The current git tag, if HEAD is exactly on a `vX.Y.Z` tag.
#   3. The most recent `vX.Y.Z` tag reachable from HEAD.
#   4. The fallback default `0.1.0`.
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

DEFAULT_VERSION="0.1.0"

strip_v() {
    # Remove a single leading 'v' or 'V' if present.
    printf '%s' "${1#[vV]}"
}

# 1. Explicit override via environment.
if [[ -n "${WHYSHO_VERSION:-}" ]]; then
    strip_v "${WHYSHO_VERSION}"
    exit 0
fi

# 2 & 3. Try git. Guard everything so a non-git checkout still yields the default.
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    # Exact tag on HEAD?
    if exact_tag="$(git describe --tags --exact-match 2>/dev/null)"; then
        if [[ "${exact_tag}" == v* || "${exact_tag}" == V* ]]; then
            strip_v "${exact_tag}"
            exit 0
        fi
    fi

    # Most recent v* tag reachable from HEAD (abbreviated form is fine; we only
    # use the tag name portion).
    if nearest_tag="$(git describe --tags --abbrev=0 --match 'v*' 2>/dev/null)"; then
        if [[ -n "${nearest_tag}" ]]; then
            strip_v "${nearest_tag}"
            exit 0
        fi
    fi
fi

# 4. Fallback.
printf '%s' "${DEFAULT_VERSION}"
