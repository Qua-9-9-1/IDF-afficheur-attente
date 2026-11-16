# Référence visuelle — écrans de station tramway

Référence de design pour `app/idfm/board_tram.*`. Niveaux de confiance (comme `METRO.md`) :

- **[sourcé]** : confirmé par une source citée en bas de page ;
- **[observé]** : souvenir d'observation des écrans réels ou de l'appli RATP, **à confirmer sur
  photo** (déposer les photos dans `docs/reference/photos/`) ;
- **[choix]** : adaptation propre à notre écran 320×170.

> Limite de la recherche (2026-11-16) : aucune source écrite trouvée ne décrit la mise en page,
> les couleurs ou les libellés des écrans de station tramway (ratp.fr répond 403 aux outils
> automatiques ; Wikipédia ne décrit pas l'information voyageurs en station). Seuls le principe
> de l'écran, les pictogrammes IDFM et les couleurs de lignes sont sourcés. Tout le reste est
> une approximation cohérente avec les écrans métro (même famille SIEL RATP).

## 1. Ce que l'on sait des écrans de station

- À chaque station RATP : un plan de ligne et **un écran qui indique la destination et le temps
  d'attente avant les prochains tramways** [sourcé, guide voyageur RATP].
- Le système **SIEL** de la RATP (temps réel) équipe « près de 160 lignes de bus ou tramways »
  [sourcé, techno-science SIEL] ; sur bus : **temps d'attente des 2 prochains passages** +
  messages de perturbation [sourcé, SIEL bus].
- Une station = deux quais = **un écran par sens** (« Direction X »), comme le métro [observé].
- Lignes exploitées hors RATP (T4, T11, T14 : STRETTO/SNCF-Keolis ; T9, T10, T12, T13 : Keolis /
  RATP Cap / RD Bièvre) : écrans de fournisseurs différents, contenu équivalent (direction,
  2 prochains passages en minutes) [observé, non vérifié] — **pas de rendu spécifique** [choix].
- Libellés : « à l'approche », « à quai » / « à l'arrêt », « Service terminé », « Info
  indisponible » — vocabulaire des écrans SIEL et de l'ancienne API temps réel RATP (« A
  l'approche », « A l'arret », « Service termine », « INFO INDISPO ») [observé, à confirmer].
  Pour le tram on retient **« à quai »** (les arrêts de tram RATP sont des stations à quais)
  [choix, à confirmer : « à l'arrêt » possible].
- Moins d'une minute : « à l'approche » plutôt que « 0 min » [observé appli/API RATP, à confirmer].

## 2. Pictogrammes IDFM (charte 2019) [sourcé]

Fichiers Wikimedia Commons *Paris transit icons - Tram.svg* et *- Tram 3a.svg* (publiés par IDFM) :

- **Mode tram** : carré blanc entre **deux barres horizontales arrondies** gris ardoise `#25303B`
  (haut et bas, ~15 % de la hauteur chacune), silhouette de tram ardoise au centre (caisse à
  avant arrondi, 3 baies, pantographe en diagonale, rail).
- **Ligne** : même gabarit, barres **à la couleur de la ligne**, nom « T3a » en ardoise sur fond
  blanc (« T » et chiffre en capitales, suffixe « a/b » en minuscule plus petite).
- Adaptation : plaques de 28 px de haut, barres de 4 px ; ligne en ×2 gras (« T3a », « T11 »),
  largeur = texte + 9 px ; la silhouette est redessinée en primitives [choix].

## 3. Couleurs officielles des lignes (open data IDFM, `referentiel-des-lignes`) [sourcé]

Relevé le 2026-11-16 (`colourweb_hexa`, `transportmode = tram`) ; utilisées en repli quand
`Line.color` et `Line.textColor` valent 0. Le texte du pictogramme est toujours ardoise sur
blanc (la couleur sert aux barres).

| Ligne | Couleur | Exploitant | Ligne | Couleur | Exploitant |
|---|---|---|---|---|---|
| T1 | `#0055C8` | RATP | T8 | `#6E6E00` | RATP |
| T2 | `#A0006E` | RATP | T9 | `#3C91DC` | Keolis |
| T3a | `#FF5A00` | RATP | T10 | `#6E6E00` | RD Bièvre |
| T3b | `#00643C` | RATP | T11 | `#FF5A00` | STRETTO |
| T4 | `#DC9600` | STRETTO | T12 | `#A50034` | RATP Cap |
| T5 | `#640082` | RATP | T13 | `#8D653D` | RATP Cap |
| T6 | `#FF0000` | RATP | T14 | `#00A092` | STRETTO |
| T7 | `#6E491E` | RATP | | | |

NB : le SVG Commons du T3a utilise `#F28E42` (teinte « print ») ; on garde la valeur web.

## 4. Adaptation à l'écran 320×170 [choix sauf mention]

```
 0 ┌──────────────────────────────────────────────┐
   │[tram][T3a] Direction                   14:03 │ en-tête 40 px (« : » clignote)
   │            Pont du Garigliano                │ nom ×2 sur toute la largeur
40 ├──────────────────────────────────────────────┤
   │Prochain tram                           à l'  │ rangée 53 px
   │Pont du Garigliano                  approche  │ (clignote)
94 ├──────────────────────────────────────────────┤
   │Tram suivant                                  │ rangée 53 px
   │Pont du Garigliano                      4 min │ chiffres ×4
148├──────────────────────────────────────────────┤
   │[INFO] T3a : travaux … (défilement)           │ bandeau 21 px
170└──────────────────────────────────────────────┘
```

- **En-tête** : pictogrammes mode + ligne [sourcé pour le dessin] ; « Direction » en petit gris
  sur la ligne de l'heure, nom du terminus en ×2 sous l'heure (jusqu'à 18-19 caractères, ex.
  « Pont du Garigliano ») ; sinon abrégé (« St- »), puis ×1 sur une ou deux lignes.
- **2 trams** visibles (`TRAM_BOARD_ROWS = 2`) [sourcé pour le principe des 2 passages SIEL] ;
  l'encodeur fait défiler les suivants.
- **Rangée** : « Prochain tram » / « Tram suivant » / « Ne tram » + destination du tram (diffère
  de la direction pour les services partiels) ; trop longue : abréviation, deux lignes ×2 (sans
  libellé de rang), troncature.
- **Temps** à droite, ambre `#FFB400` (héritage SIEL, comme le métro) :
  `N` ×4 + « min » ; < 1 min ou statut « approche » → « à l'/approche » clignotant ;
  ≥ 60 min → HH:MM (heure de Paris) ; « à quai » ×3 fixe ; « Supprimé » rouge (destination
  grisée) ; inconnu → « -- » ; départ passé de plus de 60 s → masqué.
- **États** : « Chargement… » ; erreur sans données « Information indisponible » + détail ;
  erreur avec données → « Informations non actualisées » dans le bandeau ; liste vide →
  « Service terminé » entre 01:00 et 05:00 (Paris), sinon « Aucun tram annoncé ».
- **Bandeau** : messages trafic avec étiquette jaune « INFO », sinon « Tramway T3a • station •
  Mis à jour à HH:MM ».

### Couleurs d'écran retenues [choix, approximations]
| Élément | Couleur |
|---|---|
| Fond | quasi-noir `#06090E` |
| En-tête, bandeau | ardoise foncé `#1A222C` (dérivé de l'ardoise IDFM `#25303B`) |
| Séparateurs | `#3A4656` |
| Textes principaux | blanc |
| Secondaires | gris `#9CA6B4` |
| Temps | ambre `#FFB400` |
| Supprimé | rouge `#FF4040` |

## 5. Points ouverts
- Photos des écrans T3a/T2 (RATP) et T4/T11/T13 (autres exploitants) : fond, police, couleur des
  temps, présence de l'heure.
- « à quai » vs « à l'arrêt » ; « à l'approche » vs « 0 min » / « < 1 min ».
- « Dernier tram » / « Pas de service » : pas de donnée dédiée dans le modèle.

## Sources
- RATP, *Vous prenez le tramway ?* (403 aux outils, extrait via moteur de recherche) : https://www.ratp.fr/visiter-paris/guide-voyageur/tramway
- techno-science.net, *SIEL (bus)* : https://www.techno-science.net/fr/definitions/siel-bus
- techno-science.net, *SIEL (métro de Paris)* : https://www.techno-science.net/fr/definitions/siel-metro-de-paris
- Wikimedia Commons, *Paris transit icons - Tram 3a.svg* : https://commons.wikimedia.org/wiki/File:Paris_transit_icons_-_Tram_3a.svg
- Wikimedia Commons, *Paris transit icons - Tram.svg* : https://commons.wikimedia.org/wiki/File:Paris_transit_icons_-_Tram.svg
- IDFM open data, référentiel des lignes (tram) : https://data.iledefrance-mobilites.fr/explore/dataset/referentiel-des-lignes/
