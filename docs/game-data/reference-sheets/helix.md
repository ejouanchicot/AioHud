# Helix (SCH) — Mémo

Sorts **exclusifs Scholar**, dégâts élémentaires **DoT** (Damage over Time).

---

## Fonctionnement de base

1. **Impact initial** : dégâts magiques comme un sort de magie élémentaire classique (**non capés**).
2. **Tocks** : ces mêmes dégâts se répètent — premier tock **1 à 10 s après le cast**, puis **toutes les 10 s**.
3. **Cap du DoT** : **9 999 dégâts par tock** (l'impact initial, lui, n'est pas capé).
4. **Immanence** : un Helix lancé sous Immanence donne des fenêtres de **Skillchain et Magic Burst plus longues (+2 s)**.

---

## Liste des Helix

| Sort | Élément |
|---|---|
| Pyrohelix / II | Feu |
| Cryohelix / II | Glace |
| Anemohelix / II | Vent |
| Geohelix / II | Terre |
| Ionohelix / II | Foudre |
| Hydrohelix / II | Eau |
| Luminohelix / II | Lumière |
| Noctohelix / II | Ténèbres |

Tous les Helix d'un **même tier font les mêmes dégâts** (mêmes M et V). Le choix de l'élément se fait donc sur :
- la **faiblesse élémentaire** de la cible,
- le **jour / la météo** actifs.

---

## Écrasement (overwrite)

| Sort lancé | Écrase un Helix I actif | Écrase un Helix II actif |
|---|---|---|
| **Tier I** | ✅ oui | ❌ non |
| **Tier II** | ✅ oui | ✅ oui |

**Conséquence pratique** : Helix **I pour ouvrir/faire le Skillchain**, Helix **II pour le Magic Burst** (il ne sera pas écrasé par un tier 1, et il écrase tout le reste).

---

## Durée

La durée dépend du **niveau de Scholar** (donc un **/SCH est plafonné à 60 s**).

| Niveau SCH | Durée |
|---|---|
| 20–39 | 30 s |
| 40–59 | 60 s |
| 60–99 | 90 s |

### Modificateurs de durée

| Condition | Effet |
|---|---|
| **Dark Arts** (SCH main job) | Durée portée à **168 s (18 tocks)** au niveau 99, + **24 de dégâts de base** |
| **Tabula Rasa** | Bonus de potence et de durée **~50 % supérieur** à celui de Dark Arts |
| **Modus Veritas** | **Divise la durée restante par 2**, **double les dégâts par tock** |

> L'effet de Dark Arts semble purement dépendant du niveau (données encore en test côté wiki).

---

## Bonus jour / météo

Si le **jour** et/ou la **météo** correspondants sont actifs, l'Helix reçoit le bonus (ou la pénalité) de dégâts **100 % du temps, même sans Elemental Obi**.

---

## Merits

Malgré le libellé du merit tier 1 de SCH (« Increases … Magic Attack Bonus »), l'effet est en réalité un **pourcentage** :

- **+2 % de dégâts Helix par merit**
- **+10 % au maximum** (5/5), sur les **deux tiers**.

---

## Modus Veritas — détails

- Double les dégâts par tick, **halve la durée restante**.
- Utilisable par **n'importe quel Scholar**, mais **une seule fois par Helix**.
- **Très imprécis sur les monstres haut niveau** (taux de résistance élevé).
- Ne permet **pas** de dépasser le cap de **9 999** par tock.
- Historique : autrefois cumulable — doubler dix fois un Helix à 50 dégâts donnait +50 000 dégâts en un tick → patché.

---

## Récapitulatif express

- Helix I → **Skillchain**
- Helix II → **Magic Burst** (+ écrase tout)
- Dark Arts obligatoire pour la durée max (**168 s / 18 tocks** au 99)
- Cap DoT : **9 999 / tock**
- Élément choisi selon **faiblesse cible + jour/météo**
- Merits Helix : **+10 %** max

---

### Sources

- [Category:Helix — FFXI Wiki (BG-Wiki)](https://www.bg-wiki.com/ffxi/Category:Helix)
- Helix testing on the Test Server (Foldypaws, BG)
- Dark Arts test server announcement (Camate, OF)
- Helix merit damage increase (CDF, BG)
- Helix DoT cap testing (Bismarck.Squah, FFXIAH) et confirmation (vanafratello)
- Helix II testing (CDF, BG)

---

## Ce que le plugin en fait (2026-09-11)

| Fait du mémo | Où c'est utilisé | État |
|---|---|---|
| Tier I n'écrase **pas** un Helix II actif | `debuff_rules.h` — `debuff_ladder_proven` | **appliqué** : un Helix I lancé sur un Helix II vivant ne remplace plus la ligne |
| Tier I écrase un Tier I, Tier II écrase tout | même statut 186 → le rafraîchissement normal s'en charge | appliqué, rien de spécial à faire |
| Durée **90 s** (SCH 60-99) | `tb_debuff_gen.h`, via `CORRECTIONS` dans `scripts/gen_tb_debuffs.py` | **corrigé** : `res/spells.lua` ET `tb_spells.lua` donnaient 230 s pour le tier I |
| **Dark Arts → 168 s** | — | **pas modélisé**. On annonce 90 s, le sort dure plus : on protège moins longtemps que nécessaire, ce qui est le sens d'erreur inoffensif |
| **Tabula Rasa → ~+50 % de plus que Dark Arts** | — | pas modélisé, même raison, et la valeur exacte n'est pas mesurée |
| **Modus Veritas → divise la durée restante par 2** | — | **le seul trou qui compte** : le Helix II peut finir AVANT nos 90 s, donc on refuserait un Helix I que le jeu accepte. Détectable (l'ability passe dans le paquet d'action, comme un Quick Draw), à faire |
| Merits +2 %/merit (dégâts), cap DoT 9 999, bonus jour/météo | — | dégâts, pas durée : hors de portée du HUD |

**Pourquoi 230 s était dangereux et pas seulement faux** : cette durée ne sert pas qu'au compte à rebours
affiché. `debuff_rules.h` s'en sert pour décider si le debuff le plus fort est **encore vivant**, donc s'il
doit refuser un cast plus faible. Une durée trop LONGUE refuse un sort que le jeu accepte — le debuff
n'apparaît jamais, sans un mot. Une durée trop COURTE arrête simplement de protéger trop tôt. Toutes les
valeurs de cette famille sont donc prises au bout court exprès.
