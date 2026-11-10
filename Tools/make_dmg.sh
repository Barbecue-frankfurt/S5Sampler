#!/bin/bash
# Builds S5 Sampler (VST3 + AU, Apple Silicon + Intel) and packs an installer into a .dmg.  Run on the Mac:
#   bash ~/Projekte/S5Sampler/Tools/make_dmg.sh
set -e
cd "$(dirname "$0")/.."
ROOT="$PWD"
B="$ROOT/build-release"
OUT="$HOME/Desktop"
VER="$(date +%Y.%m.%d)"
STAGE="$(mktemp -d)/S5 Sampler"
mkdir -p "$STAGE" "$B"

echo "== 1/4 Bauen (dauert beim ersten Mal einige Minuten) =="
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build "$B" --config Release --target S5Sampler_VST3 -j 8
cmake --build "$B" --config Release --target S5Sampler_AU -j 8

VST3="$(find "$B" -maxdepth 6 -name 'S5 Sampler.vst3' -path "*Release*" | head -1)"
AU="$(find "$B" -maxdepth 6 -name 'S5 Sampler.component' -path "*Release*" | head -1)"
[ -d "$VST3" ] || { echo "VST3 nicht gefunden"; exit 1; }
[ -d "$AU" ]   || { echo "AU nicht gefunden"; exit 1; }

echo "== 2/4 Signieren (ad hoc) =="
codesign --force --deep -s - "$VST3"
codesign --force --deep -s - "$AU"

echo "== 3/4 Installer-Paket =="
PK="$(mktemp -d)"
pkgbuild --component "$VST3" --install-location /Library/Audio/Plug-Ins/VST3       --identifier de.s5project.s5sampler.vst3 --version "$VER" "$PK/vst3.pkg"
pkgbuild --component "$AU"   --install-location /Library/Audio/Plug-Ins/Components  --identifier de.s5project.s5sampler.au   --version "$VER" "$PK/au.pkg"
cat > "$PK/dist.xml" <<X
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>S5 Sampler</title>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline><line choice="vst3"/><line choice="au"/></choices-outline>
  <choice id="vst3" title="VST3 (Cubase, Reaper, Ableton ...)"><pkg-ref id="de.s5project.s5sampler.vst3"/></choice>
  <choice id="au"   title="Audio Unit (Logic, GarageBand, MainStage)"><pkg-ref id="de.s5project.s5sampler.au"/></choice>
  <pkg-ref id="de.s5project.s5sampler.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="de.s5project.s5sampler.au">au.pkg</pkg-ref>
</installer-gui-script>
X
productbuild --distribution "$PK/dist.xml" --package-path "$PK" "$STAGE/S5 Sampler installieren.pkg"
cat > "$STAGE/LIES MICH.txt" <<X
S5 Sampler - Installation
1. "S5 Sampler installieren.pkg" mit Rechtsklick > Oeffnen starten (einmalig bestaetigen, die Datei ist nicht notarisiert).
2. VST3 und/oder Audio Unit waehlen und installieren.
3. DAW neu starten (Cubase: Plug-in-Manager > Neu einlesen; Logic: Plug-in-Manager > Zuruecksetzen).
Installiert nach /Library/Audio/Plug-Ins/VST3 und /Library/Audio/Plug-Ins/Components.
Entfernen: diese beiden Dateien "S5 Sampler.vst3" und "S5 Sampler.component" loeschen.
X

echo "== 4/4 DMG =="
DMG="$OUT/S5Sampler_$VER.dmg"
rm -f "$DMG"
hdiutil create -volname "S5 Sampler" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Fertig: $DMG"
