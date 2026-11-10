# Notes API PRIM (Île-de-France Mobilités)

- Portail : https://prim.iledefrance-mobilites.fr/
- Gratuit dans la limite des quotas ; **compte obligatoire** (onglet « Connexion »),
  puis génération d'un jeton dans l'espace personnel.
- Authentification : header HTTP `apikey: <clé>`.

## Prochains passages (requête unitaire)

```
GET https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF:StopPoint:Q:41304:
apikey: <clé>
```

Réponse SIRI Lite (JSON) : `Siri.ServiceDelivery.StopMonitoringDelivery[].MonitoredStopVisit[]`
→ `MonitoredVehicleJourney` (ligne, destination, `MonitoredCall.ExpectedDepartureTime`…).

> À vérifier sur la doc officielle avant implémentation (format des identifiants,
> quotas, éventuelle requête globale par ligne).

## Référentiels utiles
- Référentiel des lignes et arrêts (jeux de données PRIM / data.iledefrance-mobilites.fr)
  pour construire les listes mode → ligne → arrêt.
