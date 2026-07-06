#!/bin/bash
# Build a signed macOS installer (.pkg) for ToneFill (VST3 + AU).
#
# Signs the plugin bundles with "Developer ID Application" (hardened runtime + secure timestamp)
# and the installer package with "Developer ID Installer". Run AFTER building the plugins:
#   cmake --build build --target ToneFillPlugin_VST3 ToneFillPlugin_AU -j
#
# Notarization (optional, for distribution outside your own machines) is a separate step, see the
# commented block at the bottom - it needs your Apple ID + an app-specific password.
set -euo pipefail

VERSION="${1:-1.0.0}"
APP_ID="Developer ID Application: Jakub Juchniewicz (5LSJ6C76Q2)"
INST_ID="Developer ID Installer: Jakub Juchniewicz (5LSJ6C76Q2)"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$ROOT/build/src/plugin/ToneFillPlugin_artefacts/RelWithDebInfo"
VST3="$ART/VST3/ToneFill.vst3"
AU="$ART/AU/ToneFill.component"
DIST="$ROOT/dist"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

[ -d "$VST3" ] || { echo "Missing VST3: $VST3 (build first)"; exit 1; }
[ -d "$AU" ]   || { echo "Missing AU: $AU (build first)"; exit 1; }
mkdir -p "$DIST"

echo "==> Codesigning plugin bundles (Developer ID Application, hardened runtime)"
for B in "$VST3" "$AU"; do
    codesign --force --deep --options runtime --timestamp --sign "$APP_ID" "$B"
    codesign --verify --deep --strict --verbose=1 "$B"
done

echo "==> Staging install roots (system-wide /Library/Audio/Plug-Ins)"
mkdir -p "$STAGE/vst3/Library/Audio/Plug-Ins/VST3" "$STAGE/au/Library/Audio/Plug-Ins/Components"
cp -R "$VST3" "$STAGE/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$AU"   "$STAGE/au/Library/Audio/Plug-Ins/Components/"

echo "==> Building component packages"
pkgbuild --root "$STAGE/vst3" --identifier com.cactuzzsound.tonefill.vst3 --version "$VERSION" \
         --install-location "/" "$STAGE/ToneFill-VST3.pkg"
pkgbuild --root "$STAGE/au" --identifier com.cactuzzsound.tonefill.au --version "$VERSION" \
         --install-location "/" "$STAGE/ToneFill-AU.pkg"

cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>ToneFill $VERSION</title>
    <organization>com.cactuzzsound</organization>
    <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="ToneFill VST3 (with ARA)">
        <pkg-ref id="com.cactuzzsound.tonefill.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="ToneFill AU (Logic Pro X)">
        <pkg-ref id="com.cactuzzsound.tonefill.au"/>
    </choice>
    <pkg-ref id="com.cactuzzsound.tonefill.vst3" version="$VERSION">ToneFill-VST3.pkg</pkg-ref>
    <pkg-ref id="com.cactuzzsound.tonefill.au" version="$VERSION">ToneFill-AU.pkg</pkg-ref>
</installer-gui-script>
XML

echo "==> Building signed product installer (Developer ID Installer)"
productbuild --distribution "$STAGE/distribution.xml" --package-path "$STAGE" \
             --sign "$INST_ID" --timestamp "$DIST/ToneFill-$VERSION.pkg"

echo "==> Verifying installer signature"
pkgutil --check-signature "$DIST/ToneFill-$VERSION.pkg" | sed -n '1,6p'
echo "==> Done: $DIST/ToneFill-$VERSION.pkg"

# --- Optional: notarize + staple (needed only to distribute to OTHER machines) ---
# xcrun notarytool submit "$DIST/ToneFill-$VERSION.pkg" \
#     --apple-id "you@icloud.com" --team-id 5LSJ6C76Q2 --password "app-specific-password" --wait
# xcrun stapler staple "$DIST/ToneFill-$VERSION.pkg"
