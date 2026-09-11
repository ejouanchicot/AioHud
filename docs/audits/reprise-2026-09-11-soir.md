# Reprise — 11 septembre 2026, apres les veilleurs

Etat du depot : **`main` propre, tout pousse**, dernier commit `e96fa0b` — **v1.0.85 publiee**.
Build OK, suite offline **1088 checks, 0 failed**, hook pre-commit actif.

Le document de fond est [`outils-anti-regression-2026-09-11.md`](outils-anti-regression-2026-09-11.md) —
a lire en premier. Celui-ci ne dit que ce qui est arrive **apres** sa redaction.

---

## Ce qui a ete livre aujourd'hui

Sept commits, du plus ancien au plus recent :

| Commit | Quoi |
|---|---|
| `8a1cee4` | `flipwatch` — une decision qui n'arrive pas a se decider |
| `af792f1` | `capwatch`, diagnostics passifs, `dec_record` + le fichier de reglages unique |
| `ce67b32` | **la regle anti-leurre refaite** (voir plus bas) |
| `6e1fe23` | ce que les veilleurs ont trouve le jour meme |
| `aef63a9` | les recast etaient deja livres — fausse question ouverte, close |
| `41fafcd` | **`RVA.MENU_DECOY` supprimee** — faux positif |
| `a1b0549` | le log ne crie plus « unproven » sur un pointeur qui marche |

Les quatre veilleurs, en une ligne chacun :

- **`flipwatch`** — une valeur qui fait A,B,A,B est une dispute, pas un mouvement.
- **`capwatch`** — une table fixe qui deborde est totalement silencieuse.
- **diagnostics passifs** — ce que `//aio doctor` savait deja dire tourne maintenant tout seul.
- **`dec_record`** + `//aio why` — enregistre en permanence, contrairement aux traces qu'il faut armer avant.

**Tous les reglages sont dans `src/model/watchdogs.h`. `//aio watch off` les coupe en jeu, sans recompiler.**

---

## LA lecon de la journee

Les veilleurs ont trouve **trois defauts, tous les trois de moi**, et **une seule fausse idee** derriere les
trois :

> **Un nom de leurre (`inline` / `logwindo`) ne prouve rien du tout.**
> Le bon slot lit `inli` tant que la saisie du chat a le focus. Un leurre le lit toujours. Les deux cas sont
> indiscernables par cette seule observation. Seul un **vrai nom de menu** les separe — et un leurre n'en
> montre jamais.

Ce qu'elle a casse :

1. **La regle d'execution** — elle refutait la bonne adresse apres une seconde d'inactivite, la cost box
   mourait dans chaque trou. Quatre cycles CONFIRMED / unproven / re-CONFIRMED **a la meme adresse**.
2. **La verification `RVA.MENU_DECOY`** — elle comptait `inli` comme « un menu ouvert », donc se connecter et
   laisser le chat ouvert 90 s levait un **BLOCK** sur un pointeur parfaitement sain. Accusation **garantie,
   pas meritee**. Supprimee, pas reglee (regle de `selftest.h`).
3. **`flipwatch` lui-meme** — je lui donnais `g_rva[i]`, or l'adresse n'a jamais bouge. Ce qui oscillait,
   c'etait **si on y croit**. Il a rate exactement la forme pour laquelle il existe.

Trois regles a garder :

- **Une refutation doit peser plus lourd que la preuve qu'elle renverse.** (Notee le 10, refaite le 11.)
- **Un veilleur branche sur le mauvais nombre est pire qu'aucun veilleur** : il donne l'impression d'etre
  couvert. Quand un outil reste muet, verifier ce qu'on lui donne avant de conclure que tout va bien.
- **Ne rien demolir pour chercher mieux.** La correction n'a pas ete un seuil, mais la forme : une adresse
  prouvee est definitive, une adresse non prouvee **reste en service** pendant que la recherche tourne
  dessous, un slot qui fait ses preuves prend la place. Il n'y a plus d'instant ou rien n'est branche — donc
  l'oscillation est **impossible**, pas seulement improbable.

---

## Verifie en jeu (client reel, pas une deduction)

- `//aio why` apres un chargement : `zone 241 'Windurst Woods' (from -1) -> mode 0`. Correct.
- Dix secondes avec la saisie du chat ouverte : **zero ligne de soin** la ou il y en avait quatre cycles.
- **La cost box s'affiche** — c'est le vrai nom de menu qui rend le verdict definitif pour la session.
- Connexion sur un second personnage (Kaories) : plus aucun rapport de bug genere.

Les trois `aiohud_bugreport_*.txt` du 11/09 sont **a jeter** — aucun ne decrivait un vrai probleme.

---

## Ce qui reste ouvert

1. **`dec_record` Abyssea et debuffs** n'ont pas encore eu l'occasion de s'ecrire — il faut aller en Abyssea,
   ou se battre. **Leur silence est l'etat sain** : ils n'ecrivent que quand quelque chose ne correspond a
   rien. Rien a verifier.
2. **`capwatch` n'est branche que sur deux tables** (`FOCUS_MAX`, `OB_MAX`) — celles qui ont deja deborde. Il
   en reste une quinzaine (`ANIM_MAX`, `MAP_ENT_MAX`, `BUFF_PIN_MAX`, `TM_TRACK_MAX`...). A brancher au fur
   et a mesure, pas en bloc.
3. ~~Pas de release depuis la v1.0.84.~~ **Publiee le 11/09 a 15h02 : v1.0.85** (`e96fa0b`, tag `v1.0.85`,
   CI verte, `AioHud-1.0.85.zip` + `AioHudIcons.exe`, marquee Latest). Trois lignes de changelog : la boite de
   cout qui clignote, le rapport de bug non merite, et les quatre veilleurs avec `//aio watch off`.
   `RELEASES_N` = 65 contre `relOpen_[128]`. L'addon n'a pas bouge, donc **pas** de `//lua reload aioupdate`
   a faire cette fois.
4. **Questions BRD jamais mesurees** : Tenuto + Clarion Call ensemble, un terme plat ou deux ? Et un ecrasement
   volontaire emet-il un `0x029` ? (`docs/songs/03-protocole-captures.md`, capture 2.)

**Close, ne pas rouvrir :** « on veut les recast ». C'etait un constat, pas une demande — les recasts sont
dans Timers depuis longtemps (`TM_KEY_RECAST` dans `ui_config.h`, resolution du nom dans `hud_timers.cpp`).
Je l'ai reclame trois fois au lieu d'aller lire le code.

---

## Les commandes utiles

```
//aio watch          etat des veilleurs (combien surveilles, combien oscillent, combien satures)
//aio watch off      tout s'arrete d'observer, immediatement, sans recompiler
//aio why [zt|dbf]   vide l'anneau des decisions dans aiohud_debug.log
//aio doctor         le diagnostic complet, avec le remede de chaque probleme
//aio rva            l'etat des six adresses FFXiMain + un bloc de seeds pret a coller
```

Les sujets de `//aio why` sont **`zt`** et **`dbf`**, pas « zone » / « debuff » : `aiohud_probes.cpp` passe
avant et possede deja un `//aio zone`.
