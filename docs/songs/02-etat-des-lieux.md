# Songs dans Timers — etat des lieux au 2026-09-10

**A quoi sert ce document.** Arreter de corriger au symptome. Il dit ce qui existe deja,
ce qui a ete casse aujourd'hui, et ce qu'il faut savoir avant de reecrire quoi que ce soit.

---

## 1. Ce qui existait deja, et que j'aurais du lire

`docs/architecture/timers-songs-brd.md` (210 lignes, 2026-07-23) est la spec de reference.
Elle pose le modele a 4 sources, le split frais/retardataire, la collision de status 214,
les bandes d'ordre d'affichage, et les 5 suppressions d'alerte.

`docs/architecture/song-timers.md` (157 lignes) documente la sonde `//aio songrow`.

**Son paragraphe 9 liste 5 points non tranches depuis juillet.** Ils n'ont pas ete tranches
depuis. Plusieurs des bugs du 2026-09-10 tombent dedans.

## 2. Ce qui a ete ajoute le 2026-09-10, et qui est fragile

Sept corrections en une journee, chacune declenchee par un symptome, chacune ajoutant une regle :

| # | regle ajoutee | ou | repose sur |
|---|---|---|---|
| 1 | `stillOnYou` dans `song_unrecoverable` | `focus_rules.h` | ton propre 0x063 |
| 2 | verrou `slotOpen` dans `clarion_learn` | `focus_rules.h` | **le compte de songs** |
| 3 | entree FOCUS par (personne, statut, **sort**) | `hud_timers.cpp` | — |
| 4 | presence par **comptage** d'exemplaires | `focus_rules.h` | le 0x076 |
| 5 | alertes OUT regroupees | `hud_timers.cpp` | — |
| 6 | `listReady` refuse une liste d'avant le zone | `hud_timers.cpp` | `stampMs` |
| 7 | `song_evicted` (ejection au plafond) | `focus_rules.h` | **le compte de songs** |

Les regles 2 et 7, plus `song_unrecoverable`, comparent `songCount` a un plafond appris.

## 3. Le defaut de fond

**`songCount` ne compte pas les fake songs.** La boucle qui le calcule saute tout ce dont
`song_family()` vaut 0 — donc Gold Capriccio et Goblin Gavotte, precisement les songs
chantees POUR occuper un slot.

Consequence : le nombre compare au plafond n'est pas le nombre de slots occupes.
Tout l'etage "plafond" est bati sur un compte faux.

**Et le plafond lui-meme est appris par observation**, alors qu'il depend de l'instrument
equipe — donc il apprend le maximum sous Daurdabla et le garde apres le switch.

## 4. Ce qu'il faut avant de reecrire

Trois sources de verite, dans cet ordre :

1. **Les regles du jeu** — `01-questions-mecaniques.md`. Personne ici ne les connait avec
   certitude ; le wiki les connait. Sans elles on continuera d'inventer.
2. **Ce que le serveur emet** — `03-protocole-captures.md`. Aucun wiki n'y repond ;
   seul un log le dit. La question qui decide de l'architecture : un ecrasement volontaire
   emet-il un message, ou rien du tout ?
3. **Nos choix deliberes** — ce que la spec de juillet a decide, et ce que son paragraphe 9
   n'a jamais tranche. Les fake songs masquees en sont un : il n'etait ecrit nulle part.

## 5. Ce qu'on ne refera pas

- Pas de correction au symptome. Un symptome ouvre une question, pas un patch.
- Pas de nouvelle constante de temps ("fenetre de 6 s", "grace de 8 s") sans mesure.
- Pas de regle ajoutee a cote d'une autre : si deux regles peuvent se contredire,
  c'est qu'il en faut une seule, en amont.
