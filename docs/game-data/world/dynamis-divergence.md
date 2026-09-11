# Dynamis - Divergence — ce qu'on sait, ce qu'on affiche

## Identification : certaine, aucune sonde nécessaire

Divergence a ses **propres ids de zone**, dans `res/zones.lua` comme dans notre `src/model/zones.cpp` :

| Zones | Contenu |
|---|---|
| 39-42, 134, 135, 185-188 | Dynamis **original** |
| **294-297** | `Dynamis - San d'Oria [D]` … `Jeuno [D]` = **Divergence** |

`zt_is_divergence(zone)` (`party_state.h`) est la seule porte. Le test précédent était « le nom commence par
`Dyn` », vrai pour les quinze zones.

## Le temps

- **Base 60 minutes**, **plafond 120**.
- **Extensions automatiques, aucun key item** : +1 min par statue, +30 min par boss de vague, plafonné à
  30 min par vague, **vagues 1 et 2 seulement** (la vague 3 n'étend pas).
- Les cinq *granules of time* (key items **1545-1549**) du Dynamis original **ne tombent pas ici**. `on_55`
  ne les lit plus en Divergence et la rangée de points n'est plus dessinée.

Conséquence pour nous : `zt_recompute_dyn_limit` ne peut rien additionner, puisqu'il additionne des key items.
La boîte décompte donc depuis 60 min et **passe en négatif** au-delà, affiché `+M:SS` — du temps
supplémentaire, pas un `0:00` qu'on ne peut pas justifier. Un décompte figé à zéro pendant qu'il reste une
demi-heure, c'est exactement le bug du timer visitant d'Abyssea.

## Ce qui manque pour un décompte exact

La vraie limite. Deux sondes tournent en Divergence, bornées, sans rien à armer :

| Sonde | Ce qu'elle cherche | Borne |
|---|---|---|
| `DIV msg id=… p1=… p2=…` | le vocabulaire des messages `0x02A` d'une run : celui dont les params ressemblent à des **minutes** est l'annonce d'extension | 24 ids distincts |
| `DIV 075 @04=… @0C=…` | un compteur dans le paquet `0x075` — c'est là que vivait l'horloge de Sheol Gaol (`@0C`, en secondes) | 8 paquets |

Le `0x075` est la piste à privilégier : il vient du serveur et il est **insensible au renumérotage des
messages**, qui a dû être réparé deux fois en Abyssea avant qu'on lise le statut à la place.

## Historique

- 2026-09-11 — signalé : la boîte Dynamis s'affichait en Divergence (granules + décompte de 60 min).
  Corrigé le jour même (`1e4d661`), puis le chrono passé en décompte à la demande.
