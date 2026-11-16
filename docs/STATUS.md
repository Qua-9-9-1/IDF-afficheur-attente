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

## Décisions
| Sujet | Décision |
|---|---|
| Intégration | App C++ native dans le firmware Bruce (pas de script JS) |
| Branches | `main` + `feature/*` |
| Toolchain | PlatformIO local installé par `scripts/setup.ps1` (rien de lourd dans le dépôt) |
| Clé API | Lue sur la SD (`/idfm/config.json`), jamais commitée |
| Licence | AGPL-3.0 (imposée par Bruce si on distribue le firmware) — à confirmer |

## À faire (prochaines étapes)
1. **Utilisateur** : créer un compte PRIM et générer une clé API ; copier `sd/idfm/*` sur la
   carte SD (`/idfm/`) avec `config.json` rempli.
2. Flasher et tester sur l'appareil : démo RER, puis données réelles (essayer d'abord
   `STIF:StopArea:SP:45102:` = Châtelet - Les Halles).
3. Photos de vrais panneaux RER → `docs/reference/photos/` pour valider couleurs, en-tête,
   règle « < 1 min » / bascule en HH:MM (points marqués « de mémoire » dans `RER.md`).
4. Vérifier le quota PRIM réel (≈ 1000 req/jour ?) ; `refreshSeconds` par défaut passé à 90.
5. Autres modes : métro, Transilien, tram, bus (le générateur gère déjà `--modes`).

## Pièges de la toolchain (déjà gérés par `scripts/build.ps1`)
- **Chemins > 260 caractères** : build PlatformIO redirigé vers `%LOCALAPPDATA%\idfm\ws-<hash>`
  (un espace par clone/worktree, clé = hash du chemin du dépôt), et `bruce/.pio` est une jonction
  vers ce dossier (les scripts de Bruce codent en dur `.pio/libdeps`). Le cache de téléchargements
  `%LOCALAPPDATA%\idfm\cache` est partagé. Dépôt déplacé hors OneDrive : un déplacement change
  le hash → nouvel espace propre (l'ancien `ws*` peut être supprimé à la main).
- **`git` + PowerShell 5.1** : la progression écrite sur stderr devient une erreur fatale →
  `setup.ps1` l'encapsule.
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
