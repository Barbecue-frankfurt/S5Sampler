# S5 Sampler – VST3 im Stil des Roland S-550

Ein Sampler-Instrument (VST3, macOS universal: Apple Silicon + Intel), inspiriert vom Roland S-550.
Kein offizielles Roland-Produkt, enthält weder Roland-Code noch Roland-Samples.

## Was drin ist

- 8 Stimmen, mono Sample, Zuweisung auf der ganzen Tastatur
- Sample-Emulation: 30 kHz oder 15 kHz, 12 Bit (oder 16 Bit), Anti-Alias-Filter, Normalisierung
- "Smooth"-Schalter: aus = Wiedergabe ohne Interpolation (rauer, authentischer), an = linear interpoliert
- Loop mit Start/Ende (per Regler oder direkt im Wellenform-Fenster ziehen)
- TVF: resonanter Tiefpass mit ADSR-Hüllkurve (±6 Oktaven)
- TVA: ADSR-Lautstärkehüllkurve, Velocity
- LFO (Sinus) auf Tonhöhe und Filter, Modwheel steuert Vibrato
- Pitchbend (±2 Halbtöne), Root-Key, Tune
- Sample wird im Cubase-Projekt mitgespeichert (als 16-Bit-PCM)
- Beim ersten Start ist ein Saw-Testsound geladen ("Init Saw")

Bewusste Vereinfachungen gegenüber dem Original: ADSR statt 8-stufiger Hüllkurven, ein Sample
pro Instanz (keine Multisample-Zonen), keine 8 Einzelausgänge, kein Disk-/Floppy-Format.
Maximale Samplelänge: 25 Sekunden.

## Bauen auf dem Mac

Einmalig vorbereiten:

1. Xcode Command Line Tools: `xcode-select --install`
2. CMake: `brew install cmake` (oder von cmake.org)

Dann im Projektordner:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target S5Sampler_VST3 -j 8
```

Beim ersten Konfigurieren lädt CMake JUCE von GitHub (braucht Internet, dauert ein paar Minuten).
Nach dem Build wird das Plugin automatisch nach `~/Library/Audio/Plug-Ins/VST3/S5 Sampler.vst3` kopiert.

Zum schnellen Testen ohne Cubase gibt es zusätzlich eine Standalone-App:
`cmake --build build --config Release --target S5Sampler_Standalone`

## In Cubase laden

1. Ad-hoc signieren, sonst blockiert macOS das Plugin unter Umständen:
   ```bash
   codesign --force --deep -s - "$HOME/Library/Audio/Plug-Ins/VST3/S5 Sampler.vst3"
   ```
2. Cubase starten (bei Bedarf: Studio > VST-Plug-in-Manager > Aktualisieren).
3. Neue Instrumentenspur > VST Instruments > "S5 Sampler".
4. WAV/AIFF/FLAC ins Plugin-Fenster ziehen oder "Load Sample" klicken.

Falls Cubase das Plugin nicht anzeigt: Das Plugin muss für die Architektur passen, in der Cubase läuft
(Der Build ist universal, deckt also beides ab). Läuft Cubase unter Rosetta, wird die x86_64-Variante genutzt.

## Dateien

- `Source/SampleData.*` – Sample-Emulation (Resampling, Quantisierung), Parameter-Zeiger
- `Source/S5Voice.*` – Stimme: Wiedergabe, Loop, Filter, Hüllkurven, LFO
- `Source/PluginProcessor.*` – Parameter, State-Speicherung, Sample laden
- `Source/PluginEditor.*` – Oberfläche mit Wellenform und Loop-Markern

## Anpassen

- JUCE-Version: `GIT_TAG` in `CMakeLists.txt`
- Name/Hersteller/Codes: `juce_add_plugin(...)` in `CMakeLists.txt`
- Stimmenanzahl: `numVoices` in `PluginProcessor.cpp`

## Pixel-Oberfläche (S-550 Monitor-Stil)

- `Source/Screen/PixelScreen.*`: 640x450-Framebuffer mit eigener Bitmap-Schrift, ohne JUCE-Abhängigkeit.
- `Source/Screen/Pages.*`: Seiten (bisher EDIT-Patch PRM) und gemeinsamer Rahmen/Tastatur.
- `Source/Screen/ScreenComponent.*`: zeigt den Framebuffer ganzzahlig vergrößert (Standard 2x, ohne Glättung).
- `Source/Screen/ScreenEditor.*`: Plugin-Editor mit dem Bildschirm. Mit `-DS5_PIXEL_UI=OFF` kommt wieder die alte Knopf-Oberfläche.
- `Tests/render_test.cpp`: rendert eine Seite als PPM, um sie pixelgenau mit den Layout-Tafeln (Ordner `Layout/`) zu vergleichen.
  Aufruf: `g++ -std=c++17 Tests/render_test.cpp Source/Screen/PixelScreen.cpp Source/Screen/Pages.cpp -o rt && ./rt patch out.ppm`

### Bedienung der Pixel-Oberfläche (Stand jetzt)
- Maus auf **MODE**: Modus-Menü (PLAY, EDIT, UTILITY, FUNC, MIDI, DISK). **MENU**: Seiten des aktuellen Modus.
- **EDIT-Patch PRM** ist lebendig: Zeile anklicken, **Dec/Inc** oder Mausrad ändert den Wert.
- Lebendig (Zeile/Zelle anklicken, Dec/Inc oder Mausrad): **Patch PRM, Tone PRM, Loop, LFO, TVF, TVA, Split, Master, Message, Prog #** (Code in `LivePages.cpp`; Standardzustand ist pixelgleich mit den Tafeln, außer Loop mit berechneten Markern).
- Alle anderen 30 Seiten werden vorerst aus den Layout-Tafeln wiedergegeben (`PageImages.cpp`, pixelgenau) und werden Seite für Seite durch echten Code ersetzt.
- Tests: `Tests/ui_test.cpp` (Navigation und Wertänderung), `Tests/image_dump.cpp` (alle 40 Seiten ausgeben und gegen die Tafeln vergleichen).

## Browser-Vorschau (Web/)

`Web/s5_screen.html` ist eine einzelne Seite mit dem echten Bildschirmcode (Source/Screen), als WebAssembly übersetzt. Sie verhält sich wie der Plugin-Editor und lässt sich ohne Bauen im Browser öffnen.
`Web/build/build.sh` erzeugt `Web/s5.wasm` neu (clang++ und wasm-ld genügen, kein Emscripten). Danach das wasm als base64 in `s5_screen.html` (Variable `WASM`) ersetzen.

### Bedienung (Plugin und Browser)
- Linksklick auf einen Wert senkt ihn, Rechtsklick erhöht ihn. Maustaste halten lässt ihn laufen, immer schneller (nach 0,3 s; Stufen bei 0,3 / 0,7 / 1,3 s).
- Shift gedrückt: Start-, Loop-, End-Point sowie Window/Cursor Adrs ändern sich zehnfach.
- Mausrad und Dec/Inc ändern den markierten Wert. Disk-Popups: Klick markiert, Sel öffnet, exit geht zurück.

### Stand der Seiten
Bedienbar (Pixel-Vergleich mit den Entwürfen im Startzustand identisch, Tests/image_dump.cpp): Patch, Tone PRM, Loop, LFO, TVF, TVA, Split, Master, Message, Prog #, Truncate, Combine, D.Filter, Delete, Copy/Move, Disp.Wave, Standard, Split Disp, 8 Key Disp, Mix, Sampling, Wave Draw, Tone Map, Patch Map, Disk-Menü und -Popups, Load/Save P.PRM, Load Tone.
Noch Bilder: Wave Scope, Monitor, Dir Patch, Dir Tone, Label Set.

### Offen
- Datenmodell (Patches, Tones, 8 Parts, 16 Stimmen), das alle Seiten gemeinsam lesen und schreiben und das die Klangerzeugung steuert.
- Plugin auf dem Mac bauen und prüfen (Linux-Build war hier nicht möglich).
- Tone-Map-Parameter 11 bis 29, Tone-Namen und Patch-Namen I21 bis I28 sind Platzhalter.

## Disk-Images und echte Floppy (Source/Data, Source/Platform)

- `Source/Data/Block.*`: Datenmodell einer S-550-Disk (720-KB-Image, 737280 Bytes), lesen/schreiben, Round-Trip getestet (`Tests/disk_test.cpp`). Wave-Packung und Offsets sind nur per Round-Trip geprüft, nicht an einer echten S-550-Disk.
- Disk-Menü: IMG Load/Save, FD Read, FD Write.
- `Source/Platform/JuceFiles.h`: Image-Ordner `~/Documents/S5Sampler/Disks` (`*.IMG`), schreibt über Temp-Datei.
- `Source/Platform/MacFloppy.*` (macOS): USB-Floppy per `diskutil` erkennen, per Shell-Skript spurweise mit `dd` lesen/schreiben (Admin-Passwortdialog über `osascript`), Verify per Prüfsumme, Abbruch über Datei zwischen zwei Spuren. Nur externe Medien mit exakt 737280 Bytes. Das Skript wurde mit einer Dateiattrappe getestet (Schreiben, Lesen, Verify, Abbruch); die C++-Teile und der echte Zugriff sind noch nie auf einem Mac gebaut oder mit Hardware getestet.
- Kompatible Laufwerke laut llamamusic: IBM FD-05PUB, IBM MPF82E, Dell FD05PUB, Targus PA905.

### Parameterseiten und Disk-Daten
Nach **Load Image** (`App::linked`) bearbeiten Tone Edit, Loop und Truncate den geladenen Block: Tone-Auswahl über das ID-Feld oben links (bzw. „Source“ bei Truncate), Loop Mode, Start/Loop/End Point, Original Tone und Original Key. Änderungen landen beim nächsten Save Image im Image. Ohne geladenes Image zeigen die Seiten die festen Werte der Layout-Boards. Annahme (nicht an einer echten Disk geprüft): Original Key 12 = „C 0“. Alle übrigen Tone-/Patch-Parameter sind noch nicht abgebildet, bleiben aber im Image erhalten.

### Echte Test-Disks (Web/disks, Tools/hfe2img.py)
`RSB_501.IMG`, `CLUB_EP.IMG`, `TENOR_2.IMG` sind echte S-550-Disks (aus .OUT bzw. HFE-Dateien). `Tools/hfe2img.py` wandelt HxC-HFE (ISO-MFM, 80x2x9) in ein Rohimage um. Alle drei lesen sich mit `Block::parse` fehlerfrei; Offsets, Namen und die 12-Bit-Wave-Packung sind damit an echten Daten bestätigt. Die Browser-Vorschau enthält die drei Images im Ordner (Load Image).

Truncate (und alle Seiten mit Wellenform-Feld) zeigen nach Load Image die echte Wellenform des gewählten Tones (Min/Max pro Spalte, Skala +-2048, gleiche Skala wie die Start/End-Marker).

### Patch-Bytes (aus 7 echten Disks abgeleitet)
Patch-Eintrag (256 Bytes): 0-11 Name, 12 Pitch-Bend-Range, 15 Key Mode (0 Normal, 1 V-Sw, 2 X-Fade, 3 V-Mix, 4 Unison), 16 V-Sw-Schwelle, 17-125 Key-Map 1st Tone und 126-234 Key-Map 2nd Tone (Taste 12..120 -> Tone-Nr.), 236 Oct.Shift (vorzeichenbehaftet), 237 Level. Key Mode und Oct.Shift sind durch die Patch-Namen gestützt (z. B. „V-sw“, „uni“, Flöte +1 Oktave, Sax -1); Pitch-Bend-Range und V-Sw-Schwelle sind plausibel, aber nicht bestätigt. Angebunden: Split (Key Mode, Oct.Shift, Patch-Auswahl), Patch Map (Spalten I1/I2: Bend Range, V-Sw Thresh., Oct.Shift, Level), Split Disp (Patch-Namen). Key-Maps und 1st/2nd Tone sind noch nicht angebunden.

Split zeigt nach Load Image die Key-Maps auf der Tastatur (Strich bei jedem Tone-Wechsel, 2nd rot oben, 1st unten; Tasten C2..B6) und als 1st/2nd Tone den Tone auf C4. Anzeige nur, noch nicht editierbar. Annahme: Map-Index 0 = Taste 12 (aus 7 Disks abgeleitet, nicht am Gerät geprüft).

Key-Maps bearbeiten (Split, nach Load Image): auf die Tastatur klicken setzt den Cursor (cyan), das Feld „Key“ verschiebt ihn. 1st/2nd Tone ersetzen den Tone ab dem Cursor bis zum Ende des zusammenhängenden Bereichs. Eine Grenze verschiebt man, indem man den Cursor auf die erste Taste des oberen Bereichs setzt und dort den Tone des unteren Bereichs einstellt.

Patch-Name (Seite Patch, schwarze Box): Klick wählt die Box, Klick auf ein Zeichen setzt den Cursor, Rechtsklick/Mausrad/Inc = nächstes Zeichen, Linksklick/Dec = vorheriges; mit der Computertastatur direkt tippen (Backspace, Entf, Pfeile, Enter/Esc beenden). Mit geladener Disk wird der Name im Patch der Disk geändert.

Texteingabe-Boxen (schwarz): Patch (Name, 12 Zeichen) und Tone Edit (Name, 8 Zeichen) sind editierbar; gemeinsamer Code in LivePage::TextBox / pages::textBox. Weitere Boxen (Label Set) kommen, sobald die Seite live ist.

Label Set ist jetzt eine echte Seite: zwei Blöcke mit je vier editierbaren Textboxen (20 Zeichen). Der Text ist noch nicht auf der Disk gespeichert (Lage der Label-Zeilen im Image ungeklärt).

Pull-down-Menüs im Disk-Bereich (Load, Save, Format, Backup, Save SYS, Change SYS) sind aktiv. Zwei Speicher: Memory = App::block (Edit-Seiten), Disk = App::disk (Load Image / Dir-Seiten). Load: Disk -> Memory; Save und Backup: Memory -> Disk; Format: leert die Disk. Save SYS / Change SYS melden nur, das System-Gebiet bleibt unverändert. Save Image / DD Write schreiben weiterhin den Memory-Stand.

Pull-down-Menüs (MODE, MENU, Disk-Menü, Load/Save/Format/Backup/SYS, DD-Write-Bestätigung): Maus darüber (ohne Klick) markiert den Eintrag, ein Klick aktiviert ihn.
Rechtsklick schließt jedes Pull-down (MODE/MENU-Auswahl ohne Wahl, Disk-Menü und Popup-Seiten wie exit, DD-Write-Abfrage = Abbrechen).
Obere Reiter (MODE, MENU, SUB, COM): Mouse-over färbt den Reiter rot; bei offenem Pull-down übernimmt der andere Reiter (MODE/MENU) beim Darüberfahren.

## Zeichentabelle und Patch-ID
- Ist ein Namensfeld aktiv (Patch, Tone Edit, Label Set), erscheint rechts die 10x10-Zeichentabelle (Spalte 30-39, Zeile 5-14): Mouse-over markiert die Zelle, Klick schreibt das Zeichen an die Cursorposition. Rote Zellen: I = Leerzeichen einfügen, D = Zeichen löschen, Pfeile = Cursor. Die schraffierte Zelle hat keine bestätigte Funktion (nur gezeichnet).
- Die Patch-ID (I11..I28) ist ein wählbares Feld (cyan); erster Klick wählt nur, danach Dec/Inc/Rad/Klick. Mit geladener Disk wechselt sie `App::curPatch`.
- Patch-Seite an die Disk gebunden (bestätigt): Key Mode (15), Unison Detune (239), V-Sw Thresh. (16), P.Bend Range (12), Oct.Shift (236, vorzeichenbehaftet), Level (237), Name. Key Assign, V-Mix Ratio, A.T Assign/Sense, Output Assign bleiben lokal, bis die Bytes bestätigt sind.

## Wave Scope
Oszilloskop für das Audio-Eingangssignal (per Gerätefotos bestätigt). Leerlauf: SUB leer, COM rot, Sel und ----- inaktiv, leerer Rahmen. COM schaltet um: rote Leiste "Wave Scope", Tasten EXE (Start/Stop) und exit (zurück in den Leerlauf, Annahme). Die Kurve zeigt 480 Samples ab einem steigenden Nulldurchgang.
- Host-Schnittstelle: `App::scopeFeed (const float*, n)`; `UiState::scopeOn()` sagt, ob der Host Samples liefern soll. Browser: Mikrofon (falls erlaubt), sonst Testsignal. Im Plugin muss processBlock noch den Eingang an `scopeFeed` übergeben (Mac-Seite, nicht gebaut/getestet).

## MIDI Monitor
Per Gerätefotos bestätigt: Display CH (1-16, All; Standard 1) und Real time MSG (Off/On) oben, rechts "Key. Vel." mit den Werten der letzten Note (Note On/Off), darunter der Hex-Dump: 18 Bytes je Zeile, 10 Zeilen (180 Bytes), Statusbytes (ab 80) rot, Datenbytes weiß; SysEx (F0 ... F7) wird mitgezeigt. Real time MSG blendet Bytes ab F8 ein. Host-Schnittstelle: `App::midiFeed (msg, n)` (eine komplette Nachricht), `UiState::midiOn()`. Browser: Web MIDI (falls verfügbar). Plugin: processBlock-MIDI an `midiFeed` übergeben (Mac, ungetestet).

## LFO / TVF / TVA (Gerätefotos, CLUB_EP I46 "Wave 5")
- Layout nach den Fotos neu: Kopfzeile "I46 Wave 5 Sub 35" (Sub-Text nach dem Namen), schmale Wertefelder (3 Zellen), Envelope-Tabelle mit 8 Spalten (Abstand etwa 62 px, aus den schrägen Fotos geschätzt).
- **SUB**-Pull-down: E.Source / C.Source (Kopfzeile "E.Source I46" / "C.Source I11"; die ID dort ist wählbar, C.Source ist der Kopier-Ton). Tasten dann Sel / exit.
- **COM**-Pull-down: Copy / Init / Swap Page PRM, Tasten EXE / exit. Copy und Swap wirken zwischen C.Source und dem bearbeiteten Ton, Init setzt die Seite auf Standardwerte (Annahme: häufigste Werte der 7 Disks). Nur mit geladener Disk.
- Mit Disk gebundene Bytes im 128-Byte-Tone-Datensatz (durch Abgleich der Anzeigewerte an CLUB_EP I46 "Wave 5" und ORCHESTR I28 "Gong3" bestätigt): LFO Rate 28, Sync 29, Delay 30, Mode 32 (0 Sin, 1 P.H; weitere Modi gibt es in den Daten nicht), Offset 35 (vorzeichenbehaftet, -64..63); TVF Cut-off 38, Resonance 39, Key Follow 40 (vorzeichenbehaftet), EG Depth 43, L.Curve 45, Key-Rate 46, Vel-Rate 47, Sus 107 (+1), End 108 (+1), Level k 109+2k, Rate k 110+2k; TVA Vel-Rate 72, Sus 51 (+1), End 52 (+1), Level k 53+2k, Rate k 54+2k.
- Noch lokal (Byte nicht bestätigt): LFO Polarity, TVF LFO Depth (vermutlich 42) und EG Pol., TVA LFO Depth, L.Curve und Key-Rate, Disp.Zoom. Unerklärt in Gong3: Bytes 33 (127), 36 (1).

## PLAY-Seiten (Voice Mode)
Standard, Split Disp und 8 Key Disp haben dieselbe Part-Tabelle (CH, Patch I11-I28, Out, Level) und denselben **Voice Mode** (Kopfzelle links oben, gemeinsamer Wert in `App::voiceMode`). Laut Handbuch S.28-29: **VAL** = Last Note Priority, **VAF** = First Note Priority (in beiden Modi zeigt die Spalte `*` und die Meldungszeile den Text), **V 1 .. V22** = feste Aufteilung der 16 Stimmen auf die Gruppen A-H (Tabelle `kVoiceSplit`). Patch-Namen kommen von der Disk. Die 8 Key Disp hat acht eigene Tastaturen A-H (schwarz auf weiß), die Split Disp eine Tastatur mit End-Strichen.

## Gespielte Noten auf den PLAY-Seiten
`App::midiFeed` führt pro MIDI-Kanal mit, welche Noten gehalten werden (`App::held`; Note Off, Velocity 0 und All Notes Off beenden sie). Klingt ein Part (Kanal = sein CH-Wert), bekommt sein Buchstabe einen roten Kasten (alle drei Seiten). Split Disp zeigt die Tasten des gewählten Display-Parts (weiße Taste: blauer Kasten unten, schwarze Taste: weißer Kasten), 8 Key Disp rote Striche in der Zeile des Parts. Mit gedrückter linker Maustaste auf einer Tastatur klingt die Note auf dem Kanal des Parts, und zwar genau so lange wie die Taste gedrückt ist; Drüberwischen wechselt die Note (`LivePage::keyAt`, `UiState::dragTo/mouseUp`, `App::noteSet`); im Browser kommen echte Noten über Web MIDI, im Plugin muss `processBlock` die MIDI-Daten an `App::midiFeed` geben.

## Regel: kein blauer Rand unter Tastaturfeldern
Weiße Tastaturfelder (Split Disp, 8 Key Disp, Patch/Map-Seiten) schließen unten bündig mit dem Bildschirm ab (y = 440), darunter bleibt kein blauer Streifen.

## Punkte in den Grafikfeldern ziehen
In den Grafikfeldern (TVF/TVA-Hüllkurve, Loop, Truncate) lassen sich die Punkte mit gedrückter linker Maustaste greifen und verschieben; der Mauszeiger bleibt der normale (`LivePage::graphGrab/graphDrag`, `UiState::gripId`):
- **TVF/TVA:** Der gegriffene Punkt folgt der Maus in jede Richtung. Senkrecht = Level; waagerecht = Rate seines Segments. Das nächste Segment gibt die Zeit zurück (soweit es kann), so dass die übrigen Punkte stehen bleiben; Rest schiebt die späteren Punkte. Während des Haltens ist der Maßstab eingefroren (`LivePage::graphScale`), nach dem Loslassen wird die Kurve wieder auf die Breite eingepasst.
- **Loop:** Marker Start, Loop, End; **Truncate:** Start, End. Die Reihenfolge Start <= Loop <= End bleibt erhalten.
Änderungen laufen wie Dec/Inc über `postChange`, werden also auch in die Disk-Bytes geschrieben.

## Hüllkurven-Anzeige und Mouse-over (Regel)
**Hüllkurve** (TVF/TVA, nach Gerätefoto): Rate 127 = Sprung (senkrechte Linie), kleinere Raten brauchen quadratisch länger (`envTime`); am Sustain-Punkt hält der Pegel eine feste Breite (rote Linie); Punkte sind kleine schwarze Quadrate, Rahmen und Linien schwarz.
**Regel Mouse-over:** Überall, wo ein Wert verändert werden kann, leuchtet er beim Überfahren cyan auf, genau wie der gewählte Wert (Felder aller Live-Seiten `LivePage::hoverField`, Zeilen der Patch-Seite, Punkte in den Grafikfeldern, Marker auf Loop/Truncate). Rot leuchten nur die roten Stellen auf: MODE/MENU/SUB/COM und Pull-down-Einträge.

## Nur eine cyan Box
Cyan leuchtet nur der Wert unter der Maus. Der angeklickte (gewählte) Wert bleibt für Dec/Inc/Mausrad gewählt, wird aber nur gezeigt, solange die Maus darüber ist oder auf Dec/Inc steht (`LivePage::showSel`, `UiState::overStep`); sonst ist keine Box cyan.

### Kein Bestätigungsklick
Das Aufleuchten unter der Maus ist die Bestätigung: Ein Klick auf einen Wert (Live-Seiten und Patch-Liste) ändert ihn sofort (links = Dec, rechts = Inc). Gedrückt halten wiederholt sofort. Textfelder (Namen, ID) wählen weiterhin erst per Klick aus.

### Hüllkurven-Zeitachse und Disp.Zoom (aus Fotos, Zoom 1..4)
Jede Zoom-Stufe verdoppelt die waagerechte Skala (fest, kein Einpassen mehr). Ein Segment ist so breit wie (Level-Änderung) / Geschwindigkeit(Rate); die Geschwindigkeit steigt um etwa 3,6 % je Rate-Schritt, Rate 127 = senkrechte Linie. Das Sustain-Stück ist immer gleich lang (rot). Was rechts über den Rahmen hinausgeht, wird abgeschnitten (Zoom 4 im Foto). Beim Ziehen eines Punktes bekommt das nächste Segment die Rate, die die folgenden Punkte stehen lässt.

### Tone PRM und Loop nach Fotos
- Tone PRM: "Shift" ist eingerückt, Original Tone zeigt `***`, wenn der Tone kein Sub-Tone ist.
- Loop: "Foward" (so schreibt es das Gerät), Search Mode mit ±-Zeichen (Zeichen 0x7f im Font), "Loop Length"/"Wave Length" eine Zeile höher, Loop Length cyan.
- Screen Type 1 = ganze Wellenform, Typ 2 = Ausschnitt um den Loop Point, Typ 3 = Ausschnitt um den Start Point (cyan Kreuz in der Mitte, Skala Zoom Time: je Stufe doppelt, 5 = ca. 246 Wörter breit, Zoom Level = Höhe). Striche in der oberen Leiste = eine Loop-Länge links/rechts der Mitte. Die Wellenform ist noch synthetisch (nicht aus den Sample-Daten).

### Zahlen zittern nicht
Rechts- und mittig ausgerichtete Zahlen enden an der Zellkante der letzten Ziffer (nicht an der Tinte). Vorher verschob eine schmale Ziffer wie die 1 die ganze Zahl um 1-3 Pixel, wenn sich der Wert änderte.

### Loop-Grafik nach 11 weiteren Fotos
- Start, Loop und End Point sind immer **weiße Striche in der oberen Leiste** (alle drei Screen Types), Lage = Anteil an der ganzen Wave (Wave auf x 103..523). Die Striche lassen sich dort mit der Maus greifen und verschieben.
- Typ 1: ganze Wellenform (Höhe = Zoom Level), 4 cyan Linien teilen die Wave in Drittel.
- Typ 2 / 3: ein Punkt je Sample-Wort um Loop Point bzw. Start Point (cyan Kreuz), Fensterbreite = 48 Wörter × Zoom Time, Höhe = Zoom Level, Punkte außerhalb des Bildes entfallen.
- Loop Mode: Foward, Alter, 1 Shot, Reverse (bestätigt).

### Echte Wellenform auf der Loop-Seite
Mit geladener Disk zeichnet die Loop-Grafik die echten Samples des Tones (bei Sub-Tones die des Quell-Tones): Typ 1 als Hüllform (kleinster/größter Wert je Spalte), Typ 2/3 als ein Punkt je Sample-Wort. Höhe: voller Pegel bei Zoom Level 1 = halbe Bildhöhe, Zoom Level multipliziert. Ohne Disk bleibt die Demo-Welle. Der Pegel-Maßstab ist nicht an Fotos eines bekannten Samples geeicht.

Loop-Grafik Typ 2/3: wie am Gerät nur die echten Sample-Punkte (2x2), keine Zwischenpunkte oder Linien.

### Hüllkurven-Geschwindigkeit nach TVF-Fotos
Mit den TVF-Fotos (Disp.Zoom 1-4, Rate 68 und 20) und den TVA-Fotos ist die Zeit je Level-Schritt bei Zoom 3 an vier Raten gemessen: Rate 20 = 2,64 px, 29 = 1,65, 57 = 0,62, 68 = 0,385; dazwischen ändert sie sich mit festem Faktor je Rate-Schritt. TVA und TVF stimmen damit überein (Rate 20 ergibt in beiden Fotos fast dieselbe Breite). Jede Zoom-Stufe verdoppelt weiterhin.

Eichung der Hüllkurven-Zeit (5 Raten, alle 4 Zoom-Stufen in TVA und TVF): px je Level-Schritt bei Zoom 3 = 12,8 (Rate 5), 2,62 (20), 1,70 (29), 0,65 (57), 0,385 (68); je Zoom-Stufe etwa ×2,17 (nicht genau ×2).

Rate 100 gemessen (TVF, Zoom 4): 0,12 px je Level-Schritt bei Zoom 3; oberhalb von Rate 68 ändert sich die Geschwindigkeit etwas langsamer (Faktor 0,964 je Schritt). Damit sind 6 Raten geeicht.

## Audio-Engine (Source/Engine/Engine.*)
JUCE-freie Klangerzeugung: Patch (Key → 1./2. Tone, Key Mode Normal/V-Mix/V-SW/X-Fade) → Wave (12 Bit, 30/15 kHz, Loop Forward/Alter/1 Shot/Reverse), TVF (Tiefpass mit Resonanz, Key Follow, 8-Punkt-Hüllkurve), TVA (8-Punkt-Hüllkurve, Velocity), 16 Stimmen nach Voice Mode, Pitch Bend, Sustain-Pedal. Test: `g++ -std=c++17 -I. Tests/engine_test.cpp Source/Engine/Engine.cpp Source/Data/Block.cpp -o engine_test && ./engine_test disk.img`.
Plugin: Audio-Eingang läuft in den Wave Scope; MIDI und Maus-Tastatur gehen per FIFO zur Engine; State = Disk-Image. Hüllkurvenzeit nutzt die Annahme `kSecondsPerPixel` (EnvModel.h), nicht gemessen.

## HFE / OUT laden
Der Öffnen-Dialog (Load Image) akzeptiert .img/.out/.hfe/.ima. HFE (ISO-MFM, 80x2x9) wird in Source/Data/Hfe.cpp zum Rohimage dekodiert, Dateien über 737280 Byte (z.B. CD5_100.OUT) werden abgeschnitten.

## Start leer, Disk-Menü
Plugin/Standalone starten mit leerem Block ohne Namen/Waves (`S5_EMPTY_START`, Web-Vorschau behält die Demo) auf PLAY-Standard. Disk-Menü nach Vorlage: links HD (Load, Load P, Load T, Save, Setup, Set Label), rechts IMG (Load öffnet den Mac-Dialog, Load P/T, Save, Backup), Change SYS, DD Read/Write. Block II / Chain / Set I/II: später.

## Dateisystem ohne festen Ordner (Mac)
`FileService::dialogs()` = true (JuceFiles): kein Ordner `~/Documents/S5Sampler`, alles läuft über den Finder-Dialog (auch Netzlaufwerke). IMG Load / IMG Load P / IMG Load T öffnen den Öffnen-Dialog sofort (`App::pickImage`), IMG Save / IMG Backup fragen nach dem Speichern den Ort (`saveAs`), DD Read speichert das gelesene Image per Dialog, DD Write wählt das Image per Dialog. Die HD-Einträge arbeiten mit der geladenen Disk im Speicher. Die Web-Vorschau behält die Ordnerliste.

## Festplatten-Images (HD)
Disk-Menü links ("HD Load", "HD Load P", "HD Load T") öffnet den Finder-Dialog für ein ZIP/ZuluSCSI-Image (`* ROLAND S-550 *`).
Das Verzeichnis (64 Slots H11–H88, Sektor 0x400) verweist auf Blöcke à 1746 Sektoren (Patches, Label, Tones, Waves 16 Bit).
Slot wählen (Klick oder Dec/Inc), Sel: HD Load lädt den Block in den Speicher, P/T öffnen die bekannten Load-Seiten. Nur Lesen.
HD Load P / HD Load T (Handbuch HD5-IF S.28-32): HD Area, HD P#/T#, Int P#/T#; COM: HD Load P.PRM, HD Load Patch (Tones in freie Plätze, Nummern und Tastenzuordnung werden angepasst, "Memory full"), *Delete Patch, HD Load Tone (Wave Bank A/B, "Cannot Execute"). Logik in `Source/Data/BlockOps.cpp`. SUB > HD Area öffnet das Slot-Raster.
