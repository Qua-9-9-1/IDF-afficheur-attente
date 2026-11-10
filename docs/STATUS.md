# État d'avancement

_Dernière mise à jour : 2026-11-10_

## Fait
- Initialisation du dépôt : consignes IA (`CLAUDE.md`), README, `.gitignore`.
- Sous-module Bruce + scripts de toolchain (`scripts/setup.ps1`, `scripts/build.ps1`).
- Squelette de l'app `app/idfm` branché dans le menu « Others » de Bruce via `patches/`.

## Décisions
| Sujet | Décision |
|---|---|
| Intégration | App C++ native dans le firmware Bruce (pas de script JS) |
| Branches | `main` + `feature/*` |
| Toolchain | PlatformIO local installé par `scripts/setup.ps1` (rien de lourd dans le dépôt) |
| Clé API | Lue sur la SD (`/idfm/config.json`), jamais commitée |
| Licence | AGPL-3.0 (imposée par Bruce si on distribue le firmware) — à confirmer |

## À faire (prochaines étapes)
1. **Utilisateur** : créer un compte PRIM et générer une clé API.
2. Récolter des références visuelles des panneaux réels (photos) → `docs/reference/`.
3. Client PRIM : requête `stop-monitoring`, parsing JSON (ArduinoJson filtré).
4. Référentiel lignes/arrêts : choisir comment lister lignes et arrêts sur l'appareil
   (API PRIM « référentiel » en ligne vs. fichier pré-généré sur la SD).
5. Écrans de sélection (mode → ligne → arrêt) avec l'encodeur.
6. Rendu des panneaux, mode par mode (métro en premier).

## Pièges de la toolchain (déjà gérés par `scripts/build.ps1`)
- **Chemins > 260 caractères** (projet dans OneDrive) : cache/build PlatformIO redirigés vers
  `%LOCALAPPDATA%\idfm\ws`, et `bruce/.pio` est une jonction vers ce dossier (les scripts de
  Bruce codent en dur `.pio/libdeps`). Recommandé : déplacer le dépôt hors OneDrive (`C:\dev`).
- **Variables MSYS/MinGW** héritées du terminal (Git Bash, Orca) : l'installeur ESP-IDF refuse
  de tourner → supprimées dans le script.
- **`pio.exe` bloqué** (« Access denied ») : on lance `python -m platformio`.
- **FastLED 3.10.9** casse la compilation (conflit avec `#define FP` de Bruce) → figé en 3.10.3
  par `patches/0002-pin-fastled-3.10.3.patch`.
- Premier build ≈ 20 min (téléchargements + compilation complète) ; ensuite incrémental.

## Questions ouvertes
- Modèle exact : T-Embed CC1101 « Plus » utilise-t-il bien l'env Bruce `lilygo-t-embed-cc1101` ?
