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

### Ce qui est déjà éliminé — mesuré, pas supposé

**`0x075` ne porte pas le temps restant.** Capture live du 2026-09-11, pendant une run à qui il restait
**1:05** : huit paquets consécutifs, tous identiques.

```
DIV 075 @04=65535 @08=779329666 @0C=1878 @10=0
```

`@0C = 1878` (31:18) ne correspond pas au temps restant, et n'a pas bougé. C'était pourtant la meilleure piste
— c'est exactement là que vit l'horloge de Sheol Gaol. Éliminée en une run.

La sonde `0x075` est **gardée mais resserrée** : elle ne journalise plus que sur un **changement** d'octets,
avec un horodatage et les secondes depuis l'entrée. Huit échantillons pris dans la même seconde ne distinguent
pas un décompte d'une constante — c'est ce qui a failli faire croire à une piste.

### Les sondes en place

| Sonde | Ce qu'elle cherche | Borne |
|---|---|---|
| `DIVTEXT mode=… \| …` | **la piste principale** : les annonces du chat (« votre séjour prendra fin dans N minutes », l'annonce d'extension). Seules les lignes contenant un chiffre | 40 lignes |
| `DIV msg id=… p1=…` | les messages d'action `0x02A` de la run — aucun n'est encore apparu | 24 ids |
| `DIV 075 …` | un champ qui **change** dans le paquet battlefield | 40 changements |

Aucune n'est à armer. Une run suffit pour lire la formulation exacte, puis on écrit le parseur et les sondes
disparaissent.

## Historique

- 2026-09-11 — signalé : la boîte Dynamis s'affichait en Divergence (granules + décompte de 60 min).
  Corrigé le jour même (`1e4d661`), puis le chrono passé en décompte à la demande.
