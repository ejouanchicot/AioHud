# Regen — Durée des sorts

Ce document regroupe **uniquement** la partie *durée* (duration) des sorts de Regen : durées de base, effet des différents modificateurs, formule de calcul et équipements qui allongent la durée.

> Rappel de tick : un « tick » de Regen se déclenche toutes les **3 secondes**. Donc `durée en ticks = durée en secondes / 3`.

---

## 1. Durées de base

| Sort       | Durée de base | Ticks | HP/tick | Total HP |
|------------|---------------|-------|---------|----------|
| Regen      | 75 s          | 25    | 5       | 125      |
| Regen II   | 60 s          | 20    | 12      | 240      |
| Regen III  | 60 s          | 20    | 20      | 400      |
| Regen IV   | 60 s          | 20    | 30      | 600      |
| Regen V    | 60 s          | 20    | 40      | 800      |

À noter : **Regen (I) dure plus longtemps** que les versions supérieures (75 s au lieu de 60 s). C'est la durée de base « nue », sans aucun bonus ni équipement.

---

## 2. Modificateurs de durée par job / abilities

### Scholar — Light Arts (SCH main job uniquement)
Sous **Light Arts**, la durée reçoit un bonus dépendant du niveau. Aux niveaux 90-99, cela donne :

| Sort       | Durée de base | Durée sous Light Arts (Nv 90-99) |
|------------|---------------|----------------------------------|
| Regen      | 75 s          | 123 s                            |
| Regen II   | 60 s          | 108 s                            |
| Regen III  | 60 s          | 108 s                            |
| Regen IV   | 60 s          | 108 s                            |
| Regen V    | 60 s          | 108 s                            |

> Light Arts n'agit **que** si Scholar est le job principal. En sub job (/SCH), aucun bonus de durée Light Arts.

### Scholar — Tabula Rasa
Tabula Rasa **écrase** l'effet de Light Arts. Il ajoute un forfait fixe **quel que soit le niveau** :
- **+72 secondes** de durée (et +36 HP/tick).

À partir du niveau 90+, Tabula Rasa fait donc **+48 secondes de plus** que Light Arts seul (72 s vs 24 s de bonus). Les deux ne se cumulent pas : Tabula Rasa remplace Light Arts.

### Scholar — Perpetuance
Perpetuance augmente la durée des enhancing magic (dont Regen). Se cumule avec Light Arts / Tabula Rasa. Voir la formule d'Enhancing Magic Duration.

### Composure (RDM)
Composure augmente la durée des enhancing magic **lancées sur soi-même**. Le multiplicateur de durée s'applique donc au Regen que le lanceur se met à lui-même. *(C'est un modificateur d'Enhancing Magic Duration classique — voir §3.)*

### Job Points (durée)
| Job Point            | Job | Effet sur la durée                                              |
|----------------------|-----|-----------------------------------------------------------------|
| Scholar Job Points   | SCH | Regen +3 s / niveau **sous Light Arts** (appliqué avant Perpetuance) |
| White Mage Job Points| WHM | Regen +3 s / niveau                                             |

---

## 3. Formule de durée

La durée finale se calcule en deux étapes :

```
Durée finale = floor( Durée_de_base × (1 + total Enhancing Magic Duration %) )
               + additions forfaitaires « Regen duration » (en secondes)
```

**Étape 1 — multiplicateur (%)** : tous les bonus « Enhancing Magic Duration » sont **additifs entre eux**, puis on multiplie la durée de base. Comptent ici : Light Arts, Tabula Rasa, Perpetuance, Composure (self), et les équipements à `%` de durée (Estoqueur's Houseaux +2, Telchine augmenté « Enhancing duration », etc.).

**Étape 2 — additions fixes** : les équipements/traits qui donnent une durée en **secondes ou en ticks** spécifiques au Regen s'ajoutent *après* le multiplicateur (Orison Mitts, Ebers Mitts, Telchine Chas., Theophany Pantaloons, Coeus, Lugh's Cape, Job Points…).

> ⚠️ La formule d'HP/tick (potency) est **séparée** de celle de la durée. Un item peut booster la potency sans toucher la durée, et inversement. Ce document ne traite que la durée.

Pour le détail complet du multiplicateur, se référer à la page **Enhancing Magic Duration**.

---

## 4. Équipements qui augmentent la DURÉE de Regen

Ces pièces donnent une durée **forfaitaire en secondes / ticks** (Étape 2 de la formule). Rappel : 1 tick = 3 s.

| Équipement                   | Jobs                                             | Bonus de durée Regen        |
|------------------------------|--------------------------------------------------|------------------------------|
| Orison Mitts +1              | WHM                                              | +9 s (3 ticks)               |
| Orison Mitts +2              | WHM                                              | +18 s (6 ticks)              |
| Ebers Mitts                  | WHM                                              | +20 s (6~7 ticks)            |
| Ebers Mitts +1               | WHM                                              | +22 s (7 ticks)              |
| Ebers Mitts +2               | WHM                                              | +24 s (8 ticks)              |
| Ebers Mitts +3               | WHM                                              | +26 s (8~9 ticks)            |
| Runeist Bandeau              | RUN                                              | +20 s (6~7 ticks)            |
| Runeist Bandeau +1           | RUN                                              | +21 s (7 ticks)              |
| Rune. Bandeau +2             | RUN                                              | +24 s (8 ticks)              |
| Rune. Bandeau +3             | RUN                                              | +27 s (9 ticks)              |
| Telchine Chas.               | WHM/BLM/RDM/BRD/SMN/BLU/SCH/GEO                   | +12 s (4 ticks)              |
| Theophany Pantaloons / +1    | WHM                                              | +18 s (6 ticks)              |
| Theophany Pantaloons +2      | WHM                                              | +21 s (7 ticks)              |
| Theophany Pantaloons +3      | WHM                                              | +24 s (8 ticks)              |
| Coeus                        | SCH                                              | +12 s (4 ticks)              |
| Lugh's Cape                  | SCH                                              | +15 s (5 ticks)              |

> **Note « Orison Mitts specifically increase Regen spell duration »** : certaines pièces (Orison/Ebers Mitts, Theophany, Runeist Bandeau…) allongent **spécifiquement** la durée du Regen, en plus des bonus généraux d'Enhancing Magic Duration.

### Pièces à durée « générale » (Enhancing Magic Duration %)
Elles agissent via le multiplicateur (Étape 1) et touchent **tous** les enhancing magic, Regen inclus :
- **Estoqueur's Houseaux +2** — Enhancing magic duration
- **Telchine Armor** (augmenté Enhancing duration) — attention : partage le slot avec « +1~3 Regen potency »
- **Composure** (RDM, sur soi), **Perpetuance** (SCH)

---

## 5. Exemple de lecture rapide

**Regen II, base = 60 s.**

- Nu (aucun bonus) : **60 s** (20 ticks).
- SCH main + Light Arts (Nv 90-99) : **108 s** (36 ticks).
- SCH main + Tabula Rasa : 60 s + 72 s = **132 s** (44 ticks).
- + une pièce de durée forfaitaire (ex. Telchine Chas. +12 s) : s'ajoute **après** le multiplicateur.

---

*Document centré sur la durée uniquement. La partie potency (HP/tick) et les équipements de potency n'y figurent pas volontairement.*
