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

### Le temps restant : RÉSOLU, lu dans le `0x075`

Le paquet battlefield porte bien l'horloge — la première lecture avait comparé le mauvais champ. Les noms
viennent des définitions de paquets de Windower (`addons/libs/packets/fields.lua`, incoming `0x075`) :

| Offset | Champ | Sens |
|---|---|---|
| `@04` | Fight Designation | ≠ 0 = un timer existe ; 0 le supprime |
| `@08` | Timestamp Offset | secondes depuis le **31/12/2002 15:00 GMT** = `0x3C307D70` = 1009810800 → **début** |
| `@0C` | Fight Duration | **durée** en secondes — *pas* le temps restant |

```
temps restant = (@08 + @0C) - maintenant
```

et `maintenant` est déjà à notre disposition : `ffxi_now_tick() / 60` compte dans **exactement la même
époque** (elle avait été reversée pour les timers de buffs).

**Vérification (2026-09-11, run réelle) :**

```
DIV 075 @04=65535 @08=779329666 @0C=1878
        -> début 17:27:46 + 31:18 = fin 17:59:04
```

Le joueur lisait **« il reste 1:05 »** sur l'horloge du jeu à 17:58. Concordance à la seconde.

**Ce que ça règle d'un coup :** les extensions. Le serveur renvoie un `0x075` chaque fois que la durée change
— c'est comme ça que le timer du jeu lui-même reste juste — donc notre décompte se ré-ancre tout seul, sans
connaître les règles d'extension ni un seul id de message.

**L'erreur à ne pas refaire** : la première capture a été jugée sur huit paquets identiques pris dans la même
seconde, et `@0C` comparé au temps restant. 31:18 contre 1:05 : « ça ne correspond pas, piste morte ». Les
champs avaient un nom depuis toujours, dans un fichier présent sur le disque. **Lire la définition du paquet
avant de conclure qu'un champ ne veut rien dire.**

### Sondes restantes

| Sonde | Rôle | Borne |
|---|---|---|
| `DIV clock : start+dur -> N s left` | confirme l'ancrage à chaque run | 6 lignes |
| `DIVTEXT` | le vocabulaire du chat — devenu secondaire, gardé le temps de vérifier une run extensionnée | 40 lignes |
| `DIV 075` / `DIV msg` | bruts, sur changement uniquement | 40 / 24 |

## Historique

- 2026-09-11 — signalé : la boîte Dynamis s'affichait en Divergence (granules + décompte de 60 min).
  Corrigé le jour même (`1e4d661`), puis le chrono passé en décompte à la demande.
