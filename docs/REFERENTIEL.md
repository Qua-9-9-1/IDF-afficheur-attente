# Référentiel lignes / arrêts

Comment l'appareil liste les lignes d'un mode, puis les arrêts d'une ligne dans l'ordre du
parcours, chacun avec le `MonitoringRef` à passer à l'API PRIM « Prochains passages »
(`/marketplace/stop-monitoring?MonitoringRef=...`).

**Choix retenu** : un générateur hors ligne (`scripts/gen_referential.py`, Python 3.10+,
bibliothèque standard uniquement) télécharge les données ouvertes IDFM et produit de petits
fichiers JSON à copier sur la carte SD (`/idfm/`). Le firmware les lit avec ArduinoJson
(`app/idfm/referential.cpp`). Pas d'appel réseau pour naviguer dans les menus, pas de clé API
nécessaire pour générer.

## Sources de données

Toutes sur le portail open data IDFM (Opendatasoft), sans clé, licence ODbL / Etalab :
export via `https://data.iledefrance-mobilites.fr/api/explore/v2.1/catalog/datasets/<id>/exports/json?select=...&where=...`.

| Jeu de données (id) | Contenu utilisé | Remarques |
|---|---|---|
| [`referentiel-des-lignes`](https://data.iledefrance-mobilites.fr/explore/dataset/referentiel-des-lignes/) | `id_line` (ex. `C01742`), `shortname_line`, `transportmode`, `networkname`, `colourweb_hexa`, `textcolourweb_hexa`, `status` | RER = `transportmode="rail"` **et** `networkname="RER"` (sous-mode `local`) ; Transilien = `networkname="Transilien"` ; métro / tram / bus = `transportmode` `metro` / `tram` / `bus`. |
| [`arrets-lignes`](https://data.iledefrance-mobilites.fr/explore/dataset/arrets-lignes/) | `id` (= `IDFM:<ID_Line>`), `stop_id`, `stop_name` | Liste **non ordonnée** des arrêts d'une ligne. Pour le ferré SNCF/RER, `stop_id` = `IDFM:monomodalStopPlace:<ZdAId>` (zone d'arrêt) ; pour métro/bus, `stop_id` = `IDFM:<ArRId>` (arrêt/quai). Sert de filtre (« arrêts officiels de la ligne »). |
| [`zones-d-arrets`](https://data.iledefrance-mobilites.fr/explore/dataset/zones-d-arrets/) | `zdaid`, `zdaname` (+ `zdcid`, `zdatype`) | Nom affiché de chaque zone d'arrêt (ZdA). |
| [`arrets`](https://data.iledefrance-mobilites.fr/explore/dataset/arrets/) | `arrid` → `zdaid` | Remonte un quai (ArR, ex. métro `IDFM:463158`) à sa zone d'arrêt. |
| [`offre-horaires-tc-gtfs-idfm`](https://data.iledefrance-mobilites.fr/explore/dataset/offre-horaires-tc-gtfs-idfm/) | `https://eu.ftp.opendatasoft.com/stif/GTFS/IDFM-gtfs.zip` (≈ 117 Mo, `stop_times.txt` ≈ 710 Mo décompressé) | **Seule source de l'ordre des arrêts** : `route_id` = `IDFM:<ID_Line>`, `trips.txt` + `stop_times.txt`. Mis à jour 3×/jour, couvre les 30 prochains jours. Doc : [opendata_gtfs.pdf](https://eu.ftp.opendatasoft.com/stif/GTFS/opendata_gtfs.pdf). |

Hiérarchie des identifiants (Référentiel Arrêts IDFM) :
**ArR / quai** (`arrid`, ex. `412833`) ⊂ **ZdA / zone d'arrêt** (`zdaid`, ex. `45102`
Châtelet - Les Halles, monomodale) ⊂ **ZdC / zone de correspondance** (`zdcid`, ex. `474151`,
multimodale : RER + métro + bus de Châtelet).

Dans le GTFS IDFM, les `stop_times` SNCF référencent directement `IDFM:monomodalStopPlace:<ZdA>`
(parent = la ZdC `IDFM:<zdcid>`), alors que les quais RATP `IDFM:<ArR>` ont pour parent
directement la ZdC : le passage quai → ZdA se fait donc par le jeu `arrets`.

## Format du `MonitoringRef`

Formats acceptés par PRIM stop-monitoring (doc PRIM « Identification des objets ») :

- zone d'arrêt : `STIF:StopArea:SP:<ZdAId>:` (exemple de la doc : `STIF:StopArea:SP:43111:`,
  gare de Clamart — `43111` est bien un `zdaid` de type `railStation` dans `zones-d-arrets`) ;
- quai : `STIF:StopPoint:Q:<ArRId>:` (exemple : `STIF:StopPoint:Q:412833:` Massy-Palaiseau RER B) ;
- ligne (`LineRef`) : `STIF:Line::<ID_Line>:` (ex. `STIF:Line::C01743:` pour le RER B).

PRIM indique que **depuis le 13/03/2025, les prochains passages SNCF ne sont disponibles
qu'à la maille zone d'arrêt** et recommande les requêtes par zone d'arrêt / de correspondance
pour les RER et Transiliens.

**Retenu : `STIF:StopArea:SP:<ZdAId>:` pour tous les arrêts** (RER, mais aussi métro/tram/bus
quand on les générera : une requête par ZdA renvoie les deux sens, ce qu'affiche un panneau).
Le filtrage par ligne (`LineRef`) se fait côté client sur `MonitoredVehicleJourney.LineRef`.

Confiance : **élevée** pour le RER (format documenté + données SNCF uniquement par ZdA).
**Moyenne** pour métro/bus par ZdA (format documenté et générique, non testé faute de clé) —
à valider dès qu'une clé PRIM est disponible ; repli possible sur `StopPoint:Q` par quai.

> Remarque : l'exemple du modèle (`STIF:StopArea:SP:474151:`) utilise l'id de la **ZdC** de
> Châtelet ; la doc PRIM accepte aussi les zones de correspondance. Le générateur produit
> l'id de **ZdA** (`45102` pour Châtelet - Les Halles RER), plus précis (pas de bus/métro mêlés).

## Ordre des arrêts

`arrets-lignes` n'est pas ordonné : l'ordre vient du GTFS.

1. Toutes les courses (`trips`) de la ligne sont lues, chaque course donne une séquence de ZdA
   (`stop_sequence`), retournée si `direction_id = 1`.
2. Les séquences distinctes (« missions ») sont comptées ; celles de moins de `--min-trips`
   courses (2 par défaut) sont ignorées, et seules les ZdA présentes dans `arrets-lignes` pour
   cette ligne sont gardées (élimine détours de travaux, bruit).
3. Fusion des branches : la mission la plus longue sert d'épine dorsale ; les arrêts absents
   sont insérés **en blocs** :
   - détour entre deux arrêts communs → inséré juste après le premier ;
   - branche (avant le premier ou après le dernier arrêt commun) → ajoutée **à la fin**, lue
     depuis l'embranchement vers le terminus.

   Exemple RER A : *Marne-la-Vallée - Chessy → … → Cergy le Haut*, puis
   *Fontenay-sous-Bois → … → Boissy-Saint-Léger*, puis *Nanterre Université → … →
   Saint-Germain-en-Laye*, puis *Achères Grand Cormier → Poissy*.
4. Les arrêts de `arrets-lignes` sans aucune course GTFS (ex. fermeture pour travaux sur 30 j,
   ou incohérence du référentiel) sont ignorés et signalés (option `--keep-unserved` pour les
   ajouter en fin de liste, triés alphabétiquement).

## Fichiers produits (`sd/idfm/` → copier dans `/idfm/` de la carte SD)

```json
// lines.json
{"v":1,"generated":"2026-11-10","lines":[
  {"id":"C01742","shortName":"A","mode":"RER","color":"EB2132","textColor":"FFFFFF"}, ...]}
// stops_C01742.json
{"v":1,"line":"C01742","stops":[
  {"name":"Marne-la-Vallée - Chessy","ref":"STIF:StopArea:SP:43239:"}, ...]}
```

Noms en UTF-8 (accents conservés) ; la translittération ASCII est faite sur l'appareil.
Clés de mode : `METRO`, `RER`, `TRANSILIEN`, `TRAM`, `BUS`.

Génération du 2026-11-10 (RER) :

| Ligne | Id | Arrêts | Missions GTFS | Taille |
|---|---|---|---|---|
| RER A | C01742 | 46 | 71 | 2,8 Ko |
| RER B | C01743 | 47 | 51 | 2,8 Ko |
| RER C | C01727 | 75 | 60 | 4,6 Ko |
| RER D | C01728 | 59 | 41 | 3,5 Ko |
| RER E | C01729 | 26 | 12 | 1,6 Ko |
| `lines.json` | | 5 lignes | | 0,5 Ko |

## Utilisation du générateur

```powershell
python scripts\gen_referential.py                          # RER A-E -> sd\idfm\
python scripts\gen_referential.py --modes RER,METRO,TRAM   # plusieurs modes
python scripts\gen_referential.py --lines A,B              # filtrer (nom court ou id)
python scripts\gen_referential.py --gtfs C:\chemin\IDFM-gtfs.zip   # GTFS déjà téléchargé
```

Cache : `%LOCALAPPDATA%\idfm\referential-cache` (24 h par défaut, `--max-age`, `--refresh`).
Durée : ≈ 35 s une fois le GTFS en cache (lecture en flux de `stop_times.txt`).
Testé aussi avec `--modes METRO,TRAM` (16 lignes de métro, 15 de tram, 0,3 à 2,3 Ko par ligne).
Le mode `BUS` fonctionne mais produit ~1 900 fichiers : à filtrer avec `--lines`.

## Côté firmware

- `idfmLoadLines(mode, out)` : lit `/idfm/lines.json` (SD, sinon LittleFS) ; si absent ou sans
  ligne de ce mode → lignes RER A–E intégrées au firmware (ids et couleurs ci-dessus).
- `idfmLoadStops(line, out)` : lit `/idfm/stops_<id>.json` ; si absent → quelques grandes gares
  intégrées par ligne RER (Châtelet, Gare de Lyon, Gare du Nord, La Défense…).
- `selectLine` (menu Bruce `MENU_TYPE_SUBMENU`) / `selectStop` (`MENU_TYPE_REGULAR` : 6 lignes
  visibles, fenêtre défilante, nom long en défilement, en-tête « RER [A] » aux couleurs de la
  ligne). Retour (`Esc`) → `false`.

## Points ouverts

- Format `StopArea:SP:<ZdA>` non encore vérifié en réel (pas de clé PRIM) — à tester d'abord
  sur Châtelet - Les Halles (`STIF:StopArea:SP:45102:`) et comparer avec la ZdC `474151`.
- RER E : `arrets-lignes` associe Houilles, Maisons-Laffitte et Sartrouville à la ligne E sans
  aucune course GTFS → ignorés. Le prolongement ouest (Mantes) n'apparaît pas encore dans le GTFS.
- L'ordre des branches dépend de la mission la plus longue du moment : il peut changer d'une
  génération à l'autre (travaux). Sans conséquence fonctionnelle.

## Sources

- PRIM — Identification des objets (prochains passages) :
  https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/identification-des-objets
- PRIM — Structure des requêtes :
  https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/structure-des-requetes-parametres-dappel
- PRIM — Jeu de données arrêts et lignes associées :
  https://prim.iledefrance-mobilites.fr/fr/actualites/article/jeu-de-donnees-arrets-lignes-associees
- PRIM — Référentiels arrêts et lignes :
  https://prim.iledefrance-mobilites.fr/fr/actualites/article/referentiels-arrets-et-lignes
- Portail open data IDFM : https://data.iledefrance-mobilites.fr/ (jeux cités dans le tableau)
- Documentation GTFS IDFM : https://eu.ftp.opendatasoft.com/stif/GTFS/opendata_gtfs.pdf
