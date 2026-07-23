# Composure

Job ability — Red Mage. Reference sheet.

## Identity

| Champ | Valeur |
|---|---|
| Job | Red Mage |
| Type | Level ability |
| Level | 50 |
| Duration | 02:00:00 |
| Recast | 00:05:00 |
| Cumulative Enmity | 0 |
| Volatile Enmity | 80 |
| Command | `/ja "Composure" <me>` |

Non disponible en sub job RDM.

## Effets

État persistant de 2 h. Tant qu'il est actif :

### 1. Accuracy

```
Accuracy = floor(((24 × Level) + 74) / 49)
```

| Level | Accuracy base |
|---|---|
| 50 | 26 |
| 99 | 50 |

Job Points — catégorie *Composure Effect*, 20 ranks, +1 Accuracy physique par rank.

```
Accuracy totale (99, JP max) = 50 + 20 = 70
```

### 2. Enhancing magic (self-target uniquement)

- Durée ×3 (+200 %) sur les buffs enhancing **et** black magic lancés sur soi-même.
- Cap dur à **30 minutes**.
- Si la durée naturelle du sort dépasse déjà 30 min sans Composure, l'ability n'a **aucun effet** dessus — la durée native, plus longue, est conservée. Pas de downgrade.

### 3. En-spells

- Base damage ×3 (+200 %).
- Les modificateurs `Enspell +n` de l'équipement sont eux aussi triplés.

### 4. Coût

- Recast de **tous les sorts** +25 %.

## Interaction équipement — durée sur les autres

Composure ne prolonge que le self-target. Pour l'enhancing lancé sur autrui, le levier est le set bonus (cumulable avec Composure, cibles disjointes).

| Set | Lvl | Effet |
|---|---|---|
| Estoqueur's Attire Set +2 | 81–89 | Enhancing (others) + Enfeebling duration |
| Lethargy Armor Set | 99 | idem |
| Lethargy Armor Set +1 | 99 | idem |
| Lethargy Armor Set +2 | 99 | idem |
| Lethargy Armor Set +3 | 99 | idem |

Scaling identique pour tous, évalué **au moment du cast** :

| Pièces portées | Bonus durée |
|---|---|
| 2 | +10 % |
| 3 | +20 % |
| 4 | +35 % |
| 5 | +50 % |

**Point critique** : le set bonus « Augments Composure » ne s'applique **pas** aux enhancing lancés sur soi-même. Self-cast → seul le ×3 / cap 30 min de l'ability joue. Les deux effets sont disjoints, jamais cumulés sur une même cible.

## Formule de durée (enhancing)

```
Durée = (Base
         + 6s × RDM Group 2 Merit Level
         + 3s × RDM Relic Hands G2 Merit augment
         + RDM Job Points
         + gear listant des secondes)
      × (Augments Composure set bonus)
      × (Duration % gear + Naturalist's Roll)
      × (Gear augments)
      × (Rune Fencer Gifts)
```

Les multiplicateurs sont appliqués séparément : `Enhancing Magic Duration +x%` sur d'autres pièces est multiplicatif avec le set bonus, et les augments d'items sont multiplicatifs après.

## Notes d'usage

- Le bonus set étant évalué au cast, un swap out post-cast ne retire pas la durée déjà acquise.
- Composure étant un état de 2 h avec recast 5 min, le reproc est trivial ; le vrai coût opérationnel reste le +25 % recast pendant les phases de nuking.

Voir aussi : `lethargy-armor-set.md` · [Buffs sur les alliés](buffs-on-allies.md) — l'estimation ally-cast qui applique ce modèle multiplicatif (dont le cas Regen : les secondes flat spécifiques à Regen entrent dans la base via `regen_dur_gear_sec`).