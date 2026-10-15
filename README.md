# HyperHats by Phragma

Instrument VST3 de charleston en synthèse pure, pensé pour la psytrance et la hi-tech.

- **TYPE** : CLOSED ou OPEN. N'importe quelle note MIDI joue ce son ; la hauteur de la note est ignorée, seule la vélocité compte (accents). Une frappe coupe la précédente. Pour un closed et un open, deux instances.
- **ALLOY** : SQUARE (six carrés façon 808), FM (paires FM, plus brillant), RING (modulation en anneau, plus sale).
- **SOURCE** : du métal accordé au bruit.
- **CHARACTER** : du synthétique (6 partiels nets) à l'acoustique (jusqu'à 48 partiels irréguliers, « chick » du closed, « wash » de l'open).
- **TONE** : TUNE (±12 demi-tons), COLOR (centre du filtre, 2 à 16 kHz), SNAP (claquement de l'attaque).
- **DECAY** : CLOSED (10 à 300 ms), OPEN (80 ms à 2,5 s), SHAPE (queue naturelle → gatée).
- **WIDTH**, **VARIATION** (différences d'une frappe à l'autre ; à 0, toutes les frappes sont identiques), **CRUSH** (échantillonnage et résolution), **LEVEL**.
- Points de départ : 808, TIGHT, SPARKLE, REAL.
- **Presets** (en-tête) : ‹ › pour passer d'un preset à l'autre (usine puis les tiens), clic sur le nom pour la liste, **SAVE** pour enregistrer. Une étoile `*` signale un preset modifié. Un fichier `.hhpreset` par preset :
  - macOS : `~/Library/Audio/Presets/Phragma/HyperHats/`
  - Windows : `%APPDATA%\Phragma\HyperHats\Presets\`

JUCE 8, C++17, CMake. Les polices (Michroma, IBM Plex Mono, licence OFL : voir `Assets/`) sont téléchargées à la configuration depuis `google/fonts` à un commit fixe et vérifiées par SHA-256.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target HyperHats_VST3
```

Tests : `-DHYPERHATS_BUILD_TESTS=ON`, puis `ctest --test-dir build -C Release`.
