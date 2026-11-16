# Référence visuelle — écrans d'arrêt de bus

Référence de design pour `app/idfm/board_bus.*`. Niveaux de confiance (comme `RER.md` / `METRO.md`) :

- **[sourcé]** : confirmé par une source citée en bas de page ;
- **[observé]** : souvenir d'observation des écrans réels, **à confirmer sur photo**
  (déposer les photos dans `docs/reference/photos/`) ;
- **[choix]** : adaptation propre à notre écran 320×170.

> Limite de la recherche (2026-11-16) : les pages iledefrance-mobilites.fr et
> prim.iledefrance-mobilites.fr (dont le document « Prescriptions IV circonstancielle — Volume 1
> BUS/BHNS/CAR », qui décrit précisément les BIV) répondent 403 aux outils automatiques. Aucune
> source écrite ne donne la charte graphique (couleurs, police) des écrans d'abribus. Les couleurs
> d'écran sont donc des approximations ; les couleurs de lignes sont officielles (open data IDFM).

## 1. Les générations d'afficheurs d'arrêt de bus (RATP / IDFM)

| Génération | Description |
|---|---|
| **Bornes SIEL bus** (années 2000) | Écran LCD **noir et blanc**, sur poteau ou abribus, sur batterie ou secteur (rétro-éclairé la nuit dans un abribus éclairé) [sourcé]. Affiche **le temps d'attente des deux prochains bus** et signale incidents/perturbations [sourcé]. Bus non localisé → **temps d'attente théorique** [sourcé]. |
| **BIV d'abribus** (2014 →) | ~2 000 abribus équipés de panneaux LCD « BIV », **monochromes ou couleur** (« BCC ») [sourcé]. Une rangée par ligne de l'arrêt (le nombre de rangées dépend du nombre de lignes desservies) [sourcé pour le principe, cas Montpellier ; observé à Paris]. |
| **Arrêts « Ville de Paris »** (2019 →) | ~200 arrêts avec écrans tactiles et afficheurs LED, remplaçant progressivement les SIEL [sourcé]. |
| **Papier électronique** (pilote 2025) | Une dizaine d'arrêts, écrans e-paper noir et blanc sur batterie : temps d'attente, **affluence des prochains véhicules**, perturbations [sourcé]. Renouvellement de la plupart des écrans prévu 2025–2032 [sourcé]. |
| **Autres réseaux IDFM** (ex. Centre Essonne, 2018) | Bornes « nouvelle génération » : temps d'attente des **deux prochains bus de chaque ligne** [sourcé]. |

## 2. Contenu et libellés

- **Une rangée par ligne (et par destination)**, pictogramme de ligne à gauche (rectangle à la
  couleur de la ligne, numéro dedans), destination, puis **les 2 prochains temps** [sourcé pour
  « 2 prochains bus de chaque ligne » ; observé pour la mise en page].
- Temps en minutes. Les anciens SIEL et l'API RATP historique (qui reprenait les textes des
  bornes) écrivaient **« mn »** (ex. « 59 mn ») [sourcé, chaînes de l'API RATP] ; les écrans
  récents et le site RATP écrivent **« min »** [observé]. Nous affichons « min » [choix].
- **Libellés de remplacement du temps**, tels que renvoyés par l'API RATP (= textes des BIV)
  [sourcé, table de correspondance d'un client de l'API] :
  « A l'approche », « A l'arret », « Service termine », « SERVICE NON COMMENCE »,
  « PAS DE SERVICE »/« NON ASSURE », « INFO INDISPO .... », « DERNIER PASSAGE »,
  « PREMIER PASSAGE », « BUS SUIVANT DEVIE », « DEVIATION », « ARRET NON DESSERVI »,
  « INTERROMPU », « MANIFESTATION », « INTEMPERIES », « CIRCULATION DENSE », « BUS PERTURBE ».
- Messages de perturbation en clair sur l'écran [sourcé].
- IDFM liste pour ses écrans : temps d'attente, perturbation, arrêt non desservi, bus de
  substitution [sourcé].

## 3. Couleurs officielles des lignes de la démo (open data IDFM, `referentiel-des-lignes`)

Relevé le 2026-11-16 (`colourweb_hexa` / `textcolourweb_hexa`, `transportmode = bus`) [sourcé] :

| Ligne | id | Fond | Texte |
|---|---|---|---|
| 38 | C01083 | `#0055C8` | blanc |
| 67 | C01103 | `#FF82B4` | noir |
| 69 | C01105 | `#82C8E6` | noir |
| 72 | C01107 | `#FF1400` | blanc |
| 96 | C01127 | `#DC9600` | noir |
| N11 | C01415 | `#FF5A00` | blanc |

Les lignes 47 et 75 n'apparaissent pas comme lignes RATP dans le référentiel actuel (les
résultats « 47 »/« 75 » sont d'autres réseaux) : dans la démo, 47 est **absente** de
`BoardData.lines` (repli sur `lineName`) et 75 a des couleurs 0/0 (repli neutre) — exprès, pour
tester les deux replis. Les lignes TVM, N151, 391, 192 de la scène 2 ont des couleurs
**inventées** (scène de test de mise en page uniquement).

## 4. Adaptation à l'écran 320×170 [choix sauf mention]

```
 0 ┌──────────────────────────────────────────────┐
   │[BUS] Hôtel de Ville                    14:05 │ en-tête 30 px (« : » clignote)
   │      Direction Parc de Saint-Cloud           │
30 ├──────────────────────────────────────────────┤
   │[38 ] Porte d'Orléans          3min     9min  │ 4 rangées de 29 px
   │[47 ] Fort du Kremlin-Bicêtre  7min    19min  │ (barre de défilement
   │[67 ] Porte de Gentilly      à l'arrêt 14min  │  à droite si > 4)
   │[69 ] Champ de Mars            2min  Supprimé │
148├──────────────────────────────────────────────┤
   │[INFO] Ligne 72 : arrêt Pont Neuf … (défile)  │ bandeau 21 px
170└──────────────────────────────────────────────┘
```

- **En-tête** : carré blanc « BUS » (tient lieu de pictogramme de mode bus IDFM) [choix],
  nom de l'arrêt en ×2 (×1 sur deux lignes si trop long), sous-titre gris « Direction … » = côté
  de rue choisi (`stop.direction`) quand il existe ; heure HH:MM à droite, « : » clignotant
  (comme les écrans métro) [choix].
- **Regroupement** : une rangée par couple **ligne + destination** (une ligne qui a deux
  terminus, ex. 72 « Parc de Saint-Cloud » / « Porte de Saint-Cloud », occupe deux rangées)
  avec les **2 prochains passages** du groupe [sourcé pour les 2 temps ; choix pour le
  regroupement par destination]. Passages au-delà du 2e : non affichés (comme les BIV).
- **Tri** : par numéro de ligne en ordre naturel (numéros d'abord, 38 < 72 < 391, puis
  préfixes alphabétiques ?, N11 < N151, TVM), puis prochain passage le plus proche [choix,
  cohérent avec les BIV qui listent les lignes dans l'ordre].
- **Pictogramme de ligne** : rectangle arrondi 40×21 à la couleur IDFM, numéro centré en ×2
  (« N11 », « 391 » tiennent), ×1 au-delà (« N151 », « TVM » si besoin) [sourcé pour la charte
  rectangle/couleur ; observé]. Ligne inconnue du référentiel : `lineName` PRIM (« ? » s'il est
  vide) sur fond gris clair neutre.
- **Destination** : blanc ; ×2 seulement si **toutes** les destinations de la page tiennent en ×2
  (typographie homogène), sinon ×1 sur une ligne, puis sur deux lignes (coupure après espace ou
  tiret), puis abréviation « St- »/troncature. Grisée si tous les passages sont supprimés.
- **Temps** (ambre `#FFB400`, deux colonnes alignées de 50 px) :
  - `N` en ×2 + « min » en ×1 sur la même ligne de base ;
  - < 1 min → « à l'/approche » (le SIEL bus remplace « 0 mn » par « A l'approche »)
    [sourcé pour le libellé, observé pour la règle] ; statut PRIM « à l'approche » idem ;
  - statut « à l'arrêt » → « à/l'arrêt » [sourcé pour le libellé] ;
  - ≥ 60 min → heure HH:MM en ×1 (Europe/Paris via `primLocalTime()`) [choix] ;
  - supprimé → « Supprimé » en rouge [choix] ; temps inconnu → « -- » [choix, comme le métro] ;
  - un seul passage dans le groupe → affiché dans la 2e colonne (bord droit) ;
  - pas de distinction temps réel / théorique (les BIV ne la font pas visiblement) [sourcé :
    affichage du théorique quand le bus n'est pas localisé].
- **Lignes sans passage** : une ligne connue de l'arrêt (`BoardData.lines`, ou la ligne
  choisie) sans aucun passage garde sa rangée avec un libellé gris à la place des temps
  [sourcé pour les libellés, choix pour les heures] :
  - lignes de jour : « Service terminé » de 20:30 à 05:30, « Pas de service » sinon ;
  - Noctilien (N + chiffre) : « Service terminé » de 05:30 à 12:00, « Service non commencé »
    de 12:00 à 20:30, « Pas de service » la nuit.
- **États globaux** : « Chargement… » avant le premier appel ; erreur sans données →
  « Information indisponible » + détail (libellé « INFO INDISPO » des BIV [sourcé]) ; erreur avec
  données → rangées conservées + « Informations non actualisées » dans le bandeau ; aucune
  rangée → « Service terminé » de 01:30 à 05:30, sinon « Aucun bus annoncé ».
- **Bandeau bas** : messages trafic avec étiquette jaune « INFO » ; à défaut
  « Bus • arrêt • Mis à jour à HH:MM » (défile s'il dépasse).
- **Défilement (encodeur)** : `scrollOffset` = indice de la **première rangée** (groupe) affichée,
  borné à `rangées − 4`. L'app borne son compteur avec `departures.size()` (≥ nombre de
  rangées) : au bout de la liste, des crans supplémentaires sont sans effet visible (et autant de
  crans en arrière sont « absorbés »). Barre de défilement de 2 px à droite quand > 4 rangées.

### Couleurs d'écran retenues [choix, approximations ; même famille que le métro]
| Élément | Couleur |
|---|---|
| Fond | quasi-noir `#050812` |
| En-tête, bandeau bas | bleu nuit `#0E1630` |
| Séparateurs | `#2A365C` (rangées `#1A223C`) |
| Arrêt, destinations, heure | blanc |
| Libellés secondaires, états de ligne | gris bleuté `#9AA4BC` |
| Temps d'attente | ambre `#FFB400` |
| Supprimé | rouge `#FF4040` |
| Étiquette INFO | jaune `#FFD200` |
| Pictogramme de ligne inconnue | gris clair `#C8CDD7`, texte bleu nuit |

## 5. Points ouverts
- Photos des BIV couleur (2014) et des nouveaux écrans pour confirmer mise en page, couleurs,
  police, « min » vs « mn », et si le terminus est affiché à chaque rangée.
- Règle exacte « 0 mn » → « A l'approche » et seuil d'affichage de l'heure au lieu des minutes.
- Libellés fins (« DERNIER PASSAGE », « PREMIER PASSAGE », « DEVIATION », « ARRET NON DESSERVI »)
  non affichés : pas de champ PRIM correspondant dans notre modèle (`Departure`).
- Les heures de « Service terminé » / « non commencé » sont des heuristiques (pas d'horaires de
  service par ligne dans le modèle).
- Affluence (pilote e-paper 2025) non représentée.

## Sources
- techno-science.net, *SIEL (bus)* : https://www.techno-science.net/fr/definitions/siel-bus
- Wikipédia, *SIEL (bus)* : https://fr.wikipedia.org/wiki/SIEL_(bus)
- Transbus, *L'information dynamique des voyageurs* : https://www.transbus.org/dossiers/usagers.html
- Transbus, *SAEIV* : https://www.transbus.org/dossiers/saeiv.html
- Chaînes de l'API RATP (textes des bornes) dans un client open source :
  https://gist.github.com/aymanebarka/06afca0f52544f3e42a04675bbfd335a
- RATP, *Vous prenez le bus ?* : https://www.ratp.fr/visite-paris/francais/guide-voyageur/bus
- IDFM, *Des nouveautés pour améliorer l'information voyageurs* (403 aux outils) :
  https://www.iledefrance-mobilites.fr/actualites/modernisation-information-voyageurs-transport-ile-de-france
- IDFM, CP *Information en temps réel sur le réseau de bus Centre Essonne* (403 aux outils, résumé
  du moteur de recherche) : https://www.iledefrance-mobilites.fr/medias/portail-idfm/2fdd5bcc-e40a-4b0c-b4fd-cd2faaddf413_CP-Une-information-en-temps-r%C3%A9el-sur-le-r%C3%A9seau-de-bus-Centre-Essonne.pdf
- IDFM / PRIM, *Prescriptions IV circonstancielle Vol. 1 BUS/BHNS/CAR* (403, à lire à la main) :
  https://prim.iledefrance-mobilites.fr/content/files/2023/12/IDFM_Prescriptions_IV_Circonstancielle_Volume1_BUS_BHNS_CAR_V1.0.pdf
- IDFM open data, référentiel des lignes : https://data.iledefrance-mobilites.fr/explore/dataset/referentiel-des-lignes/
