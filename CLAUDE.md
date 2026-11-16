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
scripts/             setup.ps1 (toolchain), build.ps1 (firmware), sim.ps1 (simulateur), tools.ps1 (commun)
sim/                 Simulateur navigateur : couches Arduino/Bruce imitées + WebAssembly + serveur local
docs/                STATUS.md (avancement), IDFM_API.md (notes API), références visuelles
.tools/              (ignoré par git) TOUS les outils installés : Python portable, PlatformIO, builds
```

**Règle d'installation : rien n'est installé sur la machine.** Tout outil (Python, PlatformIO,
compilateurs, dépendances, caches, builds) va dans `.tools/` du dépôt, ignoré par git
(voir `scripts/tools.ps1`). Pas de winget, pas de `~/.platformio`, pas de `%LOCALAPPDATA%`.
Le poste bloque l'exécution des `.exe` compilés localement → le simulateur tourne en
WebAssembly dans le navigateur, servi par le Python local.

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
.\setup.cmd                         # une fois : Python portable, PlatformIO, sous-module Bruce
.\build.cmd                         # intègre l'app et compile le firmware
.\build.cmd -Upload                 # compile et flashe (USB)
.\build.cmd -Monitor                # moniteur série
.\sim.cmd                           # simulateur navigateur (app complète)
.\sim.cmd -Demo                     # simulateur, panneau RER de démo hors ligne
.\sim.cmd -Demo -Shot x.png -ExitAfter 3000   # capture sans fenêtre → .tools\sim\shots\
```

Les `.cmd` à la racine appellent `scripts\*.ps1` avec `-ExecutionPolicy Bypass` (portée : ce
lancement seulement) car la politique Windows par défaut (« Restricted ») bloque les `.ps1` ;
ne jamais modifier la politique d'exécution de la machine.

Pièges connus de la toolchain Windows : voir la section dédiée de `docs/STATUS.md`.
Le build firmware exige un dépôt à chemin court (limite 260 caractères), ex. `C:\dev\idfm`.

## Rappels pour l'IA
- Lire `docs/STATUS.md` avant de commencer, le mettre à jour avant de finir.
- Ne jamais pousser ni flasher sans accord explicite de l'utilisateur.
- Vérifier l'API PRIM sur la doc officielle (les formats d'identifiants d'arrêt ont changé
  dans le temps : `STIF:StopPoint:Q:xxxxx:` vs `IDFM:xxxxx`).
