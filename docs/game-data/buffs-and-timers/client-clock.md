---
title: The client clock — "now" for buff countdowns
summary: How the client computes "now" for buff timers (a pulled monotonic clock + signed server offset, resynced on 0x00A) and the CEIL(delta/60) rounding — the two fixes that made AioHUD's countdowns match the game to the second.
source: Ghidra decompile of FFXiMain (re/ffximain_dump.bin), 2026-07-20; model/party_state.cpp ffxi_now_tick(), model/party_state.h ticks_to_sec_ceil()
---
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

**Les deux adresses bougent à chaque patch du client (constaté 2026-11-28).** Elles étaient des constantes
(`0x492E10` / `0x4E0AF8`, le client de juillet) ; le client de novembre n'y fait **aucune** référence (0 occurrence
dans `re/ffximain_dump_20261115.bin`, contre 38 et 6 dans le dump de juin) : depuis un patch, les timers étaient
retombés en silence sur l'horloge du PC. Le portage de chaque référence du dump de juin vers celui de novembre (motif
autour de l'instruction, adresses masquées ; témoin : le pointeur mentor `0x4DFD98` → `0x4E0F48`, 532 votes sur 532)
donne **`0x493FC0`** (27 votes sur 27) et **`0x4E1CA8`** (4 sur 4).

Elles sont désormais **retrouvées au chargement** par `client_clock_rvas()` (`model/native_ui.cpp`), depuis la petite
fonction « maintenant » du client :

```
E8 <getter> 03 05 <OFF> C3 90 90 90 90  83 EC 08 E8 .. 89 44 24 00 C7 44 24 04 00 00 00 00
DF 6C 24 00 D8 0D .. D8 05 .. E8 .. 03 05 <OFF> 83 C4 08 C3        (FFXiMain+0x103910 juin, +0x103B30 novembre)
getter = stub CALL x ; JMP y  ->  A1 <PTR> 8B 40 0C C3              (sec = (*PTR)->+0x0C)
```

unique dans les deux dumps, adoptée seulement si le stub et le getter ont exactement cette forme et que les deux
`<OFF>` concordent. `ffxi_now_tick()` (`party_state.cpp`) part des **graines** `CLK_PTR_SEED = 0x493FC0` /
`CLK_OFF_SEED = 0x4E1CA8` tant que la recherche n'a pas abouti (retentée toutes les 5 s). Log : `CLOCK client clock
resolved` / `NOT resolved`, puis `CLOCK timers on the client's own clock` / `on the PC clock` à **chaque changement**
de source.

Si l'horloge est illisible ou pas encore initialisée (`sec == 0 && off == 0`, avant le premier `0x00A`), elle retombe
sur l'ancienne formule `time - EPOCH - ERA` : les décomptes restent vivants, avec le décalage de 2-3 s décrit plus
haut. Le résultat est mis en cache une fois par milliseconde.

## See also
- [Timers](timers.md) — the self-buff/recast countdowns that consume this clock.
- [0x028 action packet](../actions/action-packet.md) — the sibling client-parser trace from the same 2026-07-20 Ghidra pass.
- [Moon phases](../reference-sheets/moonphase.md) — the other Vana'diel-time readout.
- [FFXiMain statics](../ffximain-statics.md) — the self-healing registry ; the clock heals itself by its own signature instead (client_clock_rvas).
