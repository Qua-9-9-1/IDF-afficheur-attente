# Référence visuelle — écrans de quai métro

Référence de design pour `app/idfm/board_metro.*`. Niveaux de confiance (comme `RER.md`) :

- **[sourcé]** : confirmé par une source citée en bas de page ;
- **[observé]** : souvenir d'observation des écrans réels, **à confirmer sur photo**
  (déposer les photos dans `docs/reference/photos/`) ;
- **[choix]** : adaptation propre à notre écran 320×170.

> Limite de la recherche (2026-11-16) : les pages iledefrance-mobilites.fr répondent 403 aux
> outils automatiques, le communiqué de presse RATP/IDFM (PDF) n'a pas pu être rendu en image,
> et aucune source écrite ne décrit la charte graphique (couleurs, police) des nouveaux écrans
> PANAM. Les couleurs d'écran sont donc des approximations ; seules les couleurs de lignes
> sont officielles (open data IDFM).

## 1. Les générations d'afficheurs de quai métro

| Génération | Période | Description |
|---|---|---|
| **SIEL métro** (« PIQ », panneaux à diodes) | 2004 → | Écran à diodes qui remplace le panneau de direction émaillé en milieu de quai [sourcé]. Chiffres « dessinés en points orange », parfois peu lisibles [sourcé]. Temps mis à jour toutes les 10 s (15 s selon une autre source) [sourcé]. |
| **Ligne 14** (écrans LCD) | 1998/2004 → 2016 (LCD) → 2020 | Temps d'attente à la dizaine de secondes des **3 prochains trains** ; depuis 2020 : direction, info trafic, longueur du train (6 ou 8 voitures) [sourcé]. |
| **Ligne 4** | 2022 → | Écrans LCD intégrés aux portes palières [sourcé]. |
| **PANAM** (« PANneaux d'Affichage Métro », LCD) | déc. 2023 → 2025 | Remplacent les SIEL à diodes, « du même type que ceux de la ligne 14 ». Ligne 5 équipée en premier (44 panneaux, 22 stations) ; ~484 panneaux sur 242 stations [sourcé]. Police agrandie, contrastes renforcés, normes d'accessibilité [sourcé]. Affichent le **temps d'attente des 2 prochains métros** + perturbations, travaux, événements [sourcé]. Variante compacte (ex. Porte Maillot L1) sans plaque émaillée : la direction est alors écrite à l'écran [sourcé]. Variante multidirectionnelle (Invalides L13) [sourcé]. Éteints après la fin du service [sourcé]. |

## 2. Contenu et libellés

### SIEL (génération 1) [sourcé, Wikipédia / techno-science]
- Une **direction par écran** (un quai = une direction), l'écran est placé sous/à côté de la plaque
  « Direction X ».
- **2 prochains trains** ; un seul près du terminus de départ ; **3 bis et 7 bis : un seul**
  (« Prochain train dans 01 minute »).
- Exemples de libellés : « 1er train dans 5 minutes », « 2e train dans 13 minutes » ; annonce
  sonore « Direction Château de Vincennes, prochain train dans une minute, le suivant dans 5
  minutes ».
- **Lignes à branches (7 et 13)** : dans le sens des branches, **4 trains (2 par branche) avec leur
  destination**.
- Train **à l'approche** : la zone de temps **clignote « 00 »** ; train **à quai** : « 00 » fixe.
- Train en retard (régulation, incident) : le temps **clignote** (ex. « 32 », « 64 »).
- Panne / perturbation majeure : toutes les zones affichent « .. » ou « -- » ; IDFM parle des
  « XX » affichés en cas de problème.
- Versions récentes : **heure** avec **deux-points clignotants**.

### PANAM / LCD (génération 2)
- 2 prochains trains, temps en minutes, bandeau d'information trafic [sourcé].
- Fond sombre, textes blancs, temps d'attente en couleur chaude, pictogrammes « M » + numéro de
  ligne en tête [observé, à confirmer sur photo].
- Libellés « à l'approche » / « à quai » à la place du temps [observé, à confirmer ; cohérent
  avec les écrans RER et les annonces sonores].
- « Service terminé » / « Dernier train » : **non trouvés dans les sources** ; les PANAM sont
  éteints hors service [sourcé]. À vérifier sur place.

## 3. Couleurs officielles des lignes (open data IDFM, jeu `referentiel-des-lignes`)

Relevé le 2026-11-16 via l'API data.iledefrance-mobilites.fr (`colourweb_hexa` /
`textcolourweb_hexa`, `transportmode = metro`) [sourcé]. Utilisées en repli quand
`Line.color` et `Line.textColor` valent 0.

| Ligne | Fond | Texte | Ligne | Fond | Texte |
|---|---|---|---|---|---|
| 1 | `#FFBE00` | noir | 8 | `#D282BE` | noir |
| 2 | `#0055C8` | blanc | 9 | `#D2D200` | noir |
| 3 | `#6E6E00` | blanc | 10 | `#DC9600` | noir |
| 3B | `#82C8E6` | noir | 11 | `#6E491E` | blanc |
| 4 | `#A0006E` | blanc | 12 | `#00643C` | blanc |
| 5 | `#FF5A00` | noir | 13 | `#82C8E6` | noir |
| 6 | `#82DC73` | noir | 14 | `#640082` | blanc |
| 7 | `#FF82B4` | noir | 15 | `#AAAAAA` | noir |
| 7B | `#82DC73` | noir | 18 | `#00A092` | blanc |

## 4. Adaptation à l'écran 320×170 [choix sauf mention]

```
 0 ┌──────────────────────────────────────────────┐
   │(M)(1) Direction                        11:54 │ en-tête 36 px (« : » clignote)
   │       La Défense                             │
36 ├──────────────────────────────────────────────┤
   │Prochain train                          à l'  │ rangée 55 px
   │La Défense                          approche  │ (clignote)
92 ├──────────────────────────────────────────────┤
   │Train suivant                                 │ rangée 55 px
   │La Défense                              3 min │ chiffres ×4
149├──────────────────────────────────────────────┤
   │[INFO] Trafic perturbé … (défilement)         │ bandeau 21 px
170└──────────────────────────────────────────────┘
```

- **En-tête** : rond blanc « M » (pictogramme de mode) + rond de ligne à la couleur officielle
  (numéro en ×2, « 3B »/« 7B » pour les bis) [observé pour la forme ; couleurs sourcées] ;
  « Direction » en petit gris au-dessus du nom en ×2 (abrégé « St- », sinon ×1 sur une ou deux
  lignes, ex. « Les Courtilles / St-Denis Université ») — reprend la plaque émaillée, écrite à
  l'écran comme sur les PANAM compacts [sourcé pour le principe]. Heure HH:MM à droite, deux-points
  clignotants (1 Hz) [sourcé SIEL].
- **2 trains** visibles [sourcé] ; l'encodeur fait défiler les suivants (`scrollOffset`).
- **Chaque rangée** : libellé de rang « Prochain train » / « Train suivant » / « Ne train »
  (d'après les libellés SIEL et l'annonce sonore) [choix], puis la **destination du train**
  en ×2 : elle est affichée à chaque rangée car elle diffère d'un train à l'autre sur les lignes
  à branches 7 et 13 [sourcé pour le principe]. Destination trop longue : abréviation, puis deux
  lignes ×2 (le libellé de rang disparaît), puis troncature.
- **Temps d'attente** à droite, ambre `#FFB400` (héritage des diodes orange du SIEL) :
  - `N` en ×4 + « min » en ×2 (même ligne de base) ; arrondi à la minute inférieure comme le SIEL ;
    train dû (< 1 min ou en retard de moins d'1 min) → « 0 min » (équivalent du « 00 ») [choix] ;
  - ≥ 60 min → heure HH:MM en ×3 (Europe/Paris via `primLocalTime()`) [choix] ;
  - statut « à l'approche » → « à l'/approche » en ×2 **clignotant** (le SIEL fait clignoter la
    zone à l'approche) [sourcé pour le clignotement] ;
  - statut « à quai » → « à quai » en ×3 **fixe** (le SIEL affiche « 00 » fixe à quai) [sourcé] ;
  - « Supprimé » en rouge, destination grisée [choix] ;
  - temps inconnu → « -- » [sourcé SIEL] ;
  - retard : pas de mention particulière (le métro n'affiche pas d'horaire théorique) [choix] ;
  - départ passé depuis plus de 60 s sans statut « à quai » → masqué.
- **États** : chargement « Chargement… » ; erreur sans données « Information indisponible » + détail ;
  erreur avec données → rangées conservées + « Informations non actualisées » dans le bandeau ;
  liste vide → « Service terminé » entre 01:15 et 05:30 (heure de Paris), sinon « Aucun train
  annoncé » [choix].
- **Bandeau bas** : messages trafic avec étiquette jaune « INFO » ; à défaut « Métro N • station •
  Mis à jour à HH:MM » (défile s'il dépasse).

### Couleurs d'écran retenues [observé / choix, approximations]
| Élément | Couleur |
|---|---|
| Fond | quasi-noir `#050812` |
| En-tête, bandeau bas | bleu nuit `#0E1630` |
| Séparateurs | `#2A365C` |
| Destination, direction, heure | blanc |
| Libellés secondaires | gris bleuté `#9AA4BC` |
| Temps d'attente | ambre `#FFB400` |
| Supprimé | rouge `#FF4040` |
| Étiquette INFO | jaune `#FFD200` |

## 5. Points ouverts
- Couleurs et typographie exactes des PANAM (photos à recueillir).
- Libellés exacts des PANAM pour « à l'approche », « à quai », « 0 min » vs « < 1 min ».
- Branches 7/13 : le SIEL montre 2 trains **par branche** ; nous montrons les 2 prochains trains
  toutes branches confondues (destination par rangée), les suivants via l'encodeur.
- « Dernier train » : aucune donnée PRIM dédiée dans notre modèle, non affiché.

## Sources
- Wikipédia, *SIEL (métro de Paris)* : https://fr.wikipedia.org/wiki/SIEL_(m%C3%A9tro_de_Paris)
- techno-science.net, *SIEL (métro de Paris)* : https://www.techno-science.net/fr/definitions/siel-metro-de-paris
- RATP / IDFM, communiqué du 20/03/2024 (PANAM) : https://ratpgroup.com/api/media/20240320-Communique-de-presse-IDFM-et-la-RATP-modernisent-les-stations-de-metro-avec-de-nouveaux-panneaux-daffichage-de-quai-1.pdf
- Le Bonbon, nouveaux panneaux de quai : https://www.lebonbon.fr/paris/news/ratp-nouveaux-panneaux-quais-metro-paris/
  et https://en.lebonbon.fr/paris/news/hey-there-have-you-noticed-the-new-signs-on-the-metro-platform
- IDFM, *Des nouveautés pour améliorer l'information voyageurs* (403 aux outils) : https://www.iledefrance-mobilites.fr/actualites/modernisation-information-voyageurs-transport-ile-de-france
- IDFM, *RER B, métro : 30 M€ pour l'information voyageurs sur les quais* (403 aux outils) : https://www.iledefrance-mobilites.fr/actualites/rer-b-metro-30-millions-d-euros-information-voyageurs-quais
- IDFM open data, référentiel des lignes : https://data.iledefrance-mobilites.fr/explore/dataset/referentiel-des-lignes/
