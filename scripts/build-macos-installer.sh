#!/usr/bin/env bash
# =============================================================================
#  build-macos-installer.sh — build the whysho EQ1 macOS installer (.pkg)
# =============================================================================
#
#  Builds a flat distribution .pkg using pkgbuild (per component) + productbuild
#  (distribution XML), installing:
#
#    VST3 (.vst3)      -> /Library/Audio/Plug-Ins/VST3
#    Audio Unit (.component) -> /Library/Audio/Plug-Ins/Components
#
#  Output: dist/WhyshoEQ1-<version>-macOS.pkg
#
#  Code signing and notarization are OPTIONAL and gated on environment
#  variables / secrets. When they are absent the script prints a warning and
#  produces an UNSIGNED package so local and CI builds still succeed.
#
#  Signing / notarization environment (all optional):
#    APPLE_CERT             Base64 of a .p12 holding the "Developer ID
#                           Application" and "Developer ID Installer" certs.
#    APPLE_CERT_PWD         Password for the .p12.
#    APPLE_TEAM_ID          Apple Developer Team ID (e.g. ABCDE12345).
#    APPLE_SIGN_ID_APP      Override for the Developer ID Application identity.
#    APPLE_SIGN_ID_INSTALLER Override for the Developer ID Installer identity.
#    AC_NOTARY_PROFILE      notarytool keychain profile name (preferred), OR:
#    APPLE_ID + AC_PASSWORD Apple ID + app-specific password (+ APPLE_TEAM_ID).
#
#  Usage:
#    scripts/build-macos-installer.sh [--version X.Y.Z] [--build-dir DIR] [--output-dir DIR]
# =============================================================================

set -euo pipefail

# --- Resolve repo-relative paths (robust to the working directory) -----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VERSION=""
BUILD_DIR=""
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)    VERSION="$2"; shift 2 ;;
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help)    grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -z "${VERSION}" ]]    && VERSION="$("${SCRIPT_DIR}/version.sh")"
[[ -z "${BUILD_DIR}" ]]  && BUILD_DIR="${REPO_ROOT}/build"
[[ -z "${OUTPUT_DIR}" ]] && OUTPUT_DIR="${REPO_ROOT}/dist"

# Absolutize.
BUILD_DIR="$(mkdir -p "${BUILD_DIR}" 2>/dev/null; cd "${BUILD_DIR}" && pwd)"
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"

ARTEFACTS_DIR="${BUILD_DIR}/WhyshoEQ1_artefacts/Release"
VST3_SRC="${ARTEFACTS_DIR}/VST3/whysho EQ1.vst3"
AU_SRC="${ARTEFACTS_DIR}/AU/whysho EQ1.component"

PRODUCT_NAME="whysho EQ1"
PKG_ID_VST3="com.whysho.eq1.vst3"
PKG_ID_AU="com.whysho.eq1.au"
FINAL_PKG="${OUTPUT_DIR}/WhyshoEQ1-${VERSION}-macOS.pkg"

echo "==> whysho EQ1 macOS installer"
echo "    Version   : ${VERSION}"
echo "    RepoRoot  : ${REPO_ROOT}"
echo "    BuildDir  : ${BUILD_DIR}"
echo "    OutputDir : ${OUTPUT_DIR}"

# --- Verify artefacts ---------------------------------------------------------
for p in "${VST3_SRC}" "${AU_SRC}"; do
    if [[ ! -d "${p}" ]]; then
        echo "ERROR: expected build artefact not found: ${p}" >&2
        echo "       Build the plugin first (cmake --build build --config Release)." >&2
        exit 1
    fi
done

# --- Working directories ------------------------------------------------------
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/whysho-pkg.XXXXXX")"
STAGE_VST3="${WORK_DIR}/stage-vst3"
STAGE_AU="${WORK_DIR}/stage-au"
COMPONENTS_DIR="${WORK_DIR}/components"
RESOURCES_DIR="${WORK_DIR}/resources"
mkdir -p "${STAGE_VST3}" "${STAGE_AU}" "${COMPONENTS_DIR}" "${RESOURCES_DIR}"

KEYCHAIN_PATH=""
cleanup() {
    [[ -n "${KEYCHAIN_PATH}" && -f "${KEYCHAIN_PATH}" ]] && security delete-keychain "${KEYCHAIN_PATH}" 2>/dev/null || true
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

# --- Stage bundles ------------------------------------------------------------
cp -R "${VST3_SRC}" "${STAGE_VST3}/"
cp -R "${AU_SRC}"   "${STAGE_AU}/"

# =============================================================================
#  Optional code signing of the plug-in bundles (Developer ID Application)
# =============================================================================
SIGN_ID_APP=""
SIGN_ID_INSTALLER=""

if [[ -n "${APPLE_CERT:-}" && -n "${APPLE_CERT_PWD:-}" ]]; then
    echo "==> Importing signing certificate into a temporary keychain"
    KEYCHAIN_PATH="${WORK_DIR}/whysho-signing.keychain-db"
    KEYCHAIN_PWD="$(openssl rand -base64 24)"
    CERT_P12="${WORK_DIR}/cert.p12"
    echo "${APPLE_CERT}" | base64 --decode > "${CERT_P12}"

    security create-keychain -p "${KEYCHAIN_PWD}" "${KEYCHAIN_PATH}"
    security set-keychain-settings -lut 21600 "${KEYCHAIN_PATH}"
    security unlock-keychain -p "${KEYCHAIN_PWD}" "${KEYCHAIN_PATH}"
    security import "${CERT_P12}" -k "${KEYCHAIN_PATH}" -P "${APPLE_CERT_PWD}" \
        -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/productbuild
    security set-key-partition-list -S apple-tool:,apple: -s -k "${KEYCHAIN_PWD}" "${KEYCHAIN_PATH}" >/dev/null
    # Prepend our keychain to the search list so identities resolve.
    security list-keychains -d user -s "${KEYCHAIN_PATH}" $(security list-keychains -d user | tr -d '"')

    # Resolve identities (allow explicit overrides).
    SIGN_ID_APP="${APPLE_SIGN_ID_APP:-$(security find-identity -v -p codesigning "${KEYCHAIN_PATH}" \
        | grep -o '"Developer ID Application:[^"]*"' | head -n1 | tr -d '"' || true)}"
    SIGN_ID_INSTALLER="${APPLE_SIGN_ID_INSTALLER:-$(security find-identity -v "${KEYCHAIN_PATH}" \
        | grep -o '"Developer ID Installer:[^"]*"' | head -n1 | tr -d '"' || true)}"

    if [[ -n "${SIGN_ID_APP}" ]]; then
        echo "==> Code-signing plug-in bundles with: ${SIGN_ID_APP}"
        for bundle in "${STAGE_VST3}/whysho EQ1.vst3" "${STAGE_AU}/whysho EQ1.component"; do
            codesign --force --deep --timestamp --options runtime \
                --sign "${SIGN_ID_APP}" "${bundle}"
            codesign --verify --strict --verbose=2 "${bundle}"
        done
    else
        echo "WARNING: no 'Developer ID Application' identity found; bundles left UNSIGNED." >&2
    fi
else
    echo "WARNING: APPLE_CERT / APPLE_CERT_PWD not set; building UNSIGNED package." >&2
fi

# =============================================================================
#  Build component packages
# =============================================================================
echo "==> pkgbuild: VST3 component"
pkgbuild --root "${STAGE_VST3}" \
    --identifier "${PKG_ID_VST3}" \
    --version "${VERSION}" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "${COMPONENTS_DIR}/WhyshoEQ1-VST3.pkg"

echo "==> pkgbuild: AU component"
pkgbuild --root "${STAGE_AU}" \
    --identifier "${PKG_ID_AU}" \
    --version "${VERSION}" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "${COMPONENTS_DIR}/WhyshoEQ1-AU.pkg"

# =============================================================================
#  Prepare productbuild resources + distribution XML
# =============================================================================
cp "${REPO_ROOT}/packaging/EULA.txt"          "${RESOURCES_DIR}/license.txt"
cp "${REPO_ROOT}/packaging/macos/welcome.html"    "${RESOURCES_DIR}/welcome.html"
cp "${REPO_ROOT}/packaging/macos/conclusion.html" "${RESOURCES_DIR}/conclusion.html"
# Optional background image (ignored if absent).
[[ -f "${REPO_ROOT}/packaging/macos/background.png" ]] && \
    cp "${REPO_ROOT}/packaging/macos/background.png" "${RESOURCES_DIR}/background.png"

DIST_XML="${WORK_DIR}/distribution.xml"
sed "s/__VERSION__/${VERSION}/g" \
    "${REPO_ROOT}/packaging/macos/distribution.xml" > "${DIST_XML}"

# =============================================================================
#  productbuild (unsigned), then optional productsign
# =============================================================================
UNSIGNED_PKG="${WORK_DIR}/WhyshoEQ1-unsigned.pkg"
echo "==> productbuild"
productbuild --distribution "${DIST_XML}" \
    --resources "${RESOURCES_DIR}" \
    --package-path "${COMPONENTS_DIR}" \
    "${UNSIGNED_PKG}"

if [[ -n "${SIGN_ID_INSTALLER}" ]]; then
    echo "==> productsign with: ${SIGN_ID_INSTALLER}"
    productsign --sign "${SIGN_ID_INSTALLER}" "${UNSIGNED_PKG}" "${FINAL_PKG}"
else
    [[ -n "${APPLE_CERT:-}" ]] && \
        echo "WARNING: no 'Developer ID Installer' identity found; installer left UNSIGNED." >&2
    cp "${UNSIGNED_PKG}" "${FINAL_PKG}"
fi

# =============================================================================
#  Optional notarization + stapling
# =============================================================================
notarize() {
    if [[ -n "${AC_NOTARY_PROFILE:-}" ]]; then
        echo "==> Notarizing with keychain profile: ${AC_NOTARY_PROFILE}"
        xcrun notarytool submit "${FINAL_PKG}" --keychain-profile "${AC_NOTARY_PROFILE}" --wait
    elif [[ -n "${APPLE_ID:-}" && -n "${AC_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
        echo "==> Notarizing with Apple ID: ${APPLE_ID}"
        xcrun notarytool submit "${FINAL_PKG}" \
            --apple-id "${APPLE_ID}" --password "${AC_PASSWORD}" --team-id "${APPLE_TEAM_ID}" --wait
    else
        echo "WARNING: notarization credentials not set (AC_NOTARY_PROFILE or APPLE_ID/AC_PASSWORD/APPLE_TEAM_ID); skipping." >&2
        return 1
    fi
}

if [[ -n "${SIGN_ID_INSTALLER}" ]]; then
    if notarize; then
        echo "==> Stapling notarization ticket"
        xcrun stapler staple "${FINAL_PKG}"
        xcrun stapler validate "${FINAL_PKG}" || true
    fi
else
    echo "WARNING: package is unsigned; skipping notarization." >&2
fi

echo ""
echo "==> Done. Installer: ${FINAL_PKG}"
