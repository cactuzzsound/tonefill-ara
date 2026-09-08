#!/usr/bin/env bash
# Sign the built ToneFill AAX with PACE wraptool and install it for Pro Tools.
#
# One-time (caches your iLok password in the macOS keychain so it never appears in scripts):
#     wraptool sync --account <ILOK_ACCOUNT_ID> --password <YOUR_PASSWORD>
#
# Then, after every build:
#     scripts/sign_aax.sh <ILOK_ACCOUNT_ID>
#
set -euo pipefail

# wraptool: honour a WRAPTOOL env override; else pick the highest installed Eden/Fusion version.
# NOTE: the AAX Code Signing Tools "Lite" license ("Eden Tools") is served by the Fusion v5 SDK
# (EdenSDKLiteInstaller); the generic License Support v6 wraptool demands a full "PACE Tools" license.
# So for a Lite customer, point WRAPTOOL at the v5 wraptool.
WT="${WRAPTOOL:-$(ls -d /Applications/PACEAntiPiracy/Eden/Fusion/Versions/*/bin/wraptool 2>/dev/null | sort -V | tail -1)}"
[ -n "$WT" ] || WT="/Applications/PACEAntiPiracy/Eden/Fusion/Versions/6/bin/wraptool"
WCGUID="53B16D60-73F3-11F1-B005-005056920FF7"
SIGNID="Developer ID Application: Jakub Juchniewicz (5LSJ6C76Q2)"

ACCOUNT="${1:?usage: sign_aax.sh <ilok-account-id> [path-to-built.aaxplugin]}"
SRC="${2:-build/aax/ToneFill.aaxplugin}"  # native AudioSuite HostProcessor bundle (aax/)
DEST="/Library/Application Support/Avid/Audio/Plug-Ins/ToneFill.aaxplugin"

[ -d "$SRC" ] || { echo "AAX not found at: $SRC (build ToneFillPlugin_AAX first)"; exit 1; }

echo "Signing $SRC ..."
"$WT" sign --verbose --account "$ACCOUNT" --wcguid "$WCGUID" --signid "$SIGNID" --in "$SRC" --out "$DEST"
echo "Installed -> $DEST"
"$WT" verify --verbose --in "$DEST" | tail -8
