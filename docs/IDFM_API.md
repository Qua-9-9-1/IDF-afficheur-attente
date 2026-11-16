# Notes API PRIM (Île-de-France Mobilités)

- Portail : https://prim.iledefrance-mobilites.fr/
- Gratuit dans la limite des quotas ; **compte obligatoire**, puis génération d'un jeton
  dans l'espace personnel.
- Authentification : header HTTP `apikey: <clé>`.
- Implémentation : `app/idfm/prim_client.{h,cpp}`.

> Vérification (nov. 2026) : les pages de documentation PRIM renvoient 403 aux robots ;
> les informations ci-dessous viennent des extraits indexés de la doc officielle
> (pages « Identification des objets », « Structure des requêtes », « Exemple de réponses
> niveau quai ») et de clients open source qui consomment l'API. Les points marqués
> **(à confirmer)** n'ont pas pu être vus sur la doc officielle elle-même.

## Prochains passages — requête unitaire (`stop-monitoring`)

```
GET https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF:StopArea:SP:474151:&LineRef=STIF:Line::C01742:
apikey: <clé>
Accept: application/json
```

### Paramètres
| Paramètre | Obligatoire | Format |
|---|---|---|
| `MonitoringRef` | oui | `STIF:StopPoint:Q:<id ZdE>:` (quai) ou `STIF:StopArea:SP:<id ZdA>:` (zone d'arrêt) |
| `LineRef` | non | `STIF:Line::<id ligne>:` ex. `STIF:Line::C01742:` |

- **Depuis le 13 mars 2025, les passages des trains SNCF (RER B/C/D/E, Transilien) ne sont
  fournis qu'à la zone d'arrêt** (`STIF:StopArea:SP:<ZdAid>:`) → pour les RER, toujours
  utiliser la zone d'arrêt dans le référentiel.
- Une zone d'arrêt multi-lignes renvoie toutes les lignes : il faut filtrer sur `LineRef`.
  Certaines paires MonitoringRef/LineRef inconnues sont rejetées par l'API : le client
  envoie `LineRef` (réponse plus petite), puis **réessaie sans `LineRef`** sur 400/404,
  et filtre de toute façon côté client.
- L'ancien format `IDFM:xxxxx` (référentiel `monomodalStopPlace`) se convertit en
  `STIF:StopArea:SP:xxxxx:`.

### Réponse (SIRI Lite JSON)
Les textes sont « enveloppés » : `{"value": "..."}` ou `[{"value": "..."}]`. Les horaires
sont en ISO-8601 UTC (`2026-11-10T07:42:00.000Z`).

```
Siri.ServiceDelivery.ResponseTimestamp                     (UTC, sert d'horloge de secours)
Siri.ServiceDelivery.StopMonitoringDelivery[]
  .ErrorCondition                                          (présent en cas d'erreur SIRI)
  .MonitoredStopVisit[].MonitoredVehicleJourney
    .LineRef.value                 "STIF:Line::C01742:"
    .DestinationName[0].value      terminus
    .JourneyNote[0].value          code mission RER (4 lettres, ex. "ZEUS")
    .TrainNumbers.TrainNumberRef[0].value   numéro de train
    .VehicleJourneyName[0].value   (repli pour le numéro de train)
    .MonitoredCall
      .ExpectedDepartureTime / .AimedDepartureTime   (absents pour un train terminus)
      .ExpectedArrivalTime / .AimedArrivalTime        (repli)
      .DepartureStatus / .ArrivalStatus  onTime | early | delayed | cancelled | missed |
                                         notExpected | departed | arrived | noReport
      .VehicleAtStop                 booléen → « À quai »
      .DeparturePlatformName.value / .ArrivalPlatformName.value   voie/quai
      .DestinationDisplay[0].value   (repli pour le terminus)
```

Remarques :
- Métro et la plupart des bus n'ont pas d'horaire théorique (`Aimed*`) : pas de calcul de
  retard possible, seulement l'heure estimée.
- Correspondance des statuts dans le client : `cancelled`/`missed`/`notExpected` →
  Supprimé ; `VehicleAtStop` → À quai ; départ dans ≤ 60 s → À l'approche ;
  `delayed` ou estimé − théorique ≥ 60 s → Retardé ; sinon À l'heure.
- Le flux JSON est parsé avec un filtre ArduinoJson (seuls les champs ci-dessus sont
  gardés), en HTTP/1.0 pour éviter le chunked encoding.

## Messages affichés sur les écrans (`general-message`)

```
GET https://prim.iledefrance-mobilites.fr/marketplace/general-message?LineRef=STIF:Line::C01742:
```
- Paramètre `LineRef` **ou** `StopPointRef`, **jamais les deux** (`LineRef=ALL` = tout le réseau).
- Chemin lu par le client **(à confirmer sur une vraie réponse)** :
  `Siri.ServiceDelivery.GeneralMessageDelivery[].InfoMessage[].Content.Message[]`
  → `MessageType` (`shortMessage`, …) et `MessageText.value`.
- Alternative plus riche (non utilisée) : API « Messages Info Trafic » (`disruptions_bulk`).

## Quotas et erreurs
- Comptes créés **avant le 13 mars 2024** : 100 req/s et 1 000 000 req/jour sur la requête
  unitaire. **Nouveaux comptes : quotas plus bas**, non publiés sur les pages publiques ;
  des clients tiers indiquent **~1 000 requêtes/jour par API** (compteur glissant 24 h)
  **(à confirmer dans l'espace « Ma consommation » du compte)**.
  → avec 1 000 req/jour, un rafraîchissement à 30 s épuise le quota en ~8 h :
  préférer `refreshSeconds` ≥ 90 pour un usage toute la journée.
- `429` : quota épuisé (détails dans les en-têtes, `Retry-After` possible) — ne pas
  insister avant la fin de la fenêtre.
- `401`/`403` : clé absente ou invalide.
- Erreur SIRI (`ErrorCondition`) avec HTTP 200 : identifiant d'arrêt/ligne inconnu.

## Identifiants des lignes RER (`STIF:Line::<id>:`)
| Ligne | id | Source |
|---|---|---|
| A | `C01742` | confirmé (URLs RATP `ITIV_IDFM_C01742_…`) |
| B | `C01743` | confirmé (exemple doc PRIM) |
| C | `C01727` | confirmé (exemple doc PRIM) |
| D | `C01728` | **(à confirmer via le référentiel des lignes)** |
| E | `C01729` | **(à confirmer via le référentiel des lignes)** |

## Heure
Bruce écrit l'heure **locale** dans l'horloge système (epoch décalé de `tmz`/`dst`) et ne
définit pas `TZ` : appeler `configTime()`/`setenv("TZ")` casserait l'horloge de la barre
d'état. Le client garde donc sa propre référence UTC (NTP privé `pool.ntp.org` sur un port
UDP dédié, sinon `ResponseTimestamp`/en-tête `Date` de PRIM) et convertit en heure de Paris
avec les règles UE (CET/CEST) sans toucher à l'horloge système.

## Référentiels utiles
- Référentiel des lignes et arrêts (jeux de données PRIM / data.iledefrance-mobilites.fr)
  pour construire les listes mode → ligne → arrêt (jeu « arrêts et lignes associées »).
