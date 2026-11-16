# IDF Afficheur Attente

Afficheur miniature des temps d'attente des transports d'Île-de-France (métro, RER,
Transilien, tram, bus) reproduisant les panneaux officiels, sous forme d'application
native pour le firmware [Bruce](https://github.com/pr3y/Bruce) sur **LilyGO T-Embed CC1101**.

Données : API PRIM d'Île-de-France Mobilités.

## Démarrage

```powershell
git clone --recurse-submodules <url> C:\dev\idfm   # chemin court : limite Windows de 260 caractères
.\scripts\setup.ps1          # Python portable + PlatformIO dans .tools\ (rien sur la machine), récupère Bruce
.\scripts\sim.ps1 -Demo      # simulateur dans le navigateur, sans appareil
.\scripts\build.ps1          # compile le firmware Bruce avec l'app
.\scripts\build.ps1 -Upload  # flashe la carte branchée en USB
```

Tous les outils sont installés dans `.tools\` (ignoré par git) ; supprimer ce dossier
désinstalle tout.

Puis copier `sd/idfm/config.example.json` vers `/idfm/config.json` sur la carte SD et
y renseigner la clé API PRIM.

Dans Bruce : menu **Others → IDFM Board**.

Voir [CLAUDE.md](CLAUDE.md) pour l'architecture et les conventions, et
[docs/STATUS.md](docs/STATUS.md) pour l'avancement.
