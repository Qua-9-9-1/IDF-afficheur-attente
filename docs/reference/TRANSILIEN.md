# Référence visuelle — écrans de quai Transilien (SNCF)

Référence de design pour `app/idfm/board_transilien.*` (lignes H, J, K, L, N, P, R, U, V).
Niveaux de confiance (comme `RER.md` et `METRO.md`) :

- **[sourcé]** : confirmé par une source citée en bas de page ;
- **[observé]** : souvenir d'observation des écrans réels, **à confirmer sur photo**
  (déposer les photos dans `docs/reference/photos/`) ;
- **[choix]** : adaptation propre à notre écran 320×170.

> Limite de la recherche (2026-11-16) : transilien.com répond 403 aux outils automatiques ; les
> articles des blogs de ligne SNCF décrivent les écrans en prose, mais leurs images ne sont pas
> lisibles par les outils. Aucune source écrite ne donne la charte couleur exacte ni la grille
> des écrans IENA. Seules les couleurs de lignes sont officielles (open data IDFM).

## 1. Les générations d'afficheurs en gare Transilien

| Génération | Période | Description |
|---|---|---|
| **Infogare** | → fin 2020 | Système historique SNCF ; écrans CRT puis TFT/LCD et LED [sourcé]. Texte blanc sur fond bleu « roi » [sourcé, commentaire de lecteur]. |
| **IENA / Dauphine** (nouvelle charte) | sept.–oct. 2020 → | Programme de modernisation qui remplace Infogare ; Dauphine est la couche de diffusion, police **Achemine** [sourcé]. Lignes N/U dès sept. 2020, RER C nov. 2020 [sourcé]. Gris clair sur bleu foncé à la place du blanc sur bleu roi [sourcé, commentaire de lecteur]. ~8 000 écrans [sourcé]. |

Types d'écrans IENA [sourcé, blogs N/U et RER C] :
- **PIGD** (panneau d'information générale des départs) : départs groupés par gare de destination ;
- **tableau d'accès** : prochains trains toutes voies confondues (≈ 4-6 trains) ;
- **tableau de voie** : **3 prochains trains** d'une voie, détail sur le premier [sourcé] ;
- **tableau de desserte** : le prochain train d'une voie avec ses gares desservies et sa
  composition (court / long) [sourcé] ; liste paginée (« page 1/2 ») [sourcé] ;
- **tableau bidirectionnel** (nouveau type, 2020) [sourcé].

Notre afficheur reproduit le **tableau de voie** (un quai = 3 trains), complété par les infos
d'un tableau d'accès (voie de chaque train), puisque l'API interroge la gare entière.

## 2. Contenu et libellés

- **Heure de départ HH:MM** (heure théorique) comme information principale : exemple « train SARA
  de 16:41 » [sourcé, légende d'image blog N/U]. Les écrans de voie/desserte donnent aussi le
  **délai d'attente** en minutes (« dans 6 minutes », « dans 10 minutes ») [sourcé, blog N/U
  2022, formulation en prose : forme exacte à l'écran à confirmer].
- **Code mission** à 4 lettres (DAPO, POMI, PEMU, SARA, ELBA, VICK…) ; numéro de train pour les
  TER [sourcé]. Écriture normale (plus de police « girouette » pointillée) [sourcé].
- **Destination** en clair (« Paris-Montparnasse ») [sourcé].
- **Voie** : « Voie 1 » (remplace « Quai B » sur le RER C) [sourcé] ; voie en cartouche blanc
  [sourcé, commentaire] ; **changement de voie** en orange (« Changement de voie – Train voie XX »)
  [sourcé] — non géré : l'API ne signale pas le changement.
- **États** [sourcé, blog RER C] : « à l'approche » (≈ 1 min avant l'arrivée), « à quai »,
  « imminent », « stationne » (> 2 min à l'arrêt), « — — » (entre deux gares).
- **Train supprimé** : reste affiché avec la mention « Supprimé » ; sa **voie disparaît** et ses
  informations sont **grisées** ; croix orange sur les tableaux de voie/desserte [sourcé].
- **Retard** : libellé « retard xx min » [observé, non trouvé dans les sources écrites].
- **« à l'heure »** : mention affichée quand le train est à l'heure [observé, à confirmer].
- **Train court / long** : mention « train court » (cartouche vert), rien pour un train long
  [sourcé] — non disponible dans les données PRIM.
- **Desserte** : les gares desservies par le premier train, paginées ; gares supprimées en rouge
  barrées d'une croix ; « desserte modifiée » en orange [sourcé] — **non disponible** : le
  `stop-monitoring` PRIM ne donne pas la liste des arrêts (voir §5).
- **Position du train** : « Se situe entre X et Y », « stationne à X » [sourcé] — non disponible.
- **Encart message** à gauche, couleur selon le type : rouge (perturbation), orange (travaux),
  jaune (perturbation/travaux à venir), noir (sécurité), violet (commercial) [sourcé].
- Pictogrammes : mode « Train » (pictogramme IDFM : face avant d'un train) + carré arrondi de la
  couleur de la ligne avec la lettre [observé — charte IDFM des pictogrammes]. La lettre de ligne
  apparaît sur les écrans de quai (RER C) [sourcé].
- Horloge HH:MM en haut à droite [observé].

## 3. Couleurs

### Couleurs officielles des lignes [sourcé : open data IDFM `referentiel-des-lignes`, 2026-11-16]

| Ligne | Fond (`colourweb_hexa`) | Texte |
|---|---|---|
| H | `#84653D` (C01737) / `#8D653D` (C01842) | blanc |
| J | `#CEC73D` | noir |
| K | `#9B9842` | blanc |
| L | `#C4A4CC` | noir |
| N | `#00B297` | blanc |
| P | `#F58F53` | noir |
| R | `#F49FB3` | noir |
| U | `#B6134C` | blanc |
| V | `#9F9825` | blanc |

Le `Line.color` du référentiel prime quand il est renseigné ; la table ci-dessus est le repli.

### Couleurs d'écran [observé / choix, approximations]

| Élément | Couleur retenue |
|---|---|
| Fond | bleu SNCF foncé `#0A1A44`, lignes alternées `#102455` |
| En-tête, bandeau bas | `#051030` |
| Heure de départ | jaune `#FFD200` [observé, héritage Infogare] |
| Destination | blanc |
| « à l'heure », textes secondaires | gris clair `#C8D0E0` [sourcé : « gris clair sur bleu foncé »] |
| Délai « dans N min », n° de train | gris `#8C96B0` |
| Retard | cartouche orange `#FF8C00` |
| Supprimé | cartouche rouge `#E53232`, ligne grisée `#6A7490` |
| À quai / à l'approche | cartouche jaune, clignotant |
| Horloge | cartouche blanc, chiffres bleu foncé [observé] |

## 4. Adaptation à l'écran 320×170 [choix]

```
 0 ┌─────────────────────────────────────────────────┐
   │[train][L] Paris St-Lazare                [14:04]│ en-tête 26 px
26 ├─────────────────────────────────────────────────┤
   │14:05  Versailles Rive Droite               ┌Voie┐│
   │PEBU   [à quai]                             │ 4  ││ 3 trains × 40 px
   │14:06  Nanterre Université                  ┌Voie┐│
   │NOPE   à l'heure                 dans 1 min │ 7  ││
   │14:11  St-Nom-la-Bretèche Forêt de Marly    ┌Voie┐│
   │SEBO   [retard 5 min]           dans 11 min │ 5  ││
148├─────────────────────────────────────────────────┤
   │[INFO] Travaux : … (défilement)                  │ bandeau 22 px
170└─────────────────────────────────────────────────┘
```

- **3 trains** visibles comme le tableau de voie ; l'encodeur fait défiler (`TRANSILIEN_BOARD_ROWS = 3`).
- Colonne gauche : heure **théorique** (`aimed`) en jaune ×2, code mission en dessous (à défaut le
  n° de train en gris). Un train en retard garde son heure théorique, le retard
  (`expected − aimed`, arrondi à la minute) est dans le cartouche orange.
- Colonne centrale : destination ×2 (abrégée « St- » si besoin), sinon ×1 sur une ligne, sinon
  tronquée ; ligne d'état en dessous ; délai « dans N min » aligné à droite (1 à 59 min,
  calculé sur `expected`).
- Colonne droite : cadre « Voie » + numéro (×2, ×1 au-delà de 2 caractères). Absent si la voie est
  inconnue (PRIM renvoie `unknown` avant l'affectation) ou si le train est supprimé.
- « à quai » / « à l'approche » clignotent (600 ms).
- Départ passé de plus de 60 s (hors « à quai ») : masqué.
- Bandeau bas : messages trafic PRIM (étiquette INFO) ; à défaut « Ligne L • gare • Mis à jour à ».
- États vides : « Chargement… », « Information indisponible » (erreur), « Aucun train annoncé »,
  « Fin de service » (01:30–04:30, heure de Paris).
- Police GLCD 5×7 uniquement, accents CP437 via `idfmGlcd()`, heure via `primLocalTime()`.

## 5. Écarts connus et doutes

- **Desserte** (gares desservies, défilante ou paginée) : élément caractéristique des écrans
  Transilien, mais absent du modèle (`Departure` n'a pas de liste d'arrêts et `stop-monitoring`
  ne la fournit pas). Le bandeau bas sert donc aux messages trafic. Piste : un champ
  `Departure::stops` rempli via un autre service PRIM / GTFS.
- Heure HH:MM vs minutes : on affiche les deux (HH:MM en vedette). À confirmer sur photo
  laquelle est en vedette sur les tableaux de voie IENA récents.
- « à l'heure » et « retard xx min » : libellés de mémoire, non trouvés dans les sources.
- Pictogramme de mode : dessin simplifié de la face avant d'un train (20×20 px).
- Changement de voie, train court/long, position du train, « imminent », « stationne » :
  non gérés faute de données.

## Sources
- Wikipédia, *Infogare* : https://fr.wikipedia.org/wiki/Infogare
- Mes lignes N & U, *De nouveaux affichages sur les écrans de vos lignes N & U* (2020) :
  https://meslignesnetu.transilien.com/2020/09/14/nouveaux-affichages-ecrans-lignes-n-u/
- Mes lignes N & U, *Une nouvelle indication sur vos écrans d'information en gare* (2022) :
  https://meslignesnetu.transilien.com/2022/06/10/ecrans-information-gare-position-geographique/
- Mon RER C et ligne V, *Et si on parlait des nouveaux affichages RER C ?* (2020) :
  https://monrercetlignev.transilien.com/2020/11/17/et-si-on-parlait-des-nouveaux-ecrans-rer-c/
- SNCF Numérique, *Des écrans en gare qui enrichissent l'expérience voyageur* :
  https://numerique.sncf.com/actualites/des-ecrans-en-gare-qui-enrichissent-lexperience-voyageur/
- IDFM open data, *Référentiel des lignes* :
  https://data.iledefrance-mobilites.fr/explore/dataset/referentiel-des-lignes/
- Transilien, affluence sur les écrans de quai (403 aux outils) :
  https://www.transilien.com/fr/page-deplacements/affluence
