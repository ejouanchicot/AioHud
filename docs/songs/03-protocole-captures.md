# Captures a faire — ce que seul un log peut trancher

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `sp` : sonde de developpement (`src/plugin/aiohud_probes.cpp`, non suivi par git) — cela repond sur la machine de dev, **jamais dans une release**.
> Ce qu'une release porte vraiment : `//aio help` (et `//aio doctor` en premier quand quelque chose ne va pas).

**Regle du jeu de ces captures : une capture = un cas.** Melanger deux cas dans une fenetre
rend le log inexploitable, parce qu'on ne peut plus rattacher un message a un evenement.

La sonde : `//aio sp [secondes]`. Elle vit dans `aiohud_probes.cpp` (non suivi par git),
branchee apres le vrai dispatch, sous garde SEH. Elle ne decide rien et ne partira jamais
dans une release. Elle ecrit des lignes prefixees `SP`, horodatees en ms depuis l'armement.

**Un rechargement du plugin desarme la sonde.** Armer APRES le deploiement, jamais avant.

---

## Capture 1 — une song expire toute seule

**Faire :** armer quand il reste ~1 minute a une song reelle. Ne rien chanter pendant la fenetre.

**Ce qu'on cherche :** y a-t-il un `0x029` quand elle tombe ? Quel `msg` ? Que vaut `p1` ?

## Capture 2 — un ecrasement volontaire  ← LA PLUS IMPORTANTE

**Faire :** etre au plafond, chanter une song de plus pour en pousser une dehors.

**Ce qu'on cherche :** le serveur emet-il quelque chose pour la song ejectee, ou rien ?

- **Si rien** : le probleme se dissout. On ne recoit aucun message, donc on n'alerte pas.
  Trois regles ajoutees le 2026-09-10 disparaissent sans remplacement.
- **Si un message** : est-il DIFFERENT de celui de la capture 1 ?
  - different -> le serveur distingue lui-meme "perdue" de "remplacee". On applique.
  - identique -> le 0x029 ne suffit pas, il faut une seconde source. A dire franchement.

## Capture 3 — une dissipation

**Faire :** se faire dissiper une song par un mob.

**Ce qu'on cherche :** meme `msg` que la capture 1, ou un troisieme ?

## Capture 4 — le zone

**Faire :** armer, traverser, laisser les listes revenir. Ne rien chanter.

**Ce qu'on cherche :** recoit-on des `0x029` pour ce qui est tombe pendant le chargement,
ou est-ce un trou ou seul l'instantane peut nous resynchroniser ?

## Capture 5 — les trusts dans le 0x076

**Faire :** party avec des trusts, `//aio ftrace` (commande deja existante, pas la sonde).

**Ce qu'on cherche :** un trust apparait-il avec un `mem=` et une liste, ou pas du tout ?
De cette reponse depend "un trust est inverifiable, on n'alerte jamais" — ecrit dans le
code, jamais verifie.

---

## Ce qu'on saura en sortie

| question | tranchee par |
|---|---|
| perte subie vs remplacement volontaire | captures 1, 2, 3 |
| combien de nos regles de comptage sautent | capture 2 |
| faut-il une regle de zone separee | capture 4 |
| un trust est-il "inconnu" ou "vide" | capture 5 |

Ce qui ne sera **pas** tranche par ces captures, et qui restera de l'inference quoi qu'il
arrive : quelle instance de deux songs de meme status vient de tomber (le `0x029` porte un
statut, jamais un sort), et si une song perdue etait recuperable (aucun paquet ne le dit).
