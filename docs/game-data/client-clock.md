# L'horloge du client — « maintenant » pour les décomptes de buffs

**Source : le code du client, décompilé sous Ghidra le 2026-07-20.** Confirmé en jeu : après correction,
les décomptes d'AioHUD correspondent **à la seconde près** à ceux du jeu.

## Le problème

Les durées affichées accusaient un retard **constant de 2 à 3 secondes** sur celles du client. Un écart
constant, toujours dans le même sens, est la signature d'une **référence temporelle différente** — pas de
gigue réseau ni d'imprécision serveur.

Deux causes indépendantes, qui s'additionnaient.

## Cause 1 — la mauvaise horloge

Nous calculions « maintenant » depuis `time(0)`, l'horloge Windows, en secondes entières.

**Le client n'y touche jamais.** `FUN_05D63910` (FFXiMain+0x103910) :

```
CALL 0x05D3C060          ; secondes monotones locales
ADD  EAX,[0x06140AF8]    ; + décalage serveur
RET
```

Donc `secondes = clock->sec + décalageServeur`, que le code des buffs multiplie par 60.

| Élément | Absolu | RVA (`FFXiMain.dll +`) |
|---|---|---|
| pointeur vers la structure d'horloge | `0x060F2E10` | `+0x492E10` |
| → `+0x0C` secondes (u32) | `0x060F2DC4` | `+0x492DC4` |
| → `+0x10` reste en millisecondes (0-999) | `0x060F2DC8` | `+0x492DC8` |
| décalage serveur (**signé**) | `0x06140AF8` | `+0x4E0AF8` |

**Entretien.** Resynchronisé sur **chaque paquet `0x00A`** (zonage) : les secondes locales prennent
`Timestamp 1`, le décalage devient `Timestamp 2 - Timestamp 1`. Juste après un zonage, l'heure du client
vaut donc exactement `Timestamp 2`. Entre deux, elle avance librement depuis `timeGetTime`, avec au plus
±20 ms de rattrapage par appel.

C'est une horloge **tirée** : elle n'avance que lorsqu'on l'interroge. Mais le client l'interroge depuis
~56 endroits à chaque image, donc une lecture passive est au pire vieille d'une image.

**Sûreté.** Le pointeur a un unique écrivain et vise une statique du `.data` : jamais nul après
l'initialisation, jamais réalloué. Deux `safe_read` derrière un `valid_ptr` suffisent.

**Notre époque était juste depuis le début.** Le décalage serveur vaut exactement `-1009810800`, soit notre
constante `EPOCH` au signe près, et notre terme `ERA` vaut `10 × 2^32`, donc nul modulo 2^32. **Seule la
source de « maintenant » était fausse.**

## Cause 2 — le mauvais arrondi

Indépendante de toute horloge, et responsable d'environ la moitié de l'écart.

Les deux fonctions d'affichage du client — `0x05E87AD0` (icône) et `0x05E87BE0` (infobulle) — font :

```
SUB  EDX,ESI     ; delta en ticks
ADD  EDX,0x3B    ; +59
IMUL/SAR         ; /60   ->  CEIL(delta / 60)
```

Un arrondi **au supérieur**. Nous faisions une division entière, donc une troncature : **une seconde de
moins que le jeu**, systématiquement, dès que le delta n'était pas un multiple exact de 60.

D'où `ticks_to_sec_ceil()` dans `party_state.h`, utilisé partout où des ticks deviennent des secondes.

## Autres détails repris du client

- **`0x7FFFFFFF` = « permanent »** : le client ne dessine aucun décompte pour cette expiration. Nous non plus.
- Les expirations sont dans la **même unité** que celles du paquet — ticks absolus de 1/60 s, aucune conversion.

## Disposition mémoire du bloc des buffs self

Le gestionnaire `0x063` sous-type 9 (`FUN_05CF91A0`) recopie 0x30 dwords vers `DAT_060E5904`. Le
constructeur de la liste d'icônes (`0x05E87751`) en fixe la disposition :

- identifiants `u16[32]` à `0x060E5904`
- expirations `u32[32]` à `0x060E5944`

Ce sont les **mêmes valeurs** que celles du paquet — d'où la conclusion que l'erreur ne pouvait venir que de
notre « maintenant », jamais des expirations.

## Point non couvert

Seul le chemin des buffs **self** a été tracé. Les buffs des membres du groupe (`0x076` → `DAT_060E09A0`)
ne semblent porter **aucune durée** — `fields.lua` n'en documente pas. Si un décompte de buff allié est
affiché quelque part, il vient d'ailleurs et mérite sa propre passe.

## Code concerné

`src/model/party_state.cpp` — `ffxi_now_tick()` ; `src/model/party_state.h` — `ticks_to_sec_ceil()`,
`FFXI_EXPIRY_PERMANENT`.
