# Les quatre veilleurs — 2026-09-11

Ce que j'ai construit dans la nuit du 10 au 11 septembre, apres deux journees passees sur des bugs qui
auraient du prendre dix minutes. **A lire en premier si un de ces outils se trompe : la section
« Si ca deraille » a la fin.**

---

## Pourquoi

Les bugs de ces deux jours n'etaient pas difficiles. Ils etaient **invisibles**, et chacun l'etait pour une
raison differente :

| Ce qui s'est passe | Pourquoi personne ne l'a vu |
|---|---|
| Deux songs echangeaient leur place deux fois par seconde | Une image isolee est correcte. Il faut regarder des images **consecutives**. |
| La liste de surveillance des Timers tenait 24 entrees, un barde en genere plus | Une table pleine et une table vide se ressemblent **de l'exterieur**. |
| Une adresse du client a bouge, trois fonctions sont mortes | Le diagnostic etait dans le log **depuis des jours**. Personne ne lit le log. |
| Le modele des songs choisissait la mauvaise | Le resultat etait visible, **la raison** ne l'etait pas. |

Quatre angles morts, quatre outils. Aucun ne corrige quoi que ce soit : ils **regardent**.

---

## 1. `flipwatch` — une decision qui n'arrive pas a se decider

`src/model/flipwatch.h` · `flipwatch("id", valeur, now)`

Une valeur qui fait A, B, A, B, A, B n'est pas une valeur qui bouge, c'est **une dispute**. Six alternances
sans jamais se poser : il le dit **une fois**, par son nom, dans le log et dans `//aio doctor`.

Il n'accuse pas ce qui change normalement — un decompte, une liste qui grandit, un curseur qui se promene ne
reviennent jamais sans cesse sur la valeur qu'ils viennent de quitter. Et deux secondes d'immobilite effacent
l'historique.

**Branche sur** la signature des lignes de Timers, et sur chacune des six adresses FFXiMain.

## 2. `capwatch` — une table qui a discretement deborde

`src/model/capwatch.h` · `capwatch("id", utilises, capacite)`

Le projet n'alloue rien par image : tout est un tableau de taille fixe, et **deborder est totalement
silencieux**. Ce qui ne rentre pas n'existe simplement plus.

Il dit deux choses, une fois chacune :
- **bientot pleine**, avec le pic historique — la capacite se leve *avant* la soiree ou elle deborde ;
- **saturee**, apres etre restee au plafond assez longtemps pour que ce ne soit pas un simple zone-in.

**Branche sur** la liste de surveillance des Timers (elle tenait 24) et la table des buffs allies (elle tenait
32, et elle **ejecte la plus ancienne** quand elle est pleine, ce qui est une perte de donnees muette).

## 3. Les diagnostics passifs — ce que le doctor savait deja dire

`//aio doctor` est excellent, et il a un seul defaut : **il ne parle que si tu le soupconnes**. Le compteur
d'Odyssey est reste a 0 pendant une run entiere de 105 paiements alors que le diagnostic etait disponible tout
du long.

Les memes constats tournent maintenant **tout seuls** pendant que tu joues (registre `selftest`) :
- `ZT.MSGID_SILENT` — la zone parle et notre id ne capte rien : une maj a renumerote le message ;
- `ZT.ABYSSEA_BASE` — rien de reconnu sur douze messages : la base a bouge (celle-la ne se devine pas) ;
- `LC.ROOT_LOST` — la racine LuaCore ne repond plus **alors qu'un personnage est connecte**. Cette
  contradiction-la ne peut pas etre l'ecran de login, donc elle peut se declencher sans hesiter ;
- plus les cinq verifications RVA et celles des deux veilleurs ci-dessus.

## 4. `dec_record` — garder le POURQUOI

`src/model/decisions.h` · `dec_record("sujet", "...")` · dump : **`//aio why`**

Les traces existantes (`dbflog`, `ftrace`, `tpool`, `songlog`) ont toutes le meme defaut : il faut les avoir
armees **avant**. En pratique tu remarques le bug d'abord, et a ce moment la preuve est perdue.

Celle-ci enregistre en permanence dans un anneau de 256 lignes. Rien a armer, donc rien a avoir oublie.
C'est exactement ce qui a debloque la chasse aux songs — `//aio songdump`, apres coup, en une capture.

**Branche sur** le choix du mode de zone, les messages Abyssea qui ne tombent nulle part, et l'effacement d'un
debuff sur la cible.

- `//aio why` — tout
- `//aio why zt` — les zones · `//aio why dbf` — les debuffs
  *(« zt » et « dbf » et non « zone » / « debuff » : `aiohud_probes.cpp` passe avant et possede deja un
  `//aio zone`. La commande marcherait en release et pas en dev, ce qui est la pire des differences.)*

---

## Ou sont les boutons

**Tous les reglages sont dans `src/model/watchdogs.h`.** Un seul fichier, chaque nombre commente avec son
raisonnement. C'est le seul endroit a ouvrir.

En jeu, sans recompiler :

```
//aio watch          ->  etat : combien de decisions surveillees, combien oscillent,
                         combien de tables surveillees, combien sont saturees
//aio watch off      ->  tout s'arrete d'observer, immediatement
//aio watch on       ->  ca repart
//aio why [zt|dbf]   ->  vide l'anneau des decisions dans aiohud_debug.log
```

## Si ca deraille

Par ordre de probabilite :

1. **Un veilleur crie au loup.** Monte son seuil dans `watchdogs.h`, recompile. S'il crie encore :
   **supprime le point d'appel**. La regle de `selftest.h` est qu'une verification qui se trompe se
   *supprime*, elle ne se *regle* pas — le bruit enterre le seul rapport qui comptait.
2. **Un veilleur est muet alors qu'il ne devrait pas.** Baisse le seuil, ou verifie que le point d'appel est
   bien atteint.
3. **Quelque chose ne va pas et tu soupconnes un veilleur.** `//aio watch off` en jeu. Pas de recompilation,
   pas de rechargement. Si le probleme persiste, ce n'etait pas eux — ils n'ecrivent dans aucun etat que le
   HUD lit, ils ne font que regarder.
4. **Tout jeter.** Les quatre tiennent dans six fichiers (`watchdogs`, `flipwatch`, `capwatch`, `decisions`
   en `.h`/`.cpp`) plus une dizaine de lignes d'appel. Un `git revert` du commit suffit.

## Ce qui est prouve

`tests/t_flipwatch.cpp` et `tests/t_capwatch.cpp` couvrent les deux decisions pures. Les cas qui les gardent
honnetes — se poser efface le compte ; une nouvelle paire gagne ses six toute seule ; effleurer le plafond
pendant un zone-in n'est pas une saturation ; le pic est retenu apres que la table se vide — ont ete **verifies
par mutation** : source mutee, le test tombe ; source restauree, il passe.

## Ce qui reste ouvert

- Aucun des trois nouveaux points de `dec_record` n'a encore tourne en jeu. Le premier `//aio why` apres une
  zone dira s'ils sont poses au bon endroit.
- `capwatch` n'est branche que sur deux tables. Il y en a une quinzaine d'autres (`ANIM_MAX`, `MAP_ENT_MAX`,
  `BUFF_PIN_MAX`, `TM_TRACK_MAX`...) ; celles-ci sont les deux qui ont deja deborde.
- La question posee trois fois et toujours sans reponse : **« on veut les recast »** — le temps de
  rechargement du sort, ou un rappel avant qu'une song tombe ?
