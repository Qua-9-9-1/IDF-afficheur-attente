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

**Retenu pour le RER : `STIF:StopArea:SP:<ZdAId>:`** (une requête par ZdA renvoie les deux
sens, ce qu'affiche un panneau de gare). Le filtrage par ligne (`LineRef`) se fait côté client
sur `MonitoredVehicleJourney.LineRef`. Confiance : **élevée** (format documenté + données SNCF
uniquement par ZdA).

### Métro : un `MonitoringRef` par quai (= par direction)

Un afficheur de quai métro ne montre que les trains de **son** quai, donc d'une direction.

**Retenu pour le métro : `STIF:StopPoint:Q:<ArRId>:`**, l'id du quai de départ de la direction
choisie ; la ZdA (`STIF:StopArea:SP:<ZdAId>:`) reste dans `ref` de l'arrêt (repli, et pour un
éventuel affichage « tous sens »).

Ce que dit la doc PRIM (pages en 403 pour les robots, vérifié via les extraits indexés) :

- « Structure des requêtes » : *ArR, zone d'embarquement (ex-ZDER) : `STIF:StopPoint:Q:[ArRId]:`
  — exemple : quais du M4 à Châtelet : `22092` (aller) et `463158` (retour)* ; donc **un id de
  quai par sens** ;
- « Exemple de réponses — niveau quai (retour) » : requête
  `stop-monitoring?MonitoringRef=STIF:StopPoint:Q:463158:` (même quai) ;
- « Identification des objets » : restriction **SNCF seulement** à la maille ZdA depuis le
  13/03/2025 — rien de tel pour la RATP.

Correspondance GTFS → PRIM : dans le GTFS IDFM, les `stop_times` RATP référencent les quais
`IDFM:<ArRId>` (`location_type=0`, parent = la ZdC `IDFM:<zdcid>`) et le numéro est **le même**
que dans `STIF:StopPoint:Q:<ArRId>:`. Vérifié sur l'exemple de la doc : dans le GTFS,
`IDFM:22092` et `IDFM:463158` sont bien les deux quais « Châtelet » de la ligne 4 (parent
`IDFM:71264`), et le générateur trouve `22092` → *Bagneux - Lucie Aubrac* (aller,
`direction_id=1`) et `463158` → *Porte de Clignancourt* (retour). Cohérent avec la doc.

Confiance : **élevée** sur le format et la correspondance GTFS ↔ PRIM (exemple officiel
retrouvé tel quel dans les données) ; **moyenne-haute** sur « la réponse ne contient que les
trains de ce quai » (c'est le sens de la doc — un id par sens, page « niveau quai » —, mais pas
encore observé en réel faute de clé PRIM). À tester en premier avec la clé :
`STIF:StopPoint:Q:463158:` doit ne renvoyer que des *Porte de Clignancourt*.

**Repli si les quais ne répondaient pas** : interroger la ZdA (`ref` de l'arrêt) et filtrer
côté client chaque `MonitoredStopVisit` : d'abord sur l'id du quai (`MonitoringRef` de la visite
ou `MonitoredCall.StopPointRef`, à comparer au `ref` de la direction), sinon sur
`DestinationRef`/`DestinationName` ∈ terminus de la direction (le nom de la direction est
justement la liste de ces terminus, cf. plus bas). Ne pas se fier à `DirectionRef` seul : en
SIRI c'est « aller/retour » sans garantie d'alignement avec `direction_id` du GTFS.

### Récapitulatif par mode (vérifié en réel le 2026-11-16, ≈ 14 h)

| Mode | `MonitoringRef` interrogé | Choix dans l'appli | Vérification en réel |
|---|---|---|---|
| RER | `STIF:StopArea:SP:<ZdA>:` | ligne → gare | ✅ |
| Transilien (H J K L N P R U V) | `STIF:StopArea:SP:<ZdA>:` (comme le RER, pas de directions : un écran de gare liste tous les trains) | ligne → gare | ✅ Saint-Lazare `SP:58566` : 160 passages, lignes J, L **et TER** (filtrage par ligne côté client) |
| Métro | `STIF:StopPoint:Q:<quai>:` | ligne → station → direction | ✅ `Q:463158` |
| Tram (T1…T14) | `STIF:StopPoint:Q:<quai>:` (comme le métro) | ligne → station → direction | ✅ RATP T3a `Q:24117` (1 sens), STRETTO T4 `Q:471008`, Keolis T9 `Q:479008`, RD Bièvre T10 `Q:486840`, RATP Cap T12 `Q:486878` / T13 `Q:480908`, STRETTO T14 `Q:492418` ; ⚠ **T11 : aucune donnée** ni par quai (`Q:470688`, `Q:472986`) ni par zone (`SP:462335`) au moment du test |
| Bus (≈ 1 860 lignes, Noctilien compris) | `STIF:StopPoint:Q:<poteau>:` **sans filtre de ligne** (l'écran d'abribus montre toutes les lignes du poteau) | groupe → ligne → arrêt → direction (= le poteau de ce côté de la rue) | ✅ `Q:27091` (72, réseau RATP Cap : 6 lignes renvoyées), `Q:463505` (482, Keolis Seine Orly), `Q:3958` (4201, TICE / Transdev) |

Constats :
- Les quais tram/bus renvoient bien **un seul sens** (T3a : uniquement *Pont du Garigliano* ; T9 :
  uniquement *Porte de Choisy*). Sur une branche (T4 *Les Coquetiers*), le même quai sert les
  deux terminus de branche (*Aulnay* et *Hôpital de Montfermeil*) : normal.
- La réponse peut porter `STIF:StopPoint:BP:<id>:` au lieu de `Q:` dans `MonitoringRef` (T9,
  TICE) : on interroge en `Q:`, ça fonctionne.
- Au terminus, un quai renvoie aussi les arrivées (T13 *Saint-Cyr* : les deux destinations).
- Un poteau de bus renvoie aussi les **autres lignes** qui s'y arrêtent (ex. 22 et 72 à
  *Versailles - Exelmans*) : c'est voulu pour l'écran d'abribus ; les noms/couleurs viennent de
  `idfmFindLine()`.

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

## Directions du métro, du tram et du bus (`dirs`)

Pour chaque ligne de métro, tram et bus (`PLATFORM_MODES` du générateur), chaque station / arrêt
reçoit la liste de ses directions, tirée des courses GTFS (pour le bus, une direction = le
poteau d'où partent les bus de ce sens) :

1. Pour une station et un `direction_id`, on ne garde que les courses qui **partent** de la
   station (pas celles qui y terminent) : au terminus, il ne reste que la direction de départ.
2. **Quai** (`ref`) : le `stop_id` de départ (`IDFM:<ArRId>` → `STIF:StopPoint:Q:<ArRId>:`) le
   plus utilisé. Si un même sens part de plusieurs quais utilisés chacun par ≥ 20 % des courses,
   chaque quai devient une direction distincte (cas réel unique : ligne 13 vers Châtillon à
   La Fourche, un quai par branche) ; les quais plus rares sont rattachés au quai principal.
3. **Nom** (`name`) : les **vrais terminus** atteignables depuis la station — un dernier arrêt
   qu'aucune autre course du même sens ne dépasse —, du plus desservi au moins desservi, joints
   par « / » ; les terminus de < 5 % des courses et les **terminus partiels** (retournements, ex.
   ligne 7 *Place d'Italie*) sont ignorés, comme sur les plaques de quai. Noms = noms de ZdA.
4. Si deux directions d'une même station portent le même nom (La Fourche), on ajoute
   « (depuis <terminus d'origine>) ».
5. Si les noms restent identiques (même sens, même origine, plusieurs quais utilisés en
   alternance — terminus de tram à deux voies, gares routières), une seule direction est gardée,
   sur la **zone d'arrêt** (`STIF:StopArea:SP:<ZdA>:`) : tous les départs sont listés. Cas
   générés : T3a *Pont du Garigliano*, T4 *Hôpital de Montfermeil* / *Aulnay-sous-Bois*,
   T5 *Marché de Saint-Denis*, T10 *La Croix de Berny*, T11 terminus, et 77 directions de bus.
   (Aucun cas au métro : fichiers métro inchangés.)
6. Ordre : `direction_id = 0` d'abord (le sens de lecture de la liste des stations), puis 1.

Bus : 24 586 arrêts à 2 directions, 14 892 à 1 (lignes en boucle, sens uniques), 188 à 3–4,
516 sans direction (arrêts où les bus ne font que terminer : `ref` de zone d'arrêt utilisé).

Lignes à branches — exemples générés :

| Ligne | Tronc commun | Sur la branche |
|---|---|---|
| 7 vers le sud | `Mairie d'Ivry / Villejuif - Louis Aragon` | `Mairie d'Ivry` ou `Villejuif - Louis Aragon` seul |
| 7 vers le nord | `La Courneuve - 8 Mai 1945` | idem |
| 13 vers le nord | `Saint-Denis - Université / Asnières - Gennevilliers - Les Courtilles` | un seul terminus |
| 13 à La Fourche vers le sud | — | `Châtillon - Montrouge (depuis Saint-Denis - Université)` et `… (depuis Asnières - Gennevilliers - Les Courtilles)` (2 quais) |
| 10 (boucle d'Auteuil), 7bis (boucle), 3bis | stations à sens unique : une seule direction (ex. *Mirabeau* → `Gare d'Austerlitz`, *Danube* → `Louis Blanc`) | |

Le libellé du menu est « Direction <name> » ; les longs libellés défilent (menu
`MENU_TYPE_REGULAR`).

## Fichiers produits (`sd/idfm/` → copier dans `/idfm/` de la carte SD)

```json
// lines.json
{"v":1,"generated":"2026-11-16","lines":[
  {"id":"C01742","shortName":"A","mode":"RER","color":"EB2132","textColor":"FFFFFF"}, ...,
  {"id":"C01386","shortName":"3B","mode":"METRO","color":"82C8E6","textColor":"000000"}, ...]}
// stops_C01742.json (RER : ZdA seulement)
{"v":1,"line":"C01742","stops":[
  {"name":"Marne-la-Vallée - Chessy","ref":"STIF:StopArea:SP:43239:"}, ...]}
// stops_C01374.json (métro : ZdA + un quai par direction)
{"v":1,"line":"C01374","stops":[ ...,
  {"name":"Châtelet","ref":"STIF:StopArea:SP:42587:","dirs":[
    {"name":"Porte de Clignancourt","ref":"STIF:StopPoint:Q:463158:"},
    {"name":"Bagneux - Lucie Aubrac","ref":"STIF:StopPoint:Q:22092:"}]}, ...]}
```

```json
// lines_bus.json (bus : fichier à part, lignes rangées en groupes de sélection)
{"v":1,"generated":"2026-11-16","mode":"BUS","groups":[
  {"name":"1-19","in":"","lines":[{"id":"C…","shortName":"1","color":"…","textColor":"…"}, ...]},
  {"name":"20-99","in":"","lines":[ ..., {"id":"C01107","shortName":"72","color":"FF1400","textColor":"FFFFFF"},
     {"id":"C…","shortName":"39","color":"…","textColor":"…","label":"39 Rive Droite"}, ...]},
  {"name":"1100-1199 Vexin","in":"1000-1999","lines":[...]}, ...]}
```

Noms en UTF-8 (accents conservés) ; la translittération ASCII est faite sur l'appareil.
Clés de mode : `METRO`, `RER`, `TRANSILIEN`, `TRAM`, `BUS`. `lines.json` ne contient plus de bus.

### Lignes de bus retenues

`referentiel-des-lignes` liste 2 063 lignes de bus actives. Sont écartées : le transport à la
demande (`transportsubmode = demandAndResponseBus`, 130), les bus de remplacement SNCF
(`networkname` RER / Transilien / TER, 24) et les autres « Remplacement … » (`route_long_name`
du GTFS, 5), puis les lignes **sans aucune course GTFS** sur 30 jours (46 : lignes saisonnières,
bus de substitution RATP « T1 », « ORLYVAL »…). Restent **1 858 lignes**, dont 59 Noctilien.

### Groupes de sélection (encodeur rotatif, menus ≤ ~60 entrées)

Depuis la renumérotation 2025, les numéros sont quasi uniques en Île-de-France (4 chiffres en
grande couronne, les 2 premiers = le réseau) : on range donc par **numéro**, pas par réseau.

1. Menu « Bus » (20 entrées) : `1-19`, `20-99` (Paris), `100-149`, `150-199`, `200-249`,
   `250-299`, `300-399`, `400-499`, `500-599`, `600-699`, `700-799`, puis `1000-1999 >` …
   `9000-9999 >` (un parent par millier), `Noctilien`, `Autres >`.
2. Sous-menu d'un millier : une entrée par centaine, nommée avec son réseau principal
   (≥ 60 % des lignes) : `1100-1199 Vexin`, `4500-4599 Cœur d'Essonne`… ; une centaine de plus de
   60 lignes est coupée en deux (`6500-6549` / `6550-6599 Poissy - Les Mureaux`).
   `Autres >` : lignes à nom (TVM, Tzen, lettres Paris-Saclay, navettes, « Soir »…) par réseau,
   les réseaux de moins de 5 lignes ensemble dans `Divers`.
3. Liste des lignes du groupe (≤ 60 ; la plus grosse : 64 pour `20-99`), « Bus 72 ». Les
   homonymes d'un même groupe ont un `label` : `39 Rive Droite` / `39 Terres d'Envol`,
   `Soir Franconville Sud`…

Règles appliquées automatiquement par le générateur (`bus_groups`) : centaine coupée en deux
au-delà de 60 lignes, Noctilien coupé `N1-N99` / `N100+` au-delà de 60.

**Noms courts du métro** : ceux du jeu `referentiel-des-lignes` tels quels (`1`…`14`, **`3B`**
et **`7B`** pour 3bis et 7bis — même graphie que le GTFS `route_short_name`). Le menu affiche
« Metro 3B ». Couleurs = `colourweb_hexa` / `textcolourweb_hexa` du même jeu.

Génération du 2026-11-16 (`--modes RER,METRO`) — fichiers RER identiques à ceux du 2026-11-10
(seule la date de `lines.json` change) :

| Ligne | Id | Arrêts | Taille | | Ligne | Id | Arrêts | Taille |
|---|---|---|---|---|---|---|---|---|
| RER A | C01742 | 46 | 2,8 Ko | | Métro 6 | C01376 | 28 | 5,1 Ko |
| RER B | C01743 | 47 | 2,8 Ko | | Métro 7 | C01377 | 38 | 8,1 Ko |
| RER C | C01727 | 75 | 4,6 Ko | | Métro 7B | C01387 | 8 | 1,3 Ko |
| RER D | C01728 | 59 | 3,5 Ko | | Métro 8 | C01378 | 38 | 6,6 Ko |
| RER E | C01729 | 26 | 1,6 Ko | | Métro 9 | C01379 | 37 | 6,9 Ko |
| Métro 1 | C01371 | 25 | 4,6 Ko | | Métro 10 | C01380 | 23 | 4,2 Ko |
| Métro 2 | C01372 | 25 | 4,3 Ko | | Métro 11 | C01381 | 19 | 3,4 Ko |
| Métro 3 | C01373 | 25 | 4,6 Ko | | Métro 12 | C01382 | 31 | 5,9 Ko |
| Métro 3B | C01386 | 4 | 0,6 Ko | | Métro 13 | C01383 | 32 | 7,5 Ko |
| Métro 4 | C01374 | 29 | 5,7 Ko | | Métro 14 | C01384 | 21 | 4,0 Ko |
| Métro 5 | C01375 | 22 | 4,1 Ko | | `lines.json` | | 21 lignes | 1,8 Ko |

Total : ≈ 92 Ko pour les 21 fichiers d'arrêts.

Génération complète du 2026-11-16 (`--modes RER,METRO,TRANSILIEN,TRAM,BUS`) : fichiers RER et
métro **identiques** à la génération précédente (seule la date de `lines.json` change).

| Fichiers | Lignes | Taille |
|---|---|---|
| `lines.json` (RER, métro, Transilien, tram) | 45 | 3,9 Ko |
| `lines_bus.json` (67 groupes) | 1 858 | 139 Ko |
| Transilien `stops_*.json` (H 50 arrêts, J 54, K 10, L 36, N 35, P 32, R 24, U 11, V 7) | 9 | 16 Ko |
| Tram `stops_*.json` (T1 37 arrêts … T14 5) | 15 | 57 Ko (≤ 7,5 Ko par ligne) |
| Bus `bus/stops_*.json` | 1 858 | 7,1 Mo (moyenne 3,8 Ko, max 17,5 Ko) |
| **Total `sd/idfm/`** | 1 907 fichiers | **7,0 Mo** |

Les fichiers d'arrêts bus sont dans le sous-dossier `sd/idfm/bus/` (même chemin `/idfm/bus/` sur
la carte SD), **ignoré par git** : trop volumineux pour être versionné à chaque régénération.
Les régénérer avec `--modes RER,METRO,TRANSILIEN,TRAM,BUS` (≈ 1 min 30 avec le cache GTFS).

## Utilisation du générateur

Avec le Python portable du dépôt (rien d'installé sur la machine) :

```powershell
# contenu actuel de sd\idfm\ :
.\.tools\python\tools\python.exe scripts\gen_referential.py --modes RER,METRO,TRANSILIEN,TRAM,BUS
.\.tools\python\tools\python.exe scripts\gen_referential.py                      # RER A-E seulement
.\.tools\python\tools\python.exe scripts\gen_referential.py --lines A,B          # filtrer (nom court ou id)
.\.tools\python\tools\python.exe scripts\gen_referential.py --gtfs C:\chemin\IDFM-gtfs.zip
.\.tools\python\tools\python.exe scripts\gen_referential.py --modes BUS --verbose  # détail par ligne de bus
```

⚠ `lines.json` ne contient que les modes demandés : toujours passer **les 5 modes** (sinon les
lignes manquantes disparaissent du menu — le firmware retombe alors sur sa liste intégrée RER /
métro). `lines_bus.json` n'est réécrit que si `BUS` est demandé. Les anciens `stops_*.json` ne
sont jamais supprimés.

Cache : `.tools\referential-cache` (24 h par défaut, `--max-age`, `--refresh`) ; nom de fichier
= préfixe lisible + empreinte SHA-1 de la requête (l'ancien nom tronqué à 120 caractères faisait
se confondre les requêtes des différents modes). `arrets-lignes` est téléchargé en entier
(≈ 1 900 lignes ne tiennent pas dans une clause `where` d'URL) puis filtré localement.
Durée : ≈ 1 min 30 pour les 5 modes une fois le GTFS en cache : `stop_times.txt` (8,4 M lignes)
est lu en flux et chaque course est réduite dès sa dernière ligne (les lignes d'une course sont
contiguës dans le GTFS IDFM ; repli plus gourmand en mémoire sinon), la mémoire reste faible.
Le générateur signale les cas particuliers (quais multiples, directions dupliquées) ; pour le
bus, seulement avec `--verbose`.

## Côté firmware

- `idfmLoadLines(mode, out)` : lit `/idfm/lines.json` (SD, sinon LittleFS) ; si absent ou sans
  ligne de ce mode → lignes intégrées au firmware (RER A–E et métro 1–14, 3B, 7B, ids et couleurs
  ci-dessus).
- `idfmLoadStops(line, out)` : lit `/idfm/stops_<id>.json` (`dirs` → `Stop::directions`,
  `Stop::monitoringRef` = la ZdA) ; si absent → quelques grandes gares intégrées par ligne RER
  (Châtelet, Gare de Lyon, Gare du Nord, La Défense…) et quelques stations des métros 1, 4 et 14
  (Châtelet, Gare de Lyon, Nation, Montparnasse…) avec leurs quais.
- `selectLine` (menu Bruce `MENU_TYPE_SUBMENU`, « RER A » / « Metro 3B », pictogramme de la ligne
  survolée dans la barre du haut : rectangle arrondi pour le RER, pastille ronde pour le métro) /
  `selectStop` (`MENU_TYPE_REGULAR` : 6 lignes visibles, fenêtre défilante, nom long en
  défilement, même en-tête) / `selectDirection(line, stop)` (`MENU_TYPE_REGULAR`, « Direction
  La Défense »…, rouvre sur la dernière direction choisie ; sans menu si `directions` est vide
  — RER, `stop` inchangé — ou n'a qu'une entrée — terminus, boucles —, appliquée d'office). Après
  le choix : `stop.direction` = nom, `stop.monitoringRef` = quai. Retour (`Esc`) → `false`.
- Bus : `selectLine` passe par les groupes (`idfmLoadLineGroups` lit seulement les noms des
  groupes, via un filtre ArduinoJson ; `idfmLoadGroupLines(mode, i, lines, &labels)` lit le
  fichier **groupe par groupe** — les groupes précédents sont sautés par filtre — et ne garde que
  le groupe voulu). Menus `MENU_TYPE_REGULAR` : groupes (`1000-1999 >` ouvre un sous-menu) →
  lignes. `Esc` remonte d'un niveau ; au retour du panneau / de la liste d'arrêts, on revient
  directement dans la liste de lignes du dernier groupe. `idfmLoadLines(Bus, …)` garde son API
  (toutes les lignes, lues en flux).
- `idfmFindLine(id, out)` (noms/couleurs des lignes d'un abribus) : recherche brute en flux de
  `"id":"<id>"` (KMP, lecture par blocs de 512 o) dans `lines.json` puis `lines_bus.json`, arrêt
  au premier résultat, seul l'objet trouvé est parsé ; mémoire constante (< 1 Ko). Repli sur
  les lignes intégrées. Repose sur le format écrit par le générateur (JSON compact, objets ligne
  plats commençant par `"id"`).

## Points ouverts

- **T11** : aucune donnée temps réel observée (quai ou zone, 2026-11-16 ≈ 14 h). Le référentiel
  utilise quand même les quais (comme T4/T14, même exploitant, qui répondent). À retester ; si la
  ligne ne répond jamais par quai, passer T11 en zone d'arrêt.
- Bus : l'ordre des arrêts est approximatif pour les lignes dont les deux sens empruntent des rues
  différentes (ex. 72 le long des quais) : les arrêts d'un seul sens sont insérés en blocs.
- Bus : 7 Mo et ~1 900 fichiers sur la SD (et dans git). Si c'est trop, `--lines` permet de
  générer un sous-ensemble ; la sélection fonctionne avec n'importe quel sous-ensemble.
- Taille mémoire sur l'appareil non mesurée : `lines_bus.json` est lu en flux, mais
  `idfmLoadLines(Bus, …)` (utilisé par la démo) construit encore ~1 900 `Line` (~80 Ko).

- Format `StopArea:SP:<ZdA>` non encore vérifié en réel (pas de clé PRIM) — à tester d'abord
  sur Châtelet - Les Halles (`STIF:StopArea:SP:45102:`) et comparer avec la ZdC `474151`.
- RER E : `arrets-lignes` associe Houilles, Maisons-Laffitte et Sartrouville à la ligne E sans
  aucune course GTFS → ignorés. Le prolongement ouest (Mantes) n'apparaît pas encore dans le GTFS.
- L'ordre des branches dépend de la mission la plus longue du moment : il peut changer d'une
  génération à l'autre (travaux). Sans conséquence fonctionnelle.
- Métro : `StopPoint:Q` par quai non encore testé en réel — premier test :
  `STIF:StopPoint:Q:463158:` (M4 Châtelet → Porte de Clignancourt). Si la réponse mélange les
  sens, appliquer le repli décrit plus haut (ZdA + filtre client).
- Les ids de quai viennent du GTFS des 30 prochains jours : un quai provisoire (travaux) peut
  être choisi s'il est majoritaire au moment de la génération → régénérer après les travaux.
- Libellés longs sur les lignes à branches (ligne 13 : 70 caractères) : lisibles en défilement
  dans le menu, mais le panneau métro devra les abréger (ou n'afficher que la destination de
  chaque train, comme les vrais écrans).

## Sources

- PRIM — Identification des objets (prochains passages) :
  https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/identification-des-objets
- PRIM — Structure des requêtes :
  https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/structure-des-requetes-parametres-dappel
- PRIM — Exemple de réponses, niveau quai (retour) (`STIF:StopPoint:Q:463158:`) :
  https://prim.iledefrance-mobilites.fr/fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/exemple-de-reponses-niveau-quai-retour
- PRIM — Jeu de données arrêts et lignes associées :
  https://prim.iledefrance-mobilites.fr/fr/actualites/article/jeu-de-donnees-arrets-lignes-associees
- PRIM — Référentiels arrêts et lignes :
  https://prim.iledefrance-mobilites.fr/fr/actualites/article/referentiels-arrets-et-lignes
- Portail open data IDFM : https://data.iledefrance-mobilites.fr/ (jeux cités dans le tableau)
- Documentation GTFS IDFM : https://eu.ftp.opendatasoft.com/stif/GTFS/opendata_gtfs.pdf
