# Song Potency (BRD)

Valeurs de puissance par **bonus d'équipement** (`+song`). Toutes les tables supposent un **skill cappé**.

`Bonus Amount` = somme des `+<Song>` sur l'équipement (ex. Fili donne Madrigal+1, Minuet+1, March+1, Ballad+1, Scherzo+1).

## Ordre d'application — à lire avant les tables

Les bonus **Merit** et **Job Points** s'appliquent **après** le bonus en pourcentage de `+song`. Les tables ci-dessous **ne les incluent pas**.

| Chant | Merit | Job Points |
|---|---|---|
| Requiem | — | jusqu'à +60 DoT (20 ranks × 3) |
| Lullaby | — | jusqu'à +20 s durée |
| Minne | jusqu'à +10 DEF (5 ranks × 2) | jusqu'à +20 DEF |
| Minuet | jusqu'à +5 ATK (5 ranks × 1) | jusqu'à +20 ATK |
| Madrigal | jusqu'à +5 ACC (Group 1, 5 ranks) | — |

---

# Monster Debuffs

## Requiem — Light DoT

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| Foe Requiem | 2 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| Foe Requiem II | 3 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| Foe Requiem III | 4 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| Foe Requiem IV | 5 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| Foe Requiem V | 6 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| Foe Requiem VI | 7 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
| Foe Requiem VII | 8 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |

> **Le premier +1 ne fait rien** (2→2, 3→3…). Le gain ne démarre qu'à +2. Un seul point de Requiem sur le gear est du gâchis.

## Lullaby — durée de Sleep (secondes)

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Foe Lullaby | 30 | 33 | 36 | 39 | 42 | 45 | 48 | 51 | 54 | 57 |
| Horde Lullaby | 30 | 33 | 36 | 39 | 42 | 45 | 48 | 51 | 54 | 57 |
| Foe Lullaby II | 60 | 66 | 72 | 78 | 84 | 90 | 96 | 102 | 108 | 114 |
| Horde Lullaby II | 60 | 66 | 72 | 78 | 84 | 90 | 96 | 102 | 108 | 114 |

Foe (mono) et Horde (AoE) ont des valeurs **identiques**. +10 % de base par point.

## Elegy — réduction d'attack speed

**Battlefield Elegy**

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| % | -25.00 | -27.44 | -29.98 | -32.42 | -34.96 | -37.50 | -39.94 | -42.48 |
| /1024 | -256 | -281 | -307 | -332 | -358 | -384 | -409 | -435 |

**Carnage Elegy**

| | +0 → +7 |
|---|---|
| % | **-50.00** (constant) |
| /1024 | -512 |

> **Carnage Elegy est cappée à -50 % dès +0.** Aucun `+Elegy` n'a le moindre effet dessus. Le gear Elegy ne sert que pour Battlefield.

## Threnody — Magic Evasion down (élément correspondant)

| Tier | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Threnody (I) | 50 | 55 | 60 | 65 | 70 | 75 | 80 | 85 | 90 | 95 |
| **Threnody II** | 160 | 165 | 170 | 175 | 180 | 185 | 190 | 195 | 200 | 205 |

Valeurs identiques pour les 8 éléments (Fire, Ice, Wind, Earth, Lightning, Water, Light, Dark).

> Le tier I scale à **+5/point sur base 50** (+10 %/point) ; le tier II scale à **+5/point sur base 160** (+3 %/point). Le gear `+Threnody` est proportionnellement bien moins rentable sur les Threnody II — mais leur base triple le tier I.

## Finale

**Magic Finale** — chance de retrait d'effet : la source indique seulement « bonus Magic Accuracy croissant par level » — ⚠ *Verification Needed*, pas de table.

## Virelai — durée de Charm (secondes)

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| Maiden's Virelai | 30 | 33 | 36 | 39 | 42 | 45 | 48 | 51 |

## Nocturne

**Pining Nocturne**

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| Casting Time + | 15.0 % | 16.5 % | 18.0 % | 19.5 % | 21.0 % | 22.5 % | 24.0 % | 25.5 % |
| Magic Accuracy − | -15.0 | -16.5 | -18.0 | -19.5 | -21.0 | -22.5 | -24.0 | -25.5 |

---

# Party Buffs

## Paeon — HP restaurés / tick

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Army's Paeon | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| Army's Paeon II | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| Army's Paeon III | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
| Army's Paeon IV | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
| Army's Paeon V | **7** | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| Army's Paeon VI | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 |

+1 flat par point. Note : Paeon IV → V saute de 2 (5 → 7), pas 1.

## Ballad — MP restaurés / tick

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| Mage's Ballad | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| Mage's Ballad II | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| Mage's Ballad III | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |

> Le `+Ballad` est le bonus le plus rentable du jeu en valeur relative : +1 MP/tick par point sur une base de 3. Un Ballad III à +8 rend **11 MP/tick**, soit ×3.67 la base.

## Minne — Defense

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Knight's Minne | 30 | 33 | 36 | 39 | 42 | 45 | 48 | 51 | 54 | 57 |
| Knight's Minne II | 69 | 75 | 82 | 89 | 96 | 103 | 110 | 117 | 124 | 131 |
| Knight's Minne III | 108 | 118 | 129 | 140 | 151 | 162 | 172 | 183 | 194 | 205 |
| Knight's Minne IV | 164 | 180 | 196 | 213 | 229 | 246 | 262 | 278 | 295 | 311 |
| Knight's Minne V | 204 | 224 | 244 | 265 | 285 | 306 | 326 | 346 | 367 | 387 |

## Minuet — Attack & Ranged Attack

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| Valor Minuet | 32 | 35 | 38 | 41 | 44 | 48 | 51 | 54 | 57 |
| Valor Minuet II | 64 | 70 | 76 | 83 | 89 | 96 | 102 | 108 | 115 |
| Valor Minuet III | 96 | 105 | 115 | 124 | 134 | 144 | 153 | 163 | 172 |
| Valor Minuet IV | 112 | 123 | 134 | 145 | 156 | 168 | 179 | 190 | 201 |
| Valor Minuet V | 124 | 136 | 148 | 161 | 173 | 186 | 198 | 210 | 223 |

## Madrigal — Melee Accuracy

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Sword Madrigal | 45 | 49 | 54 | 58 | 63 | 67 | 72 | 76 | 81 | 85 |
| Blade Madrigal | 60 | 66 | 72 | 78 | 84 | 90 | 96 | 102 | 108 | 114 |

## Prelude — Ranged Accuracy

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| Hunter's Prelude | 45 | 49 | 54 | 58 | 63 | 67 | 72 | 76 | 81 |
| Archer's Prelude | 60 | 66 | 72 | 78 | 84 | 90 | 96 | 102 | 108 |

## Mambo — Evasion

| Song | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Sheepfoe Mambo | 48 | 52 | 57 | 62 | 67 | 72 | 76 | 81 | 86 | 91 |
| Dragonfoe Mambo | 72 | 79 | 86 | 93 | 100 | 108 | 115 | 122 | 129 | 136 |

## March — Magic Haste

**Advancing March**

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| % | 10.55 | 11.52 | 12.60 | 13.67 | 14.75 | 15.82 | 16.80 | 17.87 | 18.95 |
| /1024 | 108 | 118 | 129 | 140 | 151 | 162 | 172 | 183 | 194 |

**Victory March**

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| % | 15.92 | 17.48 | 19.04 | 20.61 | 22.27 | 23.83 | 25.39 | 27.05 | 28.61 |
| /1024 | 163 | 179 | 195 | 211 | 228 | 244 | 260 | 277 | 293 |

**Honor March** — *Marsyas requis* (cf. `bard.md`)

| | +0 | +1 | +2 | +3 | +4 |
|---|---|---|---|---|---|
| Magic Haste % | 12.30 | 13.48 | 14.65 | 15.82 | 16.99 |
| /1024 | 126 | 138 | 150 | 162 | 174 |
| Attack & R.Attack | 168 | 184 | 200 | 216 | 232 |
| Acc. & R.Acc. | 42 | 46 | 50 | 54 | 58 |

> Honor March n'est tabulée que jusqu'à **+4** (les autres Marches vont à +8). Elle apporte du haste *plus* de l'attaque et de l'accuracy — c'est le seul chant à cumuler trois lignes.

## Etude — stat unique

**Tier I** (base 9)

| Etude | Stat | +0 → +9 |
|---|---|---|
| Sinewy | STR | 9 → 18 |
| Dextrous | DEX | 9 → 18 |
| Vivacious | VIT | 9 → 18 |
| Quick | AGI | 9 → 18 |
| Learned | INT | 9 → 18 |
| Spirited | MND | 9 → 18 |
| Enchanting | CHR | 9 → 18 |

**Tier II** (base 15)

| Etude | Stat | +0 → +9 |
|---|---|---|
| Herculean | STR | 15 → 24 |
| Uncanny | DEX | 15 → 24 |
| Vital | VIT | 15 → 24 |
| Swift | AGI | 15 → 24 |
| Sage | INT | 15 → 24 |
| Logical | MND | 15 → 24 |
| Bewitching | CHR | 15 → 24 |

Progression linéaire **+1 par point** dans les deux tiers.

## Carol — résistance élémentaire

**Carol (I)** — tous éléments

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Resistance | 80 | 88 | 96 | 104 | 112 | 120 | 128 | 136 | 144 | 152 |

**Carol II** — tous éléments

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Resistance | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 100 | 100 |
| **Nullification** | 15 % | 16 % | 18 % | 19 % | 21 % | 22 % | 24 % | 25 % | 26 % | 27 % |

> Sur Carol II la **résistance est figée à 100** ; le `+Carol` n'alimente que la **nullification** (chance d'annuler complètement le dégât élémentaire). Mécanique entièrement différente du tier I.

> ⚠ **Erreur dans la source** : la table *Ice Carol II* liste « Wind Resistance / Wind Nullification ». C'est un copier-coller depuis Wind Carol II — lire **Ice**.

## Hymnus

**Goddess's Hymnus** — Reraise I. Restitue **50 %** des XP perdus. Aucune scaling par `+song`.

## Mazurka — Movement Speed

| Song | +0 → +7 |
|---|---|
| Raptor Mazurka | **10 %** (constant) |
| Chocobo Mazurka | **20 %** (constant) |

> Aucun scaling. Le gear `+Mazurka` n'existe pas utilement.

## Sirvente

**Foe Sirvente** — Enmity Loss Degradation

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| | -35 % | -36 % | -37 % | -38 % | -39 % | -40 % | -41 % | -42 % |

## Dirge

**Adventurer's Dirge** — Enmity Reduction

| | +0 → +7 |
|---|---|
| | **-32** (constant) |

Aucun scaling.

## Scherzo — Damage Reduction

**Sentinel's Scherzo**

| | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 |
|---|---|---|---|---|---|---|---|---|---|
| | 45 % | 46 % | 47 % | 48 % | 49 % | 50 % | 51 % | 52 % | 53 % |

## Aria — Physical Damage Limit+

**Aria of Passion** — *Loughnashade (certaines versions) requis*

| | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|
| | 15.6 % | 16.9 % | 18.2 % | 19.5 % | 20.8 % | 22.1 % |

> La table **démarre à +2**, pas à +0. L'arme qui donne accès au chant fournit déjà du `+song`, donc les paliers inférieurs n'existent pas en pratique.

---

# Synthèse — chants sans scaling par `+song`

Inutile de gear pour ceux-là :

| Chant | Valeur figée |
|---|---|
| Carnage Elegy | -50 % attack speed |
| Raptor Mazurka | +10 % movement |
| Chocobo Mazurka | +20 % movement |
| Adventurer's Dirge | -32 enmity |
| Goddess's Hymnus | Reraise I, 50 % XP |
| Carol II (partie *Resistance*) | 100 — seule la nullification scale |

# Données manquantes dans la source

Marquées `Question` — aucune valeur publiée :

Fowl Aubade (Sleep res.) · Herb Pastoral (Poison res.) · Shining Fantasia (Blind res.) · Scop's & Puppet's Operetta (Silence res.) · Gold Capriccio (Petrification res.) · Warding Round (Curse res.) · Goblin Gavotte (Bind res.) · Magic Finale (*Verification Needed*).

---

## Fichiers liés

- `bard.md` — job BRD, liste des chants par level
- `fili-attire-set.md` — set Empyrean : Madrigal+1, Minuet+1, March+1, Ballad+1, Scherzo+1
- `carnwenhan.md` — Mythic BRD, song duration +50 %
- [song-duration-items.txt](song-duration-items.txt) — la liste extraite du gear qui augmente la **durée** des chants (provenance de `song_dur.h` ; ces tables-ci ne couvrent que la **puissance**). Cf. [buffs-on-allies.md](buffs-on-allies.md).