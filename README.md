# Sensei Smooth – dynamischer Resonanz-Unterdrücker (VST3 / Standalone)

Eigenständige JUCE-8-Implementierung. Das Signal wird per STFT analysiert
(sqrt-Hann, 75 % Overlap) und pro Frequenz-Bin mit einer weich geregelten
Verstärkung bearbeitet.

## Erkennung (Detect)

- **Input** (Standard): Sensei Smooth sitzt als Insert auf der Spur und hört auf sein eigenes
  Signal. Bins, die deutlich über eine geglättete Referenzkurve ihres eigenen
  Spektrums hinausragen, werden abgesenkt. Kein Routing nötig.
- **Sidechain** („Platz machen“): Sensei Smooth sitzt auf der Spur, die Platz machen soll
  (z. B. Gitarre). Die Spur, die Platz braucht (z. B. Gesang), kommt über den
  Sidechain-Eingang. Abgesenkt wird dort, wo die Sidechain lauter als die Threshold-Linie
  ist *und* das Hauptsignal mit ihr konkurriert. Die Absenkung geht nur so weit, bis die
  Sidechain im jeweiligen Band 6 dB vorne liegt. Ohne Sidechain-Signal passiert nichts
  (die Anzeige meldet dann „No sidechain signal“).

  In FL Studio: Gesangs-Spur im Mixer auswählen, Rechtsklick auf den Routing-Pfeil der
  Gitarren-Spur → „Sidechain to this track“. Dann im Wrapper von Sensei Smooth (Zahnrad) den
  Sidechain-Eingang dieser Spur zuweisen und Detect auf „Sidechain“ stellen.

## Parameter

**Dynamics**

| Parameter | Bereich | Wirkung |
|-----------|---------|---------|
| Amount    | 0–24 dB | maximale Absenkung pro Frequenz |
| Precision | 0–100 % | Breite der Absenkung: 1/3 Oktave (weich) bis 1/24 Oktave (chirurgisch) |
| Threshold | 0–100 % | Input: wie weit ein Peak herausragen muss (1,5–12 dB). Sidechain: Pegelschwelle der Sidechain (−90 bis −30 dB, gestrichelte Linie) |
| Attack    | 0,5–100 ms | Ansprechzeit |
| Release   | 5–1000 ms  | Rücklaufzeit |
| Mix       | 0–100 % | Dry/Wet, phasengleich im Spektralbereich gemischt |
| Output    | ±12 dB  | Ausgangspegel |

**Detection** – direkt in der Anzeige über Punkte auf der lila Empfindlichkeitskurve
(ziehen; Doppelklick setzt zurück; beim Überfahren erscheint der Wert)

| Punkt | Parameter | Bereich | Wirkung |
|-------|-----------|---------|---------|
| rote Balken | Low / High | 20 Hz–20 kHz | Bearbeitungsbereich (mit halb-oktavigen Übergängen) |
| grüner Punkt (12 kHz) | Tilt | ±3 dB/Okt. | Empfindlichkeit kippen um 1 kHz (+ = Höhen werden stärker bearbeitet) |
| lila Punkt | Emphasis Freq / Emphasis | 100 Hz–12 kHz / ±12 dB | Empfindlichkeit um eine Frequenz anheben/absenken (Glocke, ca. 1,2 Okt. breit) |

Kurve oben = empfindlicher (mehr Absenkung), unten = weniger empfindlich.

**Bedienung:** Die Regler rechts sind nach Wichtigkeit gestaffelt: Amount (groß) zuerst,
dann Precision/Threshold, dann Attack/Release. Mix, Out und Delta sitzen in der unteren
Leiste. Alle Regler haben Tooltips.

**Header**

| Element  | Optionen | Wirkung |
|----------|----------|---------|
| Detect   | Input, Sidechain | siehe oben |
| Channels | Stereo Link, L / R, Mid / Side, Mid only, Side only | gemeinsame Erkennung, getrennte Kanäle, M/S getrennt, nur Mitte oder nur Seiten bearbeiten |
| Delta    | an/aus | nur das hören, was entfernt wird |

Latenz: 2048 Samples bei 44,1/48 kHz (≈ 43 ms), wird dem Host gemeldet.
Bei 96/192 kHz wächst die FFT mit, die Latenz bleibt etwa gleich lang.

## Build (Windows, Visual Studio 2022)

JUCE 8.0.6 liegt in `./JUCE`. Fehlt das Verzeichnis, holt CMake es automatisch.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release
```

Ergebnisse:

- `build/SenseiSmooth_artefacts/Release/VST3/Sensei Smooth.vst3` – nach `C:\Program Files\Common Files\VST3` kopieren
- `build/SenseiSmooth_artefacts/Release/Standalone/Sensei Smooth.exe`

## Tests

```bash
build/SenseiSmoothTests_artefacts/Release/SenseiSmoothTests.exe
```

Prüft Transparenz (Amount 0, Mix 0, M/S-Rundweg), Resonanz-Absenkung, unverändertes Rauschen,
Delta, Frequenzbereich, Threshold, Erkennungs-EQ, Sidechain (Absenkung, Stille, nicht verbunden,
unter Threshold) und die Kanalmodi.

`SenseiSmoothSnapshot.exe <datei.png> [sc|drag|delta]` rendert die Oberfläche mit Testsignal offscreen als PNG
(`delta` = Delta-Ansicht, `sc` = Sidechain-Modus mit einem gesangsähnlichen Sidechain-Signal, `drag` = zieht den Emphasis-Punkt per simuliertem Mausevent und prüft das Ergebnis).

## Aufbau

- `Source/ResonanceSuppressor.*` – DSP-Kern (nur `juce_dsp`, ohne Plugin-Abhängigkeiten)
- `Source/PluginProcessor.*` – Parameter (APVTS), State, Busse (Main + optionaler Sidechain)
- `Source/PluginEditor.*` – GUI mit Spektrum-, Sidechain-, Empfindlichkeits- und Reduktionsanzeige
- `Tests/` – Offline-Tests und Snapshot-Tool

Hinweis: Die Parameter-IDs `depth`, `sharpness` und `selectivity` sind intern geblieben,
angezeigt werden sie als Amount, Precision und Threshold.
