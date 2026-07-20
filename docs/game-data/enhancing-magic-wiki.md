# Enhancing Magic

Catégorie de sorts « buff » à effet persistant, plus un sous-ensemble de téléportation.

## Sous-catégories

Barspells · Boost & Gain · Enspells · Protect/Shell · Regen · Spikes · Storm · Teleport · Warp/Warp II/Retrace — plus divers isolés.

Majoritairement **White Magic**. Exceptions : les sorts de relocation et les Spikes obtenus par BLM sont classés **Black Magic**.

---

## Ce que fait le skill Enhancing Magic

Le skill détermine **potency** et **taux d'interruption**. Il **n'augmente jamais la durée**.

- La plupart des sorts affectés cappent à **500 skill**.
- **Sans cap connu** : Temper, Temper II, Enspells.
- **Phalanx** : cap à 500.
- **Stoneskin** : potency = f(skill, MND). Avec 0 MND il faudrait 540 skill pour capper.
- **Ne bénéficient pas du skill** : Protect, Shell (et d'autres).

### Caps de skill par job

| Job | Grade | Lv 49 | Lv 99 |
|---|---|---|---|
| RDM | B+ | 144 | 404 |
| SCH | D / **B+*** | 133 / 144* | 334 / **404*** |
| RUN | B- | 144 | 388 |
| WHM | C+ | 139 | 378 |
| PLD | D | 133 | 334 |
| BLM | E | 124 | 300 |

\* SCH obtient B+ **uniquement sous Light Arts**.

---

## Durée — formule complète

```
Durée = ( Base
        + 6s × RDM Group 2 Merit Level
        + 3s × RDM Relic Hands G2 Merit augment
        + RDM Job Points
        + Gear listant des SECONDES )
      × (Augments Composure Bonus)      ← set bonus Empyrean
      × (Duration listed on Gear + Naturalist's Roll)
      × (Duration Augments on Gear)     ← terme SÉPARÉ
      × (Rune Fencer Gifts)
      × (Perpetuance)
```

Les termes non applicables sont **retirés**, pas multipliés par 0.

### Le point qui compte

`Duration listed on Gear` (ligne d'item fixe, ex. Ammurapi Shield) et `Duration Augments on Gear` (augment, ex. Telchine +10%) sont **deux termes distincts**, appliqués à des étapes différentes → ils se multiplient entre eux au lieu de s'additionner. Ne pas les confondre.

**Le flat en secondes est le levier le plus fort** : il entre avant tous les multiplicateurs.

### Gear listant des secondes (exemples)

| Item | Effet |
|---|---|
| Grapevine Cape | Refresh +30 s |
| Gishdubar Sash | Refresh +20 s *reçu* (testé self-cast uniquement) |
| Telchine Chasuble | Regen +12 s |

### Composure (RDM)

- Self-cast : durée **×3**, plafonnée à **30 min**. Si le sort dépasse déjà 30 min sans Composure, l'ability ne fait rien et la durée native, plus longue, est conservée.
- Le set bonus (Estoqueur's +2 / Lethargy / +1 / +2 / +3) étend Composure aux buffs lancés **sur les autres** :

| Pièces | Bonus |
|---|---|
| 2 | +10 % |
| 3 | +20 % |
| 4 | +35 % |
| 5 | +50 % |

### Perpetuance (SCH)

Double la durée du **prochain** enhancing lancé. Amplifié par :

| Item | Multiplicateur |
|---|---|
| — | ×2.00 |
| Savant's Bracers +1 | ×2.25 |
| Savant's Bracers +2 / Arbatel Bracers | ×2.50 |
| Arbatel Bracers +1 | ×2.55 |
| Arbatel Bracers +2 | ×2.60 |

### Exemple chiffré — SCH Regen V

Regen V (60 s base), Light Arts JP 20/20 (+60 s) + JA active (+48 s pour SCH 99 main en Light Arts), Lugh's Cape + Telchine Chasuble (+27 s), Embla Sash + Ammurapi Shield (+20 % listed), 3× Telchine augmenté (+30 % augments), Perpetuance avec Arbatel Bracers +1 (×2.55) :

```
(60 + 60 + 27 + 48) × 1.2 × 1.3 × 2.55 = 775.72 s  ≈  12:56
```

### Piège — cible sous-niveau

Si la cible est d'un niveau **inférieur** au level minimum d'apprentissage du sort, la durée est réduite proportionnellement à l'écart. Haste sur un level 5 ne dure que quelques secondes.

---

## AoE / Accession (SCH)

Les enhancing **white magic mono-cible** obtenus par SCH — ou par sub-job RDM/WHM — peuvent être passés en AoE via **Accession** (Light Arts).

**Exceptions : Haste et Flurry** ne peuvent pas.

Perpetuance s'applique à *tous* les enhancing white magic conférant un buff avec durée, **Protect et Shell inclus** désormais.

---

## Bonus d'équipement — 3 catégories

1. **Enhancing magic skill** — additif direct au skill.
2. **Enhancing magic casting time -x%** — réduit le cast de tous les enhancing ; stacke avec Light Arts et Fast Cast.
3. **Enhancing magic duration +x%** — voir formule (deux termes distincts : listed vs augment).

### Casting Time Reduction

| Item | Slot | Jobs | Bonus |
|---|---|---|---|
| Siegel Sash | Waist | MNK WHM BLM RDM PLD BRD RNG SMN BLU PUP SCH GEO RUN | 8 % |
| Futhark Trousers | Legs | RUN | 12 % |
| Futhark Trousers +1 | Legs | RUN | 13 % |
| Futhark Trousers +2 | Legs | RUN | 14 % |
| Futhark Trousers +3 | Legs | RUN | 15 % |

### Duration **listed** (terme non-augment)

| Item | Slot | Jobs | Bonus |
|---|---|---|---|
| **Sroda Necklace** | Neck | WHM RDM | **-50 %** ⚠ |
| Ammurapi Shield | Shield | WHM BLM RDM BRD SMN SCH GEO | 10 % |
| Oranyan | Staff | WHM BLM RDM BRD SMN SCH GEO | 10 % |
| Embla Sash | Waist | WHM BLM RDM BRD SMN SCH GEO | 10 % |
| Atrophy Gloves | Hands | RDM | 15 % |
| Atrophy Gloves +1 | Hands | RDM | 16 % |
| Atrophy Gloves +2 | Hands | RDM | 18 % |
| Atrophy Gloves +3 | Hands | RDM | 20 % |
| Estoqueur's Cape | Back | RDM | 10 % |
| Estoqueur's Houseaux +1 | Feet | RDM | 10 % |
| Estoqueur's Houseaux +2 | Feet | RDM | 20 % |
| **Lethargy Houseaux** | Feet | RDM | 25 % |
| **Lethargy Houseaux +1** | Feet | RDM | 30 % |
| **Lethargy Houseaux +2** | Feet | RDM | 35 % |
| **Lethargy Houseaux +3** | Feet | RDM | 40 % |
| Lethargy Earring | Ear | RDM | 7 % |
| Lethargy Earring +1 | Ear | RDM | 8 % |
| Lethargy Earring +2 | Ear | RDM | 9 % |
| Sucellos's Cape | Back | RDM | 20 % |
| Vitiation Tabard +2 | Body | RDM | 10 % |
| Vitiation Tabard +3 | Body | RDM | 15 % |
| Erilaz Galea | Head | RUN | 10 % |
| Erilaz Galea +1 | Head | RUN | 15 % |
| Erilaz Galea +2 | Head | RUN | 20 % |
| Erilaz Galea +3 | Head | RUN | 25 % |
| Futhark Trousers | Legs | RUN | 10 % |
| Futhark Trousers +1 | Legs | RUN | 20 % |
| Futhark Trousers +2 | Legs | RUN | 25 % |
| Futhark Trousers +3 | Legs | RUN | 30 % |
| Regal Gauntlets | Hands | PLD RUN | 20 % |
| Dynasty Mitts | Hands | WHM | 5 % |
| Theo. Duckbills +2 | Feet | WHM | 5 % |
| Theo. Duckbills +3 | Feet | WHM | 10 % |
| Shabti Cuirass | Body | WAR DRK PLD | 9 % |
| Shabti Cuirass +1 | Body | WAR DRK PLD | 10 % |
| Pedagogy Gown +2 | Body | SCH | 8 % |
| Pedagogy Gown +3 | Body | SCH | 12 % |
| Argute Staff | Staff | SCH | 10 % |
| Pedagogy Staff | Staff | SCH | 15 % |
| Musa | Staff | SCH | 20 % |

> ⚠ **Sroda Necklace : -50 %.** C'est un malus, pas un bonus — l'item échange la durée contre autre chose. À exclure de tout set de buffing.

### Duration **augments** (terme séparé)

| Item | Slot | Jobs | Bonus |
|---|---|---|---|
| Telchine Cap / Chasuble / Gloves / Braconi / Pigaches | Head/Body/Hands/Legs/Feet | WHM BLM RDM BRD SMN BLU SCH GEO | 1–10 % chacun |
| Ghostfyre Cape | Back | RDM | 10–20 % |
| Duelist's Torque | Neck | RDM | 1–15 % |
| Duelist's Torque +1 | Neck | RDM | 1–20 % |
| Duelist's Torque +2 | Neck | RDM | 1–25 % |
| Grioavolr | Staff | WHM BLM RDM BRD SMN SCH GEO | 1–10 % |
| Gada | Club | WHM BLM SMN SCH GEO | 1–6 % |
| Colada | Sword | RDM PLD BLU | 1–4 % |

---

## Liste des sorts par job

Level d'obtention. `99 (JP)` = débloqué par Gift Job Points.

### Enspells (RDM exclusif)

| Sort | RDM |
|---|---|
| Enthunder | 16 |
| Enstone | 18 |
| Enaero | 20 |
| Enblizzard | 22 |
| Enfire | 24 |
| Enwater | 27 |
| Enthunder II | 50 |
| Enstone II | 52 |
| Enaero II | 54 |
| Enblizzard II | 56 |
| Enfire II | 58 |
| Enwater II | 60 |

### Gain / Boost

| Stat | RDM (Gain-) | WHM (Boost-) |
|---|---|---|
| VIT | 81 | 81 |
| MND | 84 | 84 |
| CHR | 87 | 87 |
| AGI | 90 | 90 |
| STR | 93 | 93 |
| INT | 96 | 96 |
| DEX | 99 | 99 |

### Protect / Shell

| Sort | PLD | RDM | RUN | SCH | WHM |
|---|---|---|---|---|---|
| Protect | 10 | 7 | 20 | 10 | 7 |
| Protect II | 30 | 27 | 40 | 30 | 27 |
| Protect III | 50 | 47 | 60 | 50 | 47 |
| Protect IV | 70 | 63 | 80 | 70 | 63 |
| Protect V | 90 | 77 | — | 80 | 76 |
| Shell | 20 | 17 | 10 | 20 | 17 |
| Shell II | 30 | 27 | 30 | 30 | 27 |
| Shell III | 60 | 57 | 50 | 60 | 57 |
| Shell IV | 80 | 68 | 70 | 71 | 68 |
| Shell V | — | 87 | 90 | 90 | 76 |

WHM exclusif : Protectra I–V (7/27/47/63/75), Shellra I–V (17/37/57/68/75).

### Regen

| Sort | RDM | RUN | SCH | WHM |
|---|---|---|---|---|
| Regen | 21 | 23 | 18 | 21 |
| Regen II | 76 | 48 | 32 | 44 |
| Regen III | — | 70 | 59 | 66 |
| Regen IV | — | 99 | 79 | 86 |
| Regen V | — | — | 99 | — |

### Refresh / Haste / Flurry / Temper

| Sort | RDM | RUN | WHM |
|---|---|---|---|
| Refresh | 41 | 62 | — |
| Refresh II | 82 | — | — |
| Refresh III | 99 (JP) | — | — |
| Haste | 48 | — | 40 |
| Haste II | 96 | — | — |
| Flurry | 48 | — | — |
| Flurry II | 96 | — | — |
| Temper | 95 | — | — |
| Temper II | 99 (JP) | — | — |

`Temper` : SCH l'obtient à 99 (JP).

### Spikes

| Sort | BLM | PLD | RDM | RUN | SCH |
|---|---|---|---|---|---|
| Blaze Spikes | 10 | — | 20 | 45 | 30 |
| Ice Spikes | 20 | — | 40 | 65 | 50 |
| Shock Spikes | 30 | — | 60 | 85 | 70 |

Spikes obtenus par BLM = classés Black Magic.

### Barspells (RDM / WHM)

RDM apprend les **Bar-<élément>** (mono-cible), WHM les **Bar-<élément>ra** (AoE).

| Élément | RDM | WHM (-ra) |
|---|---|---|
| Stone | 5 | 5 |
| Sleep | 7 | 7 |
| Water | 9 | 9 |
| Poison | 10 | 10 (SCH 17) |
| Paralyze | 12 | 12 |
| Aero | 13 | 13 |
| Fire | 17 | 17 |
| Blind | 18 | 18 |
| Blizzard | 21 | 21 |
| Silence | 23 | 23 |
| Thunder | 25 | 25 |
| Virus | 39 | 39 |
| Petrify | 43 | 43 |
| Amnesia | 78 | 78 |

### Storms (SCH)

| Storm | SCH | II |
|---|---|---|
| Sandstorm | 41 | 99 (JP) |
| Rainstorm | 42 | 99 (JP) |
| Windstorm | 43 | 99 (JP) |
| Firestorm | 44 | 99 (JP) |
| Hailstorm | 45 | 99 (JP) |
| Thunderstorm | 46 | 99 (JP) |
| Voidstorm | 47 | 99 (JP) |
| Aurorastorm | 48 | 99 (JP) |

### Défensifs / utilitaires

| Sort | BLM | PLD | RDM | RUN | SCH | WHM |
|---|---|---|---|---|---|---|
| Aquaveil | — | — | 12 | 15 | 13 | 10 |
| Blink | — | — | 23 | 35 | 30 | 19 |
| Stoneskin | — | — | 34 | 55 | 44 | 28 |
| Phalanx | — | 77 | 33 | 68 | — | — |
| Phalanx II | — | — | 75 | — | — | — |
| Erase | — | — | — | — | 39 | 32 |
| Sneak | — | — | 20 | — | 20 | 20 |
| Invisible | — | — | 25 | — | 25 | 25 |
| Deodorize | — | — | 15 | — | 15 | 15 |
| Foil | — | — | — | 58 | — | — |
| Reprisal | — | 61 | — | — | — | — |
| Crusade | — | 88 | — | 88 | — | — |
| Auspice | — | — | — | — | — | 55 |
| Adloquium | — | — | — | — | 88 | — |
| Animus Augeo / Minuo | — | — | — | — | 85 | — |
| Embrava | — | — | — | — | 2h ability | — |

### Déplacement

| Sort | BLM | WHM |
|---|---|---|
| Warp | 17 | — |
| Warp II | 40 | — |
| Escape | 29 | — |
| Retrace | 55 | — |
| Teleport-Dem / Holla / Mea | — | 36 |
| Teleport-Altep / Yhoat | — | 38 |
| Teleport-Vahzl | — | 42 |
| Recall-Jugner / Meriph / Pashh | — | 53 |

---

## Fichiers liés

- `red-mage.md` — job RDM complet
- `composure.md` — ability Composure
- `lethargy-armor-set.md` — set bonus Augments Composure