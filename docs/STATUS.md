# État d'avancement

_Dernière mise à jour : 2026-11-16_

## Fait
- Initialisation du dépôt : consignes IA (`CLAUDE.md`), README, `.gitignore`.
- Sous-module Bruce + scripts de toolchain (`scripts/setup.ps1`, `scripts/build.ps1`).
  Toolchain validée : le firmware complet compile (branche `feature-bruce-sdk-toolchain`).
- Squelette de l'app `app/idfm` branché dans le menu « Others » de Bruce via `patches/`.
- **RER (branche `feature/rer-board`) — compile, pas encore testé sur l'appareil :**
  - `idfm_model.h` : modèle commun (ligne, arrêt, départ, données du panneau).
  - `prim_client.*` : config SD, `stop-monitoring` + `general-message`, parsing ArduinoJson
    filtré, horloge UTC propre au module (NTP privé, l'horloge de Bruce n'est pas touchée),
    données fictives. Détails : `docs/IDFM_API.md`.
  - `referential.*` + `scripts/gen_referential.py` : lignes/arrêts RER générés depuis l'open
    data IDFM (GTFS pour l'ordre) → `sd/idfm/*.json` ; repli intégré si la SD est absente.
    Détails : `docs/REFERENTIEL.md`.
  - `selection.*` : écrans ligne → arrêt (`loopOptions`).
  - `board_rer.*` : rendu du panneau RER 320×170 (3 trains, code mission, attente en min,
    « à quai » / « à l'approche » clignotants, bandeau d'infos défilant). Référence de design :
    `docs/reference/RER.md`.
  - `idfm_app.cpp` : flux mode → ligne → arrêt → panneau (encodeur = défilement,
    clic = rafraîchir, Échap = retour) + entrée « RER demo » hors ligne.
- **RER validé par l'utilisateur** dans le simulateur (2026-11-16).
- **Métro (en simulateur, pas encore testé sur l'appareil ni avec l'API réelle)** :
  - Référentiel : 16 lignes (1–14, 3B, 7B), stations dans l'ordre, et pour chaque station les
    directions avec leur quai `STIF:StopPoint:Q:<id>:` (un panneau métro = un quai). Libellés
    des lignes à branches : terminus joints par « / ». Détails : `docs/REFERENTIEL.md`.
  - Parcours : Métro → ligne → station → direction (menu sauté s'il n'y en a qu'une) → panneau.
  - `board_metro.*` : « Direction X », 2 prochains trains (destination par train pour les
    branches), attente en minutes ambre, « à l'approche » clignotant, « à quai », « Supprimé »,
    « Service terminé » la nuit, bandeau INFO. Référence : `docs/reference/METRO.md`.
  - Helpers d'affichage partagés RER/métro : `board_common.*`.
  - À vérifier avec une clé : `STIF:StopPoint:Q:463158:` (ligne 4 Châtelet → Porte de
    Clignancourt) doit ne renvoyer que ce quai.
- **Simulateur navigateur** (`sim/`, `scripts/sim.ps1`) : le code `app/idfm` est compilé tel
  quel en WebAssembly (zig) avec des couches Arduino / TFT_eSPI / Bruce imitées ; même police
  GLCD que l'appareil → rendu du panneau identique au pixel près. Serveur Python local : carte SD
  = `sd/`, proxy PRIM qui garde la clé côté serveur. Validé : démo RER, navigation
  mode → ligne → arrêt, message « config absente ». Les menus Bruce sont une approximation.
  Corrections trouvées grâce à lui : « à l'approche » mal aligné, horloge de la démo figée.
- **Outils 100 % locaux au dépôt** (`.tools/`, `scripts/tools.ps1`) : Python portable (NuGet),
  PlatformIO + toolchains, workspace de build, dépendances du simulateur. Anciennes installations
  machine supprimées le 2026-11-16 (`~/.platformio`, `%LOCALAPPDATA%\idfm`, Python 3.12 winget).

## Décisions
| Sujet | Décision |
|---|---|
| Intégration | App C++ native dans le firmware Bruce (pas de script JS) |
| Branches | `main` + `feature/*` |
| Toolchain | Tout dans `.tools/` (ignoré par git), rien installé sur la machine |
| Simulation | WebAssembly dans le navigateur (le poste bloque les `.exe` compilés localement) |
| Clé API | Lue sur la SD (`/idfm/config.json`), jamais commitée |
| Licence | AGPL-3.0 (imposée par Bruce si on distribue le firmware) — à confirmer |

## À faire (prochaines étapes)
1. **Utilisateur** : créer un compte PRIM et générer une clé API ; la mettre dans
   `sd/idfm/config.json` (ignoré par git) et tester dans le simulateur (`.\scripts\sim.ps1`) ;
   essayer d'abord `STIF:StopArea:SP:45102:` = Châtelet - Les Halles.
2. Valider le build firmware 100 % local depuis un dépôt à chemin court (`C:\dev\…`), puis
   flasher : copier `sd/idfm/*` sur la carte SD (`/idfm/`).
3. Photos de vrais panneaux RER → `docs/reference/photos/` pour valider couleurs, en-tête,
   règle « < 1 min » / bascule en HH:MM (points marqués « de mémoire » dans `RER.md`).
4. Vérifier le quota PRIM réel (≈ 1000 req/jour ?) ; `refreshSeconds` par défaut passé à 90.
5. Autres modes : métro, Transilien, tram, bus (le générateur gère déjà `--modes`).

## Pièges de la toolchain (déjà gérés par `scripts/build.ps1`)
- **Chemins > 260 caractères** : certains fichiers de bibliothèques sont à ~190 caractères sous
  `libdeps` → le dépôt doit être à un chemin court (≤ ~60 caractères, ex. `C:\dev\idfm`) ;
  `build.ps1` avertit sinon. Les worktrees Orca (`…\orca\workspaces\…`) sont trop profondes pour
  le firmware (le simulateur n'est pas concerné). Workspace PlatformIO : `.tools\w` (nom court),
  `bruce/.pio` en est une jonction (les scripts de Bruce codent en dur `.pio/libdeps`).
- **`.exe` compilés localement bloqués** par la sécurité du poste (« Access is denied » ; ni
  AppLocker, ni Smart App Control, ni ASR) → simulateur en WebAssembly. `python.exe` et les
  compilateurs (zig, xtensa) passent.
- **PowerShell 5.1** : toute ligne stderr d'un outil natif (git, pip…) devient une erreur fatale
  sous `Stop` → les scripts tournent en `Continue` et vérifient les codes de sortie (`Assert-Exit`).
- **Variables MSYS/MinGW** héritées du terminal (Git Bash, Orca) : l'installeur ESP-IDF refuse
  de tourner → supprimées dans le script.
- **`pio.exe` bloqué** (« Access denied ») : on lance `python -m platformio`, et un shim
  `pio.cmd` est mis en tête du PATH pour les scripts de Bruce qui appellent `pio` directement.
- **`libnet80211.a` introuvable au link** : le `patch.py` de Bruce (via `pio pkg exec`) a échoué
  à cause du blocage ci-dessus mais a renommé l'original en `.old` → `build.ps1` le régénère
  avec `objcopy` si besoin.
- **FastLED 3.10.9** casse la compilation (conflit avec `#define FP` de Bruce) → figé en 3.10.3
  par `patches/0002-pin-fastled-3.10.3.patch`.
- Premier build ≈ 20 min (téléchargements + compilation complète) ; ensuite incrémental.

## Questions ouvertes
- Modèle exact : T-Embed CC1101 « Plus » utilise-t-il bien l'env Bruce `lilygo-t-embed-cc1101` ?
