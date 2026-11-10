# CLAUDE.md — Consignes et contexte IA

> À lire en début de chaque session. L'état d'avancement vit dans [docs/STATUS.md](docs/STATUS.md) :
> **le mettre à jour à la fin de chaque session de travail** (ce qui est fait, ce qui reste, décisions prises).

## Le projet

Afficheur d'attente miniature des transports d'Île-de-France, inspiré du produit commercial
**LEDoMATIC** (https://www.ledomatic.com/, code non open-source → on ne copie que le concept).
Objectif : reproduire **trait pour trait** les panneaux d'information voyageurs officiels
(métro, RER, Transilien, tram, bus) en miniature.

- **Matériel** : LilyGO T-Embed CC1101 (Plus) — ESP32-S3, écran TFT ST7789 320×170,
  encodeur rotatif cliquable (« potentiomètre ») + bouton, Wi-Fi, carte SD.
- **Plateforme** : application **C++ native intégrée au firmware [Bruce](https://github.com/pr3y/Bruce)**
  (Arduino-ESP32 via PlatformIO, AGPL-3.0). L'app apparaît comme une entrée de menu Bruce.
- **Données** : API **PRIM d'Île-de-France Mobilités** (https://prim.iledefrance-mobilites.fr/),
  gratuite mais nécessite un compte + clé API (header `apikey`). Endpoint principal :
  « Prochains passages » (SIRI Lite `stop-monitoring`, paramètre `MonitoringRef`).

## Parcours utilisateur cible

1. Choix du **mode de transport** (encodeur pour naviguer, clic pour valider)
2. Choix de la **ligne**
3. Choix de l'**arrêt** (et éventuellement direction)
4. Affichage plein écran façon panneau officiel, rafraîchi périodiquement

## Organisation du dépôt

```
app/idfm/            Sources de l'app (module Bruce) — c'est NOTRE code
bruce/               Sous-module git : firmware Bruce upstream, épinglé (NE PAS modifier à la main)
patches/             Patchs appliqués à Bruce pour y brancher l'app (entrée de menu…)
scripts/             setup.ps1 (toolchain), build.ps1 (intégration + compilation + flash)
docs/                STATUS.md (avancement), IDFM_API.md (notes API), références visuelles
```

L'intégration se fait par `scripts/build.ps1` : il crée une jonction `bruce/src/modules/idfm`
→ `app/idfm`, applique `patches/*.patch` sur le sous-module, puis lance PlatformIO
(env `lilygo-t-embed-cc1101`). Le sous-module reste propre côté git : toute modification
de Bruce doit passer par un fichier dans `patches/`.

## Conventions

### Commits
- **Toujours en anglais**, préfixés par un tag :
  - `[INIT]` initialisation (uniquement au tout début du dépôt)
  - `[ADD]` ajout · `[EDIT]` modification · `[FIX]` correction de bug · `[DEL]` suppression
- Longueur : quelques mots pour un petit commit, **une phrase maximum** pour un gros commit.
- Exemples : `[ADD] Stop selection screen` · `[FIX] Encoder double click` ·
  `[EDIT] Refactor PRIM client to parse departures with ArduinoJson filters`

### Branches
- `main` : toujours compilable.
- `feature/<sujet>` pour chaque chantier, fusionné dans `main` une fois compilé et testé.
- Petits correctifs/docs : directement sur `main` acceptable.

### Code
- C++ (Arduino-ESP32), en suivant les conventions de Bruce (`.clang-format` de Bruce,
  utilisation de `tft`, `bruceConfig`, `loopOptions`, `check(SelPress)`… plutôt que
  réinventer l'UI/les entrées).
- Identifiants et commentaires de code en anglais ; documentation projet en français.
- **Aucun secret commité** : la clé API PRIM est lue à l'exécution depuis la carte SD
  (`/idfm/config.json`) — jamais en dur. `secrets*`, `*.local.*` sont dans `.gitignore`.

## Commandes

```powershell
.\scripts\setup.ps1                 # une fois : Python, PlatformIO, sous-module Bruce
.\scripts\build.ps1                 # intègre l'app et compile
.\scripts\build.ps1 -Upload         # compile et flashe (USB)
.\scripts\build.ps1 -Monitor        # moniteur série
```

Pièges connus de la toolchain Windows : voir la section dédiée de `docs/STATUS.md`.
Log du dernier build : `%LOCALAPPDATA%\idfm-build.log` (si lancé avec redirection).

## Rappels pour l'IA
- Lire `docs/STATUS.md` avant de commencer, le mettre à jour avant de finir.
- Ne jamais pousser ni flasher sans accord explicite de l'utilisateur.
- Vérifier l'API PRIM sur la doc officielle (les formats d'identifiants d'arrêt ont changé
  dans le temps : `STIF:StopPoint:Q:xxxxx:` vs `IDFM:xxxxx`).
