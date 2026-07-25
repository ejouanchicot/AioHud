# Movement Speed (FFXI)

Statistique qui détermine la vitesse de déplacement d'un personnage.

- **Vitesse de base** : 5.0 yalms/seconde
- **Vitesse maximale** : 8.0
- Chaque **+0.1 = +2 %** de vitesse ; tous les ajustements sont des multiples de cette valeur.
- La vitesse de base était de 4.0 à l'origine, augmentée de 25 % (→ 5.0) en décembre 2013. L'équipement a été ajusté pour compenser, mais **pas** les capacités de job ni le cap global.
  - *Flee* : +100 % à l'origine (8.0/4.0), mais toujours plafonné à 8.0 → aujourd'hui **+60 %** (8.0/5.0).
  - Buffs à +25 % (+1.0/4.0) → aujourd'hui **+20 %** (+1.0/5.0).
- La vitesse de déplacement est l'une des rares variables de FFXI entièrement gérée **côté client**.

---

## Quantification

Le moyen le plus simple est de lire la valeur de vitesse envoyée dans les paquets. L'addon **SpeedChecker** de Windower le fait.

---

## Bonus d'amélioration

### Active Effects
| Source | Bonus |
|---|---|
| Atma of Ambition | +12 % |
| Atma of the Master Crafter | +12 % |
| Cheer: Jumbotender | +3 % |

### Temporary Effects
| Source | Bonus |
|---|---|
| Flee | +60 % |
| Sprinter's Drink | +60 % |
| Hermes Quencher | +60 % |
| Pom-pom Fruit (Quickening) | +10 % |

### Volatile Effects
| Source | Bonus |
|---|---|
| Raptor Mazurka | +10 % |
| Chocobo Mazurka | +20 % |
| Bolter's Roll | ×1.0 ~ 1.62 |
| Chocobo Jig (Quickening) | +20 % |
| Fleet Wind (Quickening) | +20 % |
| Sprinter's Shoes / Rarab Cap +1 / Talaria (Quickening) | +10 % |

**Règles d'empilement** (cap à +60 % / 160 % total) :
- Seule la **plus haute** valeur d'équipement compte.
- Mazurka + Quickening : maximum combiné de **20 %**.
- Bolter's Roll : multiplicatif avec l'équipement, mais pas avec les autres sources.

**Forme fermée :**
```
(100% + max(Equipment)) * Roll + min(sum(Quickening, Mazurka), 20%) + sum(Active) + sum(Temporary) <= 160%
```

> Le client ne reçoit que des pourcentages **pairs** : les totaux impairs sont arrondis à l'inférieur pair.
>
> Tous les *volatile effects* se dissipent dès que le joueur effectue une action offensive contre un ennemi, ou en devient la cible — même si l'action est esquivée ou résistée.

---

## Réduction (Debuff)

La vitesse est réduite par le statut **Weight** infligé par *Gravity* ou des capacités monstres à effet Gravity. La pénalité va de légère (−12 %) à quasi-immobilisante.

Weight peut être infligé aux ennemis via *Gravity*, *Gravity II*, *Indi-Gravity*, et en effet additionnel de diverses armes et Weapon Skills.

---

## Équipement

**Shneddick Ring / +1** et **Kupo Suit** sont les seuls à donner **+18 %** à tous les jobs (les anneaux sont mutuellement exclusifs avec les autres récompenses d'anneau Seekers of Adoulin).

Chaque job a accès à au moins une pièce donnant un bonus inconditionnel d'au moins +12 %, et la plupart à au moins une pièce à +18 %.

**Jobs sans option 18 % au niveau 99** (hors Shneddick Ring, gear non-iLVL) :
- WHM / BLM / SMN / SCH
- SAM / NIN (hors Dusk 17:00 → 07:00)
- WAR / MNK / PUP

### Meilleure pièce par job
| Jobs | Pièce optimale | Slot | Vitesse |
|---|---|---|---|
| RDM PLD DRK RNG DRG BLU COR RUN | Carmine Cuisses +1 | Legs | 18 % |
| THF | Pillager's Poulaines +3 | Feet | 18 % |
| GEO | Geomancy Sandals +3 | Feet | 18 % |
| BRD | Fili Cothurnes +3 | Feet | 18 % |
| BST DNC | Skadi's Jambeaux +1 | Feet | 18 % |
| MNK WHM BLM SMN SCH | Herald's Gaiters | Feet | 12 % |
| SAM NIN | Danzo Sune-Ate | Feet | 12 % |
| WAR MNK PUP | Hermes' Sandals | Feet | 12 % |

### Liste complète des pièces

| Item | Niveau | Slot | Jobs | Vitesse |
|---|---|---|---|---|
| Shneddick Ring | 99 | Rings | All Jobs | 18 % |
| Shneddick Ring +1 | 99 | Rings | All Jobs | 18 % |
| Destrier Beret | 1 | Head | All Jobs | Latent (niv. ≤ 30) |
| Pitchfork +1 | 1 | Weapon | All Jobs | Costume |
| S. Bunny Hat +1 | 1 | Head | All Jobs | Costume |
| Snow Bunny Hat | 1 | Head | All Jobs | Costume |
| Purple Race Silks | 1 | Body | All Jobs | Chocobo |
| Ninja Kyahan | 54 | Feet | NIN | 25 % (Nuit) |
| Nin. Kyahan +1 | 74 | Feet | NIN | 25 % (Dusk→Dawn) |
| Hachiya Kyahan | 99 | Feet | NIN | 25 % (Dusk→Dawn) |
| Hachiya Kyahan +1 | 99 | Feet | NIN | 25 % (Dusk→Dawn) |
| Hachiya Kyahan +2 | 99 | Feet | NIN | 25 % (Dusk→Dawn) |
| Hachiya Kyahan +3 | 99 | Feet | NIN | 25 % (Dusk→Dawn) |
| Skadi's Jambeaux +1 | 99 | Feet | THF BST RNG COR DNC RUN | 18 % |
| Fajin Boots | 95 | Feet | THF RNG | 18 % |
| Geomancy Sandals | 99 | Feet | GEO | 12 % |
| Iaso Boots | 99 | Feet | WHM RDM BRD SCH | 12 % |
| Tandava Crackows | 83 | Feet | DNC | 12 % |
| Aoidos' Cothrn. +1 | 81 | Feet | BRD | 12 % |
| Aoidos' Cothrn. +2 | 81 | Feet | BRD | 12 % |
| Fili Cothurnes | 99 | Feet | BRD | 12 % |
| Fili Cothurnes +1 | 99 | Feet | BRD | 18 % |
| Fili Cothurnes +2 | 99 | Feet | BRD | 18 % |
| Fili Cothurnes +3 | 99 | Feet | BRD | 18 % |
| Danzo Sune-Ate | 80 | Feet | SAM NIN | 12 % |
| Skadi's Jambeaux | 75 | Feet | THF BST RNG COR | 12 % |
| Blood Cuisses | 73 | Legs | RDM PLD DRK RNG DRG BLU COR RUN | 12 % |
| Crimson Cuisses | 73 | Legs | RDM PLD DRK RNG DRG BLU COR RUN | 12 % |
| Carmine Cuisses | 99 | Legs | RDM PLD DRK RNG DRG BLU COR RUN | 12 % |
| Carmine Cuisses +1 | 99 | Legs | RDM PLD DRK RNG DRG BLU COR RUN | 18 % |
| Herald's Gaiters | 70 | Feet | MNK WHM BLM SMN SCH GEO | 12 % |
| Hermes' Sandals | 70 | Feet | WAR MNK COR PUP RUN | 12 % |
| Hermes' Sandals +1 | 70 | Feet | WAR MNK COR PUP RUN | 12 % |
| Pillager's Poulaines | 99 | Feet | THF | 12 % |
| Pillager's Poulaines +1 | 99 | Feet | THF | 12 % |
| Pillager's Poulaines +2 | 99 | Feet | THF | 12 % |
| Pillager's Poulaines +3 | 99 | Feet | THF | 18 % |
| Orion Socks | 99 | Feet | RNG | 12 % |
| Orion Socks +1 | 99 | Feet | RNG | 12 % |
| Orion Socks +2 | 99 | Feet | RNG | 12 % |
| Orion Socks +3 | 99 | Feet | RNG | 18 % |
| Geomancy Sandals +1 | 99 | Feet | GEO | 12 % |
| Geomancy Sandals +2 | 99 | Feet | GEO | 12 % |
| Geomancy Sandals +3 | 99 | Feet | GEO | 18 % |
| Desert Boots | 62 | Feet | All Jobs | 12 % (Sandstorms) |
| Desert Boots +1 | 62 | Feet | All Jobs | 12 % (Sandstorms) |
| Storm Crackows | 50 | Feet | All Jobs | 12 % (Assault) |
| Areion Boots | 20 | Feet | THF RNG | 12 % |
| Areion Boots +1 | 20 | Feet | THF RNG | 12 % |
| Jute Boots | 99 | Feet | THF RNG | 12 % |
| Jute Boots +1 | 99 | Feet | THF RNG | 18 % |
| Strider Boots | 20 | Feet | THF RNG | 12 % |
| Trotter Boots | 20 | Feet | THF RNG | 12 % |
| Federation Aketon | 1 | Body | All Jobs | 12 % (zones Windurst, citoyens) |
| Kingdom Aketon | 1 | Body | All Jobs | 12 % (zones San d'Oria, citoyens) |
| Republic Aketon | 1 | Body | All Jobs | 12 % (zones Bastok, citoyens) |
| Ptr.Prt. Livery | 1 | Body | All Jobs | 12 % (Campaign, influence Windurst) |
| Ryl.Grd. Livery | 1 | Body | All Jobs | 12 % (Campaign, influence San d'Oria) |
| Myth.Msk. Livery | 1 | Body | All Jobs | 12 % (Campaign, influence Bastok) |
| Paean Boots | 99 | Feet | WHM RDM BRD SCH | 8 % |
| Track Pants +1 | 1 | Legs | All Jobs | 8 % |
| Blitzer Poleyn | 75 | Legs | WAR PLD DRK DRG SAM | 8 % |
| Desultor Tassets | 75 | Legs | MNK THF BST RNG NIN BLU COR PUP DNC RUN | 8 % |
| Tatsumaki Sitagoromo | 75 | Legs | WHM BLM RDM BRD SMN SCH GEO | 8 % |

### Pénalités (−12 %)
| Item | Niveau | Slot | Jobs |
|---|---|---|---|
| Plumb Boots | 99 | Feet | All Jobs |
| Lavalier +1 | 96 | Neck | All Jobs |
| Mollusca Mantle | 92 | Back | WAR MNK RDM THF PLD DRK BST BRD RNG SAM NIN DRG BLU COR DNC |
| Oneiros Axe | 88 | Weapon | WAR |
| Oneiros Barbut | 88 | Head | WAR PLD |
| Scuta Cape | 88 | Back | All Jobs |
| Oily Trousers | 74 | Legs | (multi) |
| Dusk Gloves / +1 | 72 | Hands | (multi) |
| Dusk Jerkin / +1 | 72 | Body | (multi) |
| Dusk Ledelsens / +1 | 72 | Feet | (multi) |
| Dusk Mask / +1 | 72 | Head | (multi) |
| Dusk Trousers / +1 | 72 | Legs | (multi) |
| Mahant Sandals | 63 | Feet | MNK WHM BLM RDM PLD BRD RNG SMN BLU PUP SCH |
| Raikiri | 56 | Weapon | SAM |

### Enchantements (empilent avec l'équipement)
| Item | Slot | Jobs | Vitesse |
|---|---|---|---|
| Sprinter's Shoes | Feet | All Jobs | 10 % |
| Rarab Cap +1 | Head | All Jobs | 10 % |
| Talaria | Feet | All Jobs | 10 % |
