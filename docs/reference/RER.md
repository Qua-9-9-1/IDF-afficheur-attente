# Référence visuelle — écrans de quai RER

Référence de design pour `app/idfm/board_rer.*`. Les points sont classés par niveau de confiance :

- **[sourcé]** : confirmé par une source citée en bas de page ;
- **[observé]** : souvenir d'observation des écrans réels, cohérent entre plusieurs générations,
  **à confirmer sur photo** (déposer les photos dans `docs/reference/photos/`) ;
- **[choix]** : adaptation propre à notre écran 320×170.

> Limite de la recherche (2026-11-10) : les pages officielles RATP / IDFM décrivant les écrans
> (ratp.fr, iledefrance-mobilites.fr) répondent 403 aux outils automatiques, et aucune source
> écrite ne donne la charte couleur exacte. Les couleurs ci-dessous sont donc des
> approximations à recaler sur photos.

## 1. Les familles d'écrans sur les quais RER

| Génération | Exploitant | Description |
|---|---|---|
| **SIEL / afficheurs cathodiques** (1992→) | RATP (RER A, B) | Destination et heure de passage des **5 prochains trains** [sourcé]. |
| **PID** (panneaux indicateurs de desserte) | RATP | « Boîtes en métal couvertes de loupiottes » : liste des gares, un voyant s'allume en face des gares desservies par le prochain train [sourcé]. Depuis 2012, ajout de la destination et du code mission [sourcé]. |
| **IMAGE** (2014→) | RATP | Écrans LCD ; à Châtelet-les-Halles et Val de Fontenay ils affichent aussi les RER D et E (SNCF) [sourcé]. |
| **PAD / SysPad** (test 2018, RER A 2019-2020, RER B sud depuis oct. 2023) | RATP | Écran panoramique horizontal (ex. Gare de Lyon) ou totem vertical (ex. Val de Fontenay). Remplace les PID : la desserte du prochain train est placée directement sur le plan de ligne [sourcé]. ~265 panneaux sur le RER A [sourcé]. |
| **Infogare** (→ fin 2020) puis **IENA / Dauphine** | SNCF (RER C, D, E, branches SNCF de A et B, Transilien) | Écran d'accès (6 prochains départs toutes directions), écrans de passerelle, écrans de quai (prochain(s) train(s)) [sourcé]. Police **Achemine** sous Dauphine [sourcé]. Écrans de quai Transilien : **3 prochains trains, desserte détaillée du premier** [sourcé]. |

Depuis 2018, le SIEL RER affiche le **temps d'attente** du prochain train (et non plus l'heure de
passage) [sourcé]. En situation perturbée, l'heure est remplacée par la **position du train**
(« en gare de X » / « entre X et Y ») et un **bandeau jaune** donne la cause de l'incident [sourcé].

## 2. Anatomie d'un écran de quai actuel (synthèse SysPad / IENA)

### En-tête
- Pictogramme de mode **RER** (cartouche blanc arrondi, lettres « RER » foncées) suivi du
  **pictogramme de ligne** : carré à coins arrondis de la couleur de la ligne, lettre blanche
  (noire sur le RER C) [observé — charte IDFM des pictogrammes].
- Horloge **HH:MM** en haut à droite [observé].
- Nom de la gare / direction : présent selon les générations [observé, à vérifier].

### Lignes de départ (une par train)
- **Code mission** à 4 lettres (ex. `ZEUS`, `QIKI`, `NELY`) : la 1re lettre code la destination
  (initiale du terminus, avec exceptions), les 3e/4e lettres sont phonétiques sur A et B
  [sourcé]. Affiché en petit, dans un cartouche, à gauche ou au-dessus de la destination
  [observé].
- **Destination** en blanc, gros corps, casse mixte (« Saint-Germain-en-Laye »), parfois abrégée
  (« St-Germain-en-Laye ») [observé].
- **Temps d'attente** à droite, en grand, couleur chaude (jaune / ambre) : gros chiffre + « min »
  en petit [observé]. Au-delà d'un horizon (≈ 1 h), heure de passage **HH:MM** [observé / choix].
- **États** : « à quai » et « à l'approche » remplacent le temps et **clignotent** [observé,
  libellés sourcés via maligneb.fr / SIEL] ; « Retardé » (étiquette orange) ; « Supprimé »
  (rouge) ; « train court » / « train long » [sourcé : « train court »].
- **Voie** affichée sur les écrans SNCF (carré « Voie X ») [observé].
- Nombre de trains : 2 à 3 sur les écrans de quai [sourcé : 3 sur Transilien] ; 5 sur les
  anciens SIEL [sourcé].

### Bas d'écran
- **Bandeau défilant** : liste des gares desservies par le prochain train (SNCF) ou messages
  d'information trafic [observé] ; bandeau jaune de perturbation [sourcé].

### Couleurs [observé, approximations]
| Élément | Couleur retenue (RGB) |
|---|---|
| Fond | bleu nuit `#0B1F4D` |
| Fond en-tête | bleu plus sombre `#061538` |
| Ligne paire (alternance) | `#11295E` |
| Destination | blanc `#FFFFFF` |
| Temps d'attente / heure | jaune `#FFD200` |
| Texte secondaire (voie) | gris bleuté `#A8B4CC` |
| Retardé | orange `#FF9100` |
| Supprimé | rouge `#FF4040` |
| Bandeau bas | `#06112E`, étiquette « INFO » jaune |

### Typographie
- RATP : **Parisine** (police de la signalétique RATP / IDFM) ; SNCF Dauphine : **Achemine**
  [sourcé pour Achemine].
- Sur l'appareil : seule la police GLCD 5×7 de TFT_eSPI est compilée dans Bruce (voir §4).

## 3. Couleurs officielles des lignes (référentiel IDFM, champ `colourweb_hexa`)

| Ligne | Fond | Texte |
|---|---|---|
| RER A | `#E3051C` | blanc |
| RER B | `#5291CE` | blanc |
| RER C | `#FFCE00` | noir |
| RER D | `#00814F` | blanc |
| RER E | `#C04191` | blanc |

Valeurs fournies par le cahier des charges, cohérentes avec le référentiel des lignes IDFM
(jeu « Référentiel des lignes », champs `colourweb_hexa` / `textcolourweb_hexa`) ; à reconfirmer
lors de l'import du référentiel (le `Line.color` du modèle prime quand il est renseigné).

## 4. Adaptation à l'écran 320×170 [choix]

```
 0 ┌──────────────────────────────────────────────┐
   │[RER][A] Châtelet les Halles            12:34 │ en-tête 26 px
26 ├──────────────────────────────────────────────┤
   │[ZEUS] Voie 1                           3     │
   │Boissy-St-Léger                          min  │ 3 lignes × 40 px
   │[QIKI] Retardé                          12    │
   │Marne-la-Vallée Chessy                   min  │
   │[NELY]                                  à quai│ (clignote)
   │Cergy-le-Haut                                 │
148├──────────────────────────────────────────────┤
   │[INFO] Trafic perturbé entre … (défilement)   │ bandeau 22 px
170└──────────────────────────────────────────────┘
```

- **3 trains** visibles (l'encodeur peut faire défiler la liste : paramètre `scrollOffset`).
- Police GLCD : taille ×2 (12×16 px) pour la destination, ×3 pour les minutes, ×1 pour le code
  mission. Abréviations « Saint- » → « St- » si la destination déborde, puis troncature.
- **Accents** : la police GLCD de TFT_eSPI contient la table **CP437** : les minuscules accentuées
  françaises (é è ê ë à â ç î ï ô ù û ü) et É, Ç sont affichées telles quelles ; les autres
  majuscules accentuées (À, È, Ê, Î, Ô…) sont translittérées (A, E…).
- Règles de temps d'attente :
  - statut « à quai » → « à quai » clignotant ; statut « à l'approche » → « à l'approche »
    clignotant ;
  - sinon `attente = expected (ou aimed) − maintenant` : < 60 s → « <1 min » ; < 60 min →
    « N min » ; sinon heure locale HH:MM (Europe/Paris via `primLocalTime()` : Bruce ne
    règle pas `TZ`, donc pas de `localtime_r`) ;
  - départ passé depuis plus de 2 min sans statut « à quai » → ligne masquée.
- Bandeau bas : messages d'info trafic (étiquette « INFO ») ; à défaut, rappel de la ligne, de
  la gare et de l'heure de mise à jour.

## Sources
- Wikipédia, *SIEL (RER d'Île-de-France)* : https://fr.wikipedia.org/wiki/SIEL_(RER_d'%C3%8Ele-de-France)
- Wikipédia, *Infogare* : https://fr.wikipedia.org/wiki/Infogare
- Wikipédia, *Système d'information en ligne* : https://fr.wikipedia.org/wiki/Syst%C3%A8me_d'information_en_ligne
- Ma Ligne B, *L'information voyageurs mise à neuf* (2023) : https://maligneb.fr/linformation-voyageurs-mise-a-neuf/
- Ma Ligne A, *Les écrans en gare et dans nos trains* (2021) : https://malignea.fr/tout-sur-linformation-voyageurs-4-les-ecrans-en-gare-et-dans-nos-trains/
- user.io, *Les coulisses du Syspad du RER A* : https://user.io/future/syspad-rer-a-panneaux-indicateurs-direction/
- user.io, *RATP — Le Syspad du RER A* : https://user.io/work/ratp-rera-pid-syspad/
- Sortir à Paris, *Codes mission des RER* : https://www.sortiraparis.com/en/what-to-visit-in-paris/history-heritage/articles/336163-transportation-rer-meaning-mission-code
- RATP, *RER B — écrans d'information voyageurs* (403 aux outils, à consulter à la main) : https://www.ratp.fr/rer-b-ecrans-information-voyageurs
- IDFM, *RER B, métro : 30 M€ pour l'information voyageurs sur les quais* (403 aux outils) : https://www.iledefrance-mobilites.fr/actualites/rer-b-metro-30-millions-d-euros-information-voyageurs-quais
- Transilien, affluence sur les écrans de quai : https://www.transilien.com/fr/page-presse/affluence
