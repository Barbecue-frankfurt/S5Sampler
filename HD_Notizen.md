# HD-Menü – Stand 2026-11-05 (Notizen für morgen)

Material in /home/claude/HD_Material: CD5_manual.pdf (gescannt, Seitenzahl = PDF-Seite; HD-relevant S. 18-26, 28-33, 38-41, 45-48),
S550HD5.OUT (HD5-IF System Ver 1.03, 720KB-Image, Seiten-Tabelle ab 0x5480 / 0xfd00, Meldungen 0x9cf0-0x9fd0),
hdchk.cpp (Test-Tool für HdImage). HD-Image des Users: /tmp/hdz/HD1_512.img (aus HD1_512_img.zip; ggf. neu entpacken): 63/64 Slots lesbar, H11 hat keine Patch-Daten.

## Offen (Reihenfolge)
1. Setup-Seite (Disk-Menü "Setup" öffnet bisher fälschlich FUNC "Initialize"). Zeilen: S/Load I<-Area [H11-H88,Off], S/Load II<-Area, S/Load(P.CHG) ch [1-16,Off], S/Controller [Off,Mouse,RC-100], 2 Leerzeilen, *HD Capacity, [ID= ].
   Befehle (HD5-IF 1.03): HD Connection, HD Format, HD Backup SYS (CD-5-Handbuch: "HD Save SYS"), FD Save SYS, HD Park Heads, HD Restart. Format-Auswahl 20M/40M/60M/80M = Area 16/32/48/64.
   SCSI-Check-Anzeige: "*** IF - HD Connection Check ***", Interface/Harddisk (CD-5: "* SCSI Device Connection Check *", I/F check OK, ID=0..6 CD-5/Hard Disk/N.C.). User-Fotos der Setup-Seite fehlen noch.
2. HD Save Seite (bisher falsch = Disketten-Liste): Felder I<-HD Area, II<-HD Area, Select Chain, Wave Verify; Befehle HD Save Chain/Set I/Set II/Block I/Block II/Func I/MIDI I; Meldungen Now Verify / Verify Error. HdImage kann nur lesen -> saveBlock + Image schreiben nötig.
3. HD Load: Load Chain / Load Func / Load MIDI / HD INDEX fehlen ("not available yet").
4. HD Load P/T: Tones in Block der Ziel-Patchnummer (bisher immer Block I); Used Tone Only in HD-Version ignoriert; T.List-Anzeige (Org/Sub/---, ***/---) fehlt; Memory-Full-Liste der nicht geladenen Tones; Load Tone: Cannot Execute bei keinem Platz, bei wenig Platz teilweise laden.
5. Meldungen: HD Formatting, HD Initializing, HD Saving System, HD No System, HD Connection OK/Error, HD Unformatted, HD Util. ver. Error, HD Heads Parking, Working/Complete/Can't Execute/Now Loading/Now Saving, Memory Full.
Frage an User: läuft auf seinem Gerät HD5-IF 1.03 oder CD-5-System? (bei Konflikt zählt sein Gerät/Fotos)
Wave Loop: fertig, im Zip S5Sampler_stand.zip.

## 2026-11-07: Options > "Hard disk images (SCSI) ..." gebaut
- App: hdDev[7] (ID 0-6), cdId, hdActiveId (höchste ID bootet), hdAttach/hdDetach/hdActivate/setCdId, readPath/hdConfigChanged.
- Platform/HdOptions.{h,cpp}: Dialog + Settings-Datei (~/Library/Application Support/S5Sampler.hd.settings). ScreenEditor legt im Standalone einen "Options"-Knopf über den JUCE-Knopf (Menü: Hard disk images / Audio-MIDI-State).
- ID 7 = S-550 (fest, laut CD-5-Handbuch S.19), CD-5 bekommt eine ID 0-6 (User meinte ursprünglich: CD auf ID 7 -> mit User klären).
- VST3: kein Options-Knopf; Zugang später über Setup-Seite (SCSI Check).
- Nicht auf dem Mac getestet (nur Syntax-Check gegen JUCE-Header + ui_test).

## 2026-11-07 (2): Setup-Seite + HD-Load-Fotos eingebaut
- Gerät des Users: "S-550 CD-5 SYS V.1.00 22 Nov/88" -> CD-5-System (Frage geklärt, Namen "HD Save SYS" usw.).
- Neue Seite "DiskSetup" (Disk-Menü "Setup"): S/Load I/II<-Area, S/Load(P.CHG) ch, S/Controller (Off/MU1/RC-100), Hard Disk ID, CD-5 ID (an Options-Geräte gekoppelt), *HD Capacity, Systemzeile. COM: SCSI Check (Layout geschätzt), HD Format/Save SYS/FD Save SYS/Park Heads "not available yet", HD Restart = Image neu lesen.
- HD Load: Werte-Spalte 20.6875, Blockname 24.6875, Pfeil-Glyph, SUB-Menü "HD Area 1/2", COM-Menü "HD Load Chain/SetI/SetII/BlockI/BlockII/FuncI/MIDII" (Chain/Func/MIDI noch "not available yet"), Raster wie am Gerät (H1-H4 / H5-H8, Blocknamen, rote Cursorleiste, Kopf "HD Area 1  H11 > H12").
- Offen: HD Save-Seite, Load Chain/Func/MIDI, Fotos der COM-Menüs von Setup, Meldungen.
- 2026-11-07 (3): COM der Setup-Seite nach Fotos: Working (COM bleibt offen, ---- ----), SCSI-Check-Box mit ID 6-4 links, 3-0 rechts (CD-5/Hard Disk/N.C.), danach Complete.
- 2026-11-07 (4): HD Save-Seite (HdSave, hdAreaPage(save)), HdImage::saveBlock + App::hdSaveSlot + writeRange (schreibt direkt in die Image-Datei), Load Chain, COM-Auswahl startet auf Eintrag 1, Disk-Menü-Marken S S S S H C. Offen: Save/Load Func + MIDI, Chain-Kette über Disks, Verify ist nur Meldung.
- 2026-11-07 (5): HD Load/Save wie IMG: COM bleibt offen, ---- ----, Now Loading/Now Saving/Now Verify (Zähler 64>0), weisse Box, blinkender Pfeil, 5 s simuliert, Arbeit (Laden/Schreiben in Datei) erst am Ende; danach Complete.
- 2026-11-07 (6): gemeinsames Fortschrittsfenster drawXfer fuer IMG/FD/HD Load+Save (Popup x336 Zeile 4, Infobox buendig oben, Pfeil mittig zur Schrift).
- 2026-11-07 (7): Create Hard disk Image (Options-Menue + Dialog; HdImage::makeBlank 20/40/60/80 MB, Areas ab Sektor 5368, ohne Systembereich), leere Areas = valid=false, Save zeigt Label in der Infobox, Verzeichnis-Kurztext (e+0x18) = Blocklabel, Areas-Zaehler = formatierte Areas.

## (8) S-550 SysEx (vorbereitet)
- Source/Midi/SysEx.h/.cpp: Roland F0 41 DEV 1E, RQ1/DT1 + WSD/DAT/ACK/EOD, Adressen (Patch 0x0000+n*512, Func 0x4000, MIDI 0x8000, Tone 0xC000+n*256 x64, Wave A 0x200000 / B 0x280000, Segment 24576 Byte), Nibble-Kodierung, 12-Bit-Wellen.
- Platform/MidiLink: JUCE-Ports + Hintergrund-Thread. Noch KEINE UI, nicht an echter Hardware getestet. Tests/sysex_test.cpp (Fake-S-550).
- Offen: UI-Seite (Ports, Device-ID, Exclusive), Live-Panel-Bedienung, Hardware-Test (Annahmen: Disk-Record = Parameter-Reihenfolge 1:1; Block II = Tone 33-64/Patch 17-32).
(9) UI: Options > "S-550 MIDI link (SysEx) ..." (Platform/MidiLinkUi.*) und Disk-Setup COM "S-550 MIDI Link". Ports, Device-ID, One way/Handshake, Block I/II, Send/Receive, Abort, Live out/in (30 Hz Diff gegen Snapshot, 6 Msg/Tick, >400 Aenderungen = Set geladen, nicht senden). Nicht live: Function/MIDI-Parameter (setFuncParam/setMidiParam vorhanden).
(10) Handshake-Fix: Retry, RQD statt RQ1 im Handshake (Fallback beidseitig), Log (Copy log), Merge-Option.
(11) Log-Auswertung: S-550 sendet DAT je 128 Byte + EOD; naechste Anfrage vor EOD -> RJC. Fix: auf EOD warten, Inaktivitaets-Timeout (Wave 24576 B ~10 s), Handshake-Senden in 128-Byte-DAT.
(12) Function/MIDI-Parameter live (App::mpar/fpar, Seiten Master/Message/ProgNo), MIDI-Thru + Mute + Panic, Options: Display 1x/2x. Offen/zu pruefen: PC-Nummern-Adresse (Handbuch widerspruechlich: 01 46 vs 02 06; gewaehlt 01 46 = Index 99).
(13) Play-Seite: Part-Tabelle (Function-Block idx 0x10 VoiceMode, 0x11 RX-CH, 0x19 Patch, 0x2B Level) live per DT1 + Program Change (Nummer aus mpar 99+Patch). kFuncSize=1024.
(14) Disk-Menue: MIDI Load / MIDI Save (S-550 als Laufwerk, Seiten MidiLoad/MidiSave, App::midiXfer), Load Wave (wav/aiff/mp3 -> Tone, Data/WaveImport, 30/15 kHz, 12 Bit, First-fit im Bank). Wave-Banks: A,B = Block I, C,D = Block II (Adresse 0x200000 + bank*0x80000). MP3 per JUCE_USE_MP3AUDIOFORMAT=1.

(15) REGELN: Jede Load/Save-Seite öffnet das COM-Fenster sofort beim Betreten (UiState::render, Liste ids; DiskSetup/UTIL ausgenommen).
Römisch I/II in allen Befehlstexten explizit (\002 = I, \001 = II). Ladepfeil 4 px höher (raw wird mit setShift mitverschoben).
Load Wave: belegter Tone-Platz zeigt Namen neben der ID, leere nur die ID.
(16) Load Wave im Layout von FD Load T: File (Quelle, Finder öffnet beim Betreten) / Int T# (Ziel: belegt = Name+Info, leer = nur Nummer) / Wave Bank / Rate / Normalize; COM = roter Balken, Klick/EXE importiert; SUB = andere Datei. LivePage::onEnter.
(17) Load Wave: nach der Dateiwahl bleibt COM zu (Ziel wählen, dann COM -> Load Wave). REGEL Overlays: ein Menü/Balken verdeckt darunterliegenden Text komplett, nichts schaut daneben heraus (Quellzeile wird bei offenem Balken nicht gezeichnet).
(18) Load Wave: Dateiname ohne Endung in der Namensspalte (8 Zeichen), Länge rechtsbündig unter/über der Wave-Info; nach Klick 1 s "Now Loading" mit Zähler 64->0, dann Complete.
(19) IMG Load Tone: wie Load Wave - Finder beim Betreten, COM bleibt zu bis Ziel gewählt, Klick -> 1 s Now Loading (FD bleibt unverändert).
(20) FD/IMG Load T: SUB-Menü IMG/Internal (FD: Disk/Internal) -> Sel -> große Tone-Ansicht (Quelle 32 Disk-Tones, Ziel 64 Speicher), Klick wählt, Sel übernimmt. IMG Load P/T: 1 s Now Loading, COM nicht automatisch. Overlap: Quell-/Zielzeile bei offenem SUB-Menü nicht gezeichnet.
(21) Große Tone-Ansicht: SUB öffnet sie sofort mit Pull-down darüber (wie Foto); Name ohne Tone (nur Patch geschrieben) = ROT (N/T = Name/Tone); tightPopup wischt angeschnittene Texte komplett (eraseCut).
(22) Tone-Liste (Handbuch S.37-38): Original-Name weiss, Sub-Tone rot, gelöscht leer; [N/T] mouse-over rot, Klick schaltet auf Wave-Daten (A-0.8, A-0.8x2, Sub 11 rot, Sub -- rot). Gilt in FD/IMG Load T und allen Tone-Map-Seiten (addToneMap).
(23) Loop-/Truncate-Graph: keine erfundene Demo-Welle mehr; ohne echte Wave (leerer Tone / keine Disk) wird keine Welle gezeichnet (Regel: jede gezeigte Welle ist abspielbar).
(24) VST3: schmale Leiste (24 px) unter dem Screen mit Options-Button (gleiches Menü, ohne Audio/MIDI-Eintrag); MIDI-Ports öffnen automatisch beim Start, wenn sie beim letzten Beenden offen waren (Checkbox im Dialog, .midi.settings: auto/wasOpen).
(25) IMG-Seiten: alle IMG-Ladevorgänge/Saves 1 s; FD/IMG Load P: SUB -> große Patch-Ansicht (2 Spalten, 16 pro Block). IMG Load/Save/Backup öffnen COM sofort (kein Ziel zu wählen), IMG Load P/T erst nach Zielwahl.
(26) IMG Save/Backup: beim Betreten zuerst der Finder-Speicherdialog (Ort+Name, FileService::chooseSave/writeChosen), danach COM-Fenster; Datei wird nach dem Job in die gewählte Datei geschrieben; Save Chain fragt für Block II erneut ("Change Next Disk").

## Tag 2
- Wave Loop: MENU öffnet jetzt die UTIL-Liste (Seite fehlte in PageImages → Modus PLAY).
- Trig.Play und Convert aus dem UTIL-Menü entfernt.
- CD-Einträge (CD Load / Load P / Load T / Player): im Handbuch nicht vorhanden (nur "UTL. Backup"); noch nicht umgesetzt.
- CD Load / Load P / Load T / Player aus dem UTIL-Menü gestrichen (CD-5-Hardware; durch HD/IMG abgedeckt). Übrig: SYS.Backup.
- UTIL-SYS.Backup nach Fotos + CD-5/S-550-Handbuch gebaut (Seite SysBackup): S/Load I/II, P.CHG, S/Controller (gleiche Werte wie DiskSetup), Warnung, COM "SYS.Backup": Now Loading, Change disk, Formatting, Now Saving, Complete. Nur Ablauf simuliert, schreibt nichts und löscht den Speicher nicht.
- SYS.Backup erweitert: COM = SYS.Backup (FD lesen, Change disk/EXE, auf FD schreiben), SYS Load FD, SYS Load IMG (Finder, .OUT/.IMG 720K), SYS Save FD, SYS Save IMG (Finder), HD Save SYS (noch nicht: HD-Systembereich-Format unbekannt, CD-5-System auf deiner HD liegt anders als die HD5-IF-.OUT). System wird roh (737280 Byte) gehalten, Systemzeile erscheint in Zeile 10.
- HD Save SYS (Setup + SYS.Backup) gebaut: Finder fragt nach einem HD-Image, das schon das System enthält; kopiert Systemverzeichnis (Sektor 1) + Sektoren 256-5367 in das aktive HD-Image (App::hdSaveSys). Grund: HD-Systembereich ist kein Disketten-Abbild (4x1280-Sektor-Blöcke, ab ~Sektor 943 nicht aus FD ableitbar). Unterlagen: CD5.OUT des Users war durch Textmodus-Übertragung um 1059 0x0A-Bytes zu groß (repariert in /tmp/sysd, nicht im Projekt).
- HD Load SYS / HD Save SYS neu: SYS.Backup COM hat "HD Load SYS" (Liste der verlinkten HD-Images, liest Systemverzeichnis + Sektoren 256-5367 in den Speicher App::hdSysMem, Systemzeile erscheint) und "HD Save SYS" (schreibt den Speicher ins aktive HD-Image). Setup-"HD Save SYS" nutzt denselben Speicher. FD/IMG-System und HD-System sind getrennte Speicher.
- SYS.Backup: Einträge SYS Load/Save HD; SYS Save HD fragt das Ziel-HD-Image ab (App::hdSaveSys(id)); Listen ohne weisse Linie unter dem Balken, Breite angepasst (x325, max 18 Zellen); Warntext zentriert.
- SYS.Backup-Einzelbefehl (FD->FD) entfernt; COM: SYS Load FD/IMG/HD, SYS Save FD/IMG/HD.
- Wave Draw gebaut (Handbuch S.59-62): Belt (ganze Laenge, weisses Quadrat = Window Adrs, Klick/Ziehen setzt), 228 Adressen x 2 px, linke Maustaste zeichnet (rot, Linie zwischen Mauspositionen aufgefuellt), Randzonen scrollen 114, rechte Taste oberhalb/unterhalb der Mitte rollt (Zoom 2-7), Cursor Adrs/Data folgen der Maus und sind editierbar, COM: COPY / CLR WINDOW / CLR ALL. App::waveRev -> Engine laedt Wave neu. Nicht umgesetzt: Window Loop (Feld ohne Wirkung), Fotos vom Geraet fehlen (Geometrie nach Handbuch).
- Wave Draw nach Geraetefoto: Tone-Name + Bank-Zeit (B-0.8), Wave Length + Wert, Tasten Draw / Roll D|U (Maus ueber Wave), Cursor Adrs/Data cyan (Maus in Wave), Data-Label rechtsbuendig, Zeilen 11/12, cyan Linien bei Window, +114, +228, Gürtel-Marken Start/Loop/End (vermutet) + Window-Quadrat.

## Wave Draw – Window Loop + Zeitleiste
- Window Loop On: Start=0, Loop-Punkt=Window Adrs, End=Window Adrs+228, Vorwärts-Loop (Handbuch S.62). Off, Source-Wechsel oder Seite verlassen stellt die Original-Punkte wieder her.
- Cursor Adrs / Data (cyan) folgen der Maus auch auf der Zeitleiste (Adresse über die ganze Wave).
- Unbestätigt: Bedeutung der Zeitleisten-Marken (Start/Loop/End), Zoom-Faktoren.
- Fotos Wave Draw (Tag 2, Nachtrag): Zeile unter den Buttons zeigt nur den roten Titel, kein "Loaded..." (jetzt beim Betreten geleert). Cyan-Linien (Window, +114, +228) sitzen symmetrisch im Rahmen (~4 % Rand links/rechts). Zeitleiste geht über die ganze Rahmenbreite; Cursor Adrs auf der Wave = Window Adrs + Position zwischen den Linien. Links/rechts außerhalb der Linien (Scroll-Zone) zeigt das Gerät Window Adrs bzw. +229. Cursor-Felder bleiben bei offenem COM stehen, leer wenn die Maus nicht auf Wave/Zeitleiste ist (SUB offen).
- Wave Draw Cursor-Abgleich mit Fotos: Cursor Adrs = Window Adrs + (x - Linie0)/Pixel pro Adresse (Foto: 10496+21 ≈ 10515, 20992+90 ≈ 21083 ✓). Scroll-Zonen links/rechts der cyanen Linien (Buttons "Adrs"/"exit"): links = Window Adrs (10496), rechts = Window Adrs + 229 (10725). Data = Wavewert an dieser Adresse.
- Fix: SUB blieb nach dem Source-Wahlfenster rot/aktiv (popupButtons/subRed wurden nicht zurückgesetzt) → Malen war gesperrt. Betrifft alle Seiten mit Tone-Map-Auswahl; jetzt beim Schließen zurückgesetzt.
- Wave Draw gegen Owner's Manual S.59-62 geprüft: Source nur Original-Tones, Belt (weißes Quadrat = Window Adrs), Klick auf Belt/Randpfeile ±114, INC/DEC 114er-Schritte, Zoom 1-7 (Roll mit rechter Taste oben/unten, bei Zoom 1 nicht), gemalte Wave rot, Window Loop (Start 0, Loop = Window, End = Window+228; Off/Seitenwechsel stellt zurück), COM: Copy / CLR Window / CLR All. Marken auf der Zeitleiste: Start/End hoch, Loop kurz (nach Foto).
- HD Load P/T: SUB > "HD #" / "Internal" öffnet jetzt die große Ansicht (wie bei FD/IMG Load und den UTIL-Seiten): Quelle = Block der gewählten HD Area, Ziel = Speicher; Klick wählt, Sel übernimmt und fragt die nächste Zeile, exit schließt. [N/T]-Umschaltung bei Tones.
- HD Load / HD Save (Areas): COM öffnet sich beim Betreten nicht mehr (erst SUB > HD Area 1/2). In der großen Raster-Ansicht folgt der rote Cursor nicht mehr der Maus; nur ein Klick setzt den Platz (rot bleibt stehen, Anzeige oben + Wert), Sel übernimmt.

## Stand Ende Tag 2 / Morgen weiter
- Offen: HD Load/Save (Areas) prüfen, HD Load P/T Rest, HD Park Heads/Format, echtes Wave Verify, PC-Nummer-Adresse am Gerät prüfen, README, Zoom-Faktoren und Zeitleisten-Marken (Wave Draw) mit Gerät abgleichen.

## Tag 3
- HD Load/Save Chain: die Anzeige (Label, H-Nummer, cyan Pfeil + I/II) wechselt auf halber Strecke von Area I auf Area II; mit Wave Verify: Saving I, Saving II, Verify I, Verify II.
- HD Load P/T: die große Ansicht öffnet erst, wenn der Eintrag im SUB-Menü geklickt wird (kein Vorschau-Aufklappen beim Überfahren).
- HD Park Heads entfernt (DISK-Setup COM: SCSI Check, HD Format, HD Save SYS, FD Save SYS, HD Restart, S-550 MIDI Link).
- Echtes Wave Verify (HD Save, Wave Verify On): nach dem Schreiben wird die Image-Datei neu gelesen und die Wave-Daten aller Original-Tones des Blocks verglichen; Abweichung → "Verify Error".
- HD Load P/T: COM öffnet beim Betreten nicht mehr von selbst. Große Ansicht für Patches (Foto): eine Spalte je Block (I11-I28 links, II11-II28 rechts), Id gelb links vom Namen, roter Balken nur auf dem Namen; Kopfzeile "Internal / HD #" + Auswahl. FD Load P hat noch das alte Zwei-Spalten-Layout (ungeprüft).
- DISK-Setup COM: "HD Format" entfernt (könnte eine Partition löschen); neue Partitionen/Images kommen aus dem Options-Menü. COM jetzt: SCSI Check, HD Save SYS, FD Save SYS, HD Restart, S-550 MIDI Link.
- DISK-Setup COM: auch "HD Restart" entfernt (SCSI Check, HD Save SYS, FD Save SYS, S-550 MIDI Link).
- MIDI-Message / MIDI-Prog # nach Geräte-Fotos: Spalten rechtsbündig mit 62 px Abstand, Spaltenköpfe "A-*" gelb, Zeilen RX-CH/P.Chg/Bend/B.Rng/Mod/Hold/A.T/Vol, SUB-Taste leer, rechts neben Exclusive/Device ID "HD Load Set (P.Chg)" + "CH Off/n" (= S/Load(P.CHG) aus DISK-Setup); Prog #: Zeilen I1/I2/II1/II2 im Abstand von 3 Zeilen, Kopfzahlen gelb. Defaults (A.T/Vol Off, RX 3,10,1,2,5..8) stammen aus dem Handbuchbild, das Foto zeigt andere Gerätewerte.
- Offen (MIDI): Func/MIDI-Daten auf Disk/IMG/HD laden+speichern (Ablage der MIDI-Daten im Disk-Image noch nicht bekannt; Function-Daten vermutlich ab 0x10c00).
- Disk-seitiges Load/Save MIDI gestrichen: Einträge "MIDI" entfernt in DISK-Load/Save, FD/IMG Load + Save, HD Load + Save (Func bleibt, noch ohne Funktion). Die SysEx-Seiten DISK-MIDI Load/Save (S-550 als Laufwerk) bleiben unverändert.
- REGEL (neu): SUB / COM ohne Funktion bleiben weiß und leer (LivePage::frameNames: kein subHook/comHook → " "); die Maus darüber zeigt eine rote Linie, und die rechten Tasten zeigen "Sel" / "----" (Fotos MIDI-Monitor). Ausnahmen mit Beschriftung ohne Funktion: showSub/showCom (MIDI-Message und Prog # haben ein beschriftetes COM, Funktion unbekannt). Gilt für alle LivePages; Patch/Play-Seiten (nicht LivePage) unverändert.

## Patch-Seite: SUB / COM (Tag 3, nach Fotos)
- SUB: Pulldown "E.Source" / "C.Source" (x 240, Zeile 4). Sel übernimmt, Kopfzeile zeigt "E.Source"/"C.Source" + Patch-ID (gelb, anklickbar, Dec/Inc ändert: E = bearbeiteter Patch, C = Quellpatch).
- COM: Copy All / Copy Page / Init All / Init Page / Swap All / Swap Page PRM (x 336, Zeile 4), Buttons EXE/exit. Rechtsklick schließt.
- Copy/Swap brauchen C.Source (sonst nichts). All = alle 256 Bytes, Page = Name + bekannte Bytes (Key Mode, Unison Detune, V-Sw Thresh., P.Bend Range, Oct.Shift, Level).
- Init (Handbuch S.114): Key Mode Normal, Key Assign Rotary, Unison Detune 0, V-Sw Thresh. 64, V-Mix 0, P.Bend 2, A.T Assign P.Mod, A.T Sense 0, Oct.Shift 0, Output Out1, Level 127, Name leer. Init All = Init Page (restliche Bytes unbekannt, bleiben).
- UNSICHER: ob "Page" den Namen einschließt; Popup-x-Position; Key Assign/V-Mix/A.T/Output sind noch nicht im Block gespeichert (nur Anzeige). Split und Patch Map: SUB/COM noch offen (Handbuch S.105-108: Split nur Copy/Init/Swap Page).

## Split-Seite neu nach Foto (Tag 3)
- Info-Modus: Zeilen Type Select, 1st Tone, 2nd Tone, Key Mode (Reihenfolge wie Foto), Zeile 8 Köpfe I1-I4 (bei Block II II1-II4), Zeilen 9-16 Tonliste (4 Spalten x 8, Nummern 1-8 gelb). Weiß = vom Patch benutzt (1st-Map, 2nd-Map nur bei V-Sw/X-Fade/V-Mix), Schwarz = vorhanden aber unbenutzt, leer = leere Tones.
- Set-Modus unverändert (Type Select, Key Mode, Set Sw, 1st, 2nd, Oct.Shift + Information-Zeilen), keine Liste (kein Platz).
- UNSICHER: Foto zeigt bei Type Select rechts "1st" statt "Info" - wir zeigen weiter Info/Set; Oct.Shift/Set Sw nur im Set-Modus erreichbar; Spaltenpositionen aus Foto geschätzt; SUB/COM auf Split noch ohne Funktion.

## Split-Seite, 2. Fassung (Tag 3, nach 5 Fotos)
- Kein Info/Set mehr: Type Select = 1st&2nd / 1st / 2nd / Off (Off = nur anzeigen, sonst schreibt ein Tastendruck den gewählten Tone in die Map). Start: Off (Sicherheit, kein versehentliches Überschreiben).
- Zeilen: 4 Type Select, 5 1st Tone, 6 2nd Tone, 7 Key Mode, 8 Oct.Shift (auf Wunsch behalten), Liste: Kopf Zeile 9, Tones Zeilen 10-17. Information-Zeilen entfallen.
- SUB/COM wie Patch-Seite: E.Source/C.Source, Copy/Init/Swap Page PRM (Page = Key Mode, 1st-/2nd-Map, Oct.Shift; Init = Normal, Tone I11, Oct 0 nach Handbuch S.114). Header "E.Source I11".
- UNSICHER: rosa Rauten neben D50PB/BELL1 im Foto (nur bei offenem SUB) nicht umgesetzt; Popup-x geschätzt; ob Oct.Shift zur Seite zählt.

## Split: Type Select mit Info (Tag 3)
- Type Select = Info / 1st&2nd / 1st / 2nd / Off, Start: Info. Info und Off schreiben nichts; nur in Info erscheinen violette Pfeile (neue Palettenfarbe Violet 0xe64be6, nach Foto geschätzt) vor jedem Tone, der für eine gerade gehaltene Taste klingt (2nd-Map nur bei V-Sw/X-Fade/V-Mix).
- UNSICHER: Pfeilform (vorhandener Pfeil-Glyph), Farbwert, ob Pfeile auch bei den anderen Werten erscheinen.

- Header 'E.Source I11' (Zeile 2, Tone-Seiten + Split) ohne Seiten-Shift gezeichnet: liegt nun über dem blauen Feld, nicht mehr darauf.
- Kopfzeile E.Source/C.Source linksbündig zur MODE-Box (Spalte 3.5) auf Tone-, Split- und Patch-Seiten.

- README: bewusst erst GANZ AM ENDE neu schreiben (Entscheidung des Users), bis dahin nicht anfassen.

## Einzelausgänge / Output Assign (Tag 3)
- Bytes (aus MIDI-Tabelle + Disks abgeleitet): Tone Output Assign = Tone-Byte 8 (Tone.f1, 0-7 = Out 1-8; CLUB_EP: 6 Bass-Tones = 1). Patch Output Assign = Patch-Byte 243 (0-7 = Out 1-8, 8 = Tone; CLUB_EP "E.Piano/Bs", "8161/Bass" = 8).
- UI: Patch-Seite (Out1..Out8, Tone), Patch Map (1-8, Tone), Tone Edit (1-8) an die Bytes gebunden; Patch-COM Init/Copy/Swap Page nehmen Byte 243 mit (Init = Out1). Tone Map-Seite bleibt lokal (Platzhalter).
- Engine: Voice.out; render(l, r, n, outL[8], outR[8]); Ausgang aktiv -> Stimme nur dort; nicht aktiv (null) -> läuft im Haupt-Mix. Test in Tests/engine_test.cpp.
- Plugin: 8 zusätzliche Stereo-Ausgangsbusse "Out 1".."Out 8", standardmäßig aus (Host schaltet sie ein). NICHT gebaut/getestet (JUCE nur auf dem Mac).
- Offen: bei "Tone" sinkt laut Handbuch die max. Stimmenzahl (nicht nachgebildet); Floppy-Klick läuft nur im Mix; Mono-Variante der Ausgänge nicht vorgesehen.

## Knackser/Aussetzer im Cubase-Test (48 kHz) - Fixes ohne Messung am Mac
- Engine::setVoiceMode baute die Gruppen alle 25 ms neu (groupOf kurz auf -1 -> in dem Moment konnte eine Note keine Stimme bekommen). Jetzt nur bei Änderung, über Temp-Array.
- Snapshot (Tones/Patches) wurde 40x/s neu gebaut; jetzt nur bei Hash-Änderung (paramHash); alte Snapshots werden auf dem Message-Thread freigegeben (retired[]), nicht im Audio-Thread.
- Nicht behoben/vermutet: Voice-Stealing ohne Ausblendung, harter Notenstart, Imaging durch lineare Interpolation 30 kHz -> 48 kHz; Default-Voice-Mode V11 = Part A nur 8 Stimmen.
- Version: Source/Version.h (S5_BUILD) im Options-Menü (erste Zeile) und links im Streifen unter dem Bildschirm; vor jedem Zip aktualisieren. s5update.sh kopiert das Plugin jetzt immer frisch.
- Diagnose gegen Stottern: Streifen zeigt CPU-Last (Mittel/Spitze), Blöcke >50 %, geklaute und verworfene Noten (Options > Zähler zurücksetzen).

## Stand Ende Tag 3 (09.11.2026)
- Kratz-/Stotteraussetzer in Cubase kamen von Cubase selbst (Peak im Projekt), nicht vom Plugin. Messzeile im Streifen (CPU, geklaut, verworfen) bleibt vorerst; kann später entfernt werden.
- User testet weiter in Cubase (Einzelausgänge, Disk laden, Projekt speichern/öffnen) und meldet sich am 10.11.
- Offen: PC-Nummern-Adresse (später), Wave Draw Zoom/Gürtel am Gerät, README ganz am Ende, Voice-Steal-Ausblendung, Split/Patch weitere Fotos nach Bedarf, Tone Map Output Assign lokal.
- Workflow: Zip ~/Downloads -> ~/Projekte/s5update.sh (baut, signiert, kopiert), Cubase neu starten; Version im Streifen unter dem Bildschirm / Options-Menü prüfen.

## Tag 4 (10.11.2026)
- Load Wave: zu lange Samples werden auf die Bankgroesse gekuerzt (Meldung "Cut to bank size"), kein Fehler mehr.
- Disk Label: Seite neu nach Foto: Titel "DISK-Set Label", 2 Bloecke je 5 Zeilen x 12 Zeichen (=60 Buchstaben laut Handbuch S.132), Zeichentabelle rechts, Tasten rechts "Char."/"----" beim Editieren. Markieren wie Textverarbeitung: Maus ziehen, Shift+Klick, Shift+Pfeile, Pos1/Ende, Strg/Cmd+A/C/X/V (System-Zwischenablage), Entf/Backspace loescht Auswahl (Rest der Zeile rueckt nach links), Enter = naechste Zeile, Esc = Ende. Zeichentabelle ersetzt Auswahl. Backup-Fenster zeigt 5 Zeilen. Unsicher: Funktion der Tasten "Char."/"----" am Geraet (hier: wie Dec/Inc).
- HD Save: Set speichert inkl. Play/FUNC/MIDI-Parametern (Sektor 8), Block nur Sounddaten (Parameter der Area bleiben, nur Label wird geschrieben). Befehlsliste um "MIDI I" ergaenzt (7 Befehle wie im Handbuch S.33). Func/MIDI speichern/laden weiter "not available" (Ablage im Sektor unbekannt).
- 8 Key Disp: Teile mit CH OFF lasen Tastendaten mit Index -1 -> Zufallsmarkierungen; behoben.
- HD Save (Fotos 10.11.): Fenster "Now Saving"/"Now Verify" zeigt das Disk-Label als 5 Zeilen (z.B. "BEST OF JX10" ... "J.HOFMANN"); jetzt aus Set Label (Block I = Zeilen 1-5, II = 6-10), sonst Block-Label in Zeile 1. Befehlsliste mit 7 Eintraegen stimmt mit den Fotos.
- Set Label: Zeichentabelle eine Spalte nach links (x 448..608, Foto 447..609), klebt nicht mehr am rechten Rand.
- Set Label startet leer (kein Roland-Standardtext mehr).
- Sampling-Eingang: eigener Bus "Sidechain" (Stereo, aus bis der Host ihn einschaltet) zusaetzlich zum "Audio In"; Aufnahme/Scope nehmen den Sidechain (Mono-Mix L+R), sonst Audio In. In Cubase: Plugin-Fenster -> Sidechain aktivieren (Taste im Plugin-Kopf) und im Sidechain-Eingang eine Spur / einen Kanal waehlen (Spur: Sends "Sidechain"-Ziel, oder bei der Spur Send -> S5 Sampler). Ungetestet (kein JUCE hier).
- Wave Scope nach Foto: roter Balken/EXE startet den Eingang (Statuszeile 'Start', Tasten 'Stop' / '----'), Kurve und Mittelmarke in Cyan. Stop beendet, exit verlaesst.
- Leiste unter dem Bildschirm (nur im Plugin): REC LEVEL (Sidechain-/Eingangs-Pegel fuer Aufnahme und Scope, 0-127, 100 = 1.0) und VOLUME (Master auf Haupt- und Einzelausgaenge, mit Rampe), CPU nur "CPU x%"; Details + Version im Options-Menue. Ausgerichtet an linker/rechter Kante des blauen Bildschirms (x 16 / 624). Doppelklick = 100. Beide Werte werden im Projekt gespeichert. Ungetestet (kein JUCE hier). Standalone hat keine Leiste, also keine Regler.
- Naechster Schritt: .dmg mit VST3 + AU (AU-Format in CMake ergaenzen, Skript make_dmg.sh auf dem Mac).
- DMG-Installer: CMake baut auf macOS zusaetzlich AU (MusicDevice); Tools/make_dmg.sh baut Universal (arm64+x86_64), signiert ad hoc, erzeugt .pkg (VST3 -> /Library/Audio/Plug-Ins/VST3, AU -> .../Components, einzeln waehlbar) und packt ein .dmg auf den Schreibtisch. Nicht notarisiert (Rechtsklick > Oeffnen). Ungetestet (kein macOS hier).
- make_dmg.sh: ohne -G Xcode (Compiler wurde sonst nicht gefunden), Standard-Generator wie beim normalen Build.
