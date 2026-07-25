# Carnwenhan (Level 119 III)

Mythic dagger — **Bard**. Weapon Skill : **Mordant Rime**.

| Champ | Valeur |
|---|---|
| Type | Weapon — Dagger |
| Jobs | Bard |
| Races | All |
| Level | 99 |
| Item Level | **119** |
| DMG | 116 |
| Delay | 186 |
| DPS | 37.42 |
| TP/Hit | 62.47 |
| Flags | Equippable, Exclusive, Rare, Not vendorable, Not sendable |
| Stack | 1 |

## Description (base, non augmentée)

```
DMG:116  Delay:186
Magic Accuracy+40   Magic Damage+155
Dagger skill +269   Parrying skill +269   Magic Accuracy skill +255
Increase song effect duration V
"Mordant Rime"
Aftermath: Increases Magic Accuracy and Accuracy
Occasionally attacks twice or thrice
Afterglow
```

### Increase song effect duration V

= **Song duration +50 %.**

---

## Aftermath (post-WS Mordant Rime)

| Niveau | Effet | Durée |
|---|---|---|
| Lv.1 | Magic Accuracy (valeur non confirmée) | 4:30 |
| Lv.2 | `floor(TP ÷ 10 + 10)` Accuracy — *à vérifier* | 2:00 |
| Lv.3 | **Occasionally attacks twice (40 %) or thrice (20 %)** | 3:00 |

Le multi-attack de l'Aftermath Lv.3 est distinct de la ligne `Occasionally attacks twice or thrice` native de l'arme.

---

## Versions

`Lv.75 → 80 → 85 → 90 → 95 → 99 → 99 II → iLv 119 → iLv 119 II → iLv 119 III`

Cette page = le tier terminal (**iLv 119 III**, avec Afterglow).

---

## Obtention

| Source | Coût / notes |
|---|---|
| Final REME Upgrade | iLv 119 **non-Afterglow** : **10 000 Beitetsu** |
| Final REME Upgrade | iLv 119 **Afterglow** : **1 Beitetsu** |
| Dealer Moogle (Various) | Kupon W-M119 / Mog Kupon W-M119 → arme **non augmentée** |
| Dealer Moogle (Various) | Kupon W-RMEA / Mog Kupon W-RMEA |

> Le différentiel 10 000 vs 1 Beitetsu porte sur l'étape d'upgrade, pas sur l'arme finale — l'Afterglow s'obtient en poussant la chaîne complète.

---

## Augments — Oboro

**Où** : Oboro, Port Jeuno (E-6).
**Items** : S. Astral Detritus & M. Astral Detritus. (Voir prérequis Oboro.)

### Path A — R15 (max)

```
DMG +14
Mordant Rime: Damage +15%
Accuracy +30   Magic Accuracy +30
```

### Table par rank

| Rank | [1] DMG | [2] Mordant Rime DMG | [3] Acc / M.Acc |
|---|---|---|---|
| 1 | +1 | +1 % | +1 / +1 |
| 2 | +1 | +2 % | +3 / +3 |
| 3 | +2 | +3 % | +5 / +5 |
| 4 | — | — | — |
| 5 | — | — | — |
| 6 | +6 | +6 % | +11 / +11 |
| 7 | +6 | +7 % | +13 / +13 |
| 8 | — | — | — |
| 9 | — | — | — |
| 10 | — | — | — |
| 11 | +10 | +11 % | +21 / +21 |
| 12 | +11 | +12 % | +23 / +23 |
| 13 | +12 | +13 % | +25 / +25 |
| 14 | +13 | +14 % | +27 / +27 |
| 15 | **+14** | **+15 %** | **+30 / +30** |

> Ranks 4, 5, 8, 9, 10 : **aucune donnée** dans la source (pas « aucun gain » — non renseigné). Les valeurs interpolent proprement, donc la progression est probablement continue.

---

## Hidden Effect — le point important

L'arme porte un **`Mordant Rime damage +30%` caché**, non listé dans la description.

Il est **multiplicatif** avec l'augment `Mordant Rime: Damage +15%` du rank 15 — ce sont deux termes distincts de l'équation :

```
1.30 × 1.15 = 1.495   →   +49.5 % de dégâts sur Mordant Rime
```

**Pas +45 %.** L'erreur naturelle est de les additionner.

---

## À retenir pour un set BRD

- `Increase song effect duration V` = **+50 % durée** — c'est la ligne qui fait de Carnwenhan l'arme de buffing de référence, cumulable avec le Song effect duration du set Fili (`fili-attire-set.md`) et le gift JP +5 %.
- Les skills bruts (Dagger/Parrying +269, Magic Accuracy skill +255) sont massifs mais ne touchent pas la durée des chants.

---

## Fichiers liés

- `bard.md` — job BRD complet
- `fili-attire-set.md` — set Empyrean BRD