# Harnais de sécurité — plan et suivi

**Objectif, dans les mots du projet :** si demain quelque chose ne marche plus, on a de quoi comprendre pourquoi ;
et si on committe un changement, les tests se lancent et vérifient qu'on n'a rien cassé.

Ce document est **suivi par git** (contrairement au reste de `docs/`) parce qu'il est un état d'avancement :
il doit survivre aux sessions et se lire par n'importe qui reprend le chantier.

---

## 1. L'ampleur, mesurée

20 101 lignes de code écrites à la main, sur 52 fichiers de plus de 60 lignes.
Le taux de lignes touchant le dessin va de 0 à 36 % : **l'essentiel est de la logique**, donc extractible.

Ce qui n'est PAS concerné, et ne le sera jamais :

- le rendu (un flou, une ligne noire, un glow qui bave) — il faut un device et des yeux ;
- les offsets mémoire — seul le jeu dit s'ils sont justes ;
- les écarts de région ou de chemin d'installation.

Pour ceux-là, le harnais en jeu (`//aio doctor`, le veilleur, les sondes) est la réponse, pas les tests unitaires.

---

## 2. La recette, par élément

Un élément est **fait** quand ces cinq étapes sont franchies. Pas avant.

1. **Extraire la décision** en fonction pure, dans son propre en-tête, sans device, sans mémoire jeu, sans
   horloge implicite. Tout ce dont elle a besoin lui est passé.
2. **Écrire les cas depuis des captures réelles**, pas depuis l'imagination — horodatages inclus quand le
   défaut portait sur un ordre d'événements.
3. **Prouver que les tests mordent** : remettre le défaut d'origine et vérifier qu'ils tombent. Un test qui n'a
   jamais échoué ne prouve que sa compilation.
4. **Brancher la production** sur la fonction extraite, et vérifier qu'aucune copie de la règle ne subsiste.
5. **Vérifier en jeu** que le comportement n'a pas bougé, avec la sonde du module.

L'étape 3 est celle qu'on est tenté de sauter. C'est celle qui distingue un harnais d'une décoration.

---

## 3. L'ordre, dicté par où les bugs sont réellement

Compté sur l'audit complet du 2026-08-06 (41 constats), par fichier incriminé.

| # | élément | fichier | constats | état |
|---|---|---|---|---|
| 1 | Slot d'un song allié (garder / retirer) | `model/song_slot.h` | — | **fait** (15 cas, 4 mordent) — vérifié en jeu |
| 2 | Groupement AoE et éclatement en retard | `model/ally_group.h` | 20 | **fait** (23 cas, 10 mordent) -- verifie en jeu |
| 3 | Modèle de durée d'un song / buff allié | `model/song_dur.h` | — | partiel (`t_durations`) -- **confirmé en jeu** (m1 potence, m2 Troubadour x2, a3 Marcato +20) |
| 4 | Moniteur FOCUS : mute, oubli, 5e song, songs jumelles | `model/focus_rules.h` | 20 | **fait** (41 cas, 11 mordent) -- verifie en jeu, **6 defauts trouves** |
| 5 | Aller-retour config et profils | `model/ui_config.cpp` | 14 | partiel (`t_config`) |
| 6 | Attribution d'un buff à son lanceur | `model/cast_match.h` | 8 | **fait** (13 cas, 3 mordent) -- verifie en jeu |
| 7 | Zone tracker : Limbus, Omen, Abyssea, Gaol | `model/party_state_zonetracker.cpp` | 5 | partiel (`t_limbus`, `t_omen`) |
| 8 | Roster : ordre, trusts, hors-zone | `model/party_state_roster.cpp` | 3 | à faire |
| 9 | Skillchains et fenêtres | `model/skillchain.cpp` | — | fait (`t_skillchain`) |
| 10 | Groupes de buffs et ordre du bandeau | `model/buff_groups.h` | — | fait (`t_buffgroups`) |

La colonne d'état donne le nombre de cas et, après « mordent », combien tombent quand on remet le
défaut d'origine — c'est cette seconde valeur qui dit si le test protège quelque chose.

Les lignes « partiel » ont des tests qui existent mais ne couvrent pas la décision au sens de la recette
ci-dessus : `ui_config.cpp` est déjà dans la suite **et** porte 14 constats, ce qui est l'avertissement le plus
utile de ce tableau — des tests présents ne suffisent pas s'ils n'encodent pas les incidents.

---

## 4. Ce qui tourne déjà, et quand

| couche | déclenchement | ce qu'elle attrape |
|---|---|---|
| garde-fous de compilation | chaque build | conventions, latchs règle 10, `%f` dans un log |
| suite hors-ligne (`tests.bat`) | **chaque commit** (`.githooks/pre-commit`) + chaque push (CI) | régressions dans le code couvert |
| vérification de la release | chaque tag | zip cassé, sidecar manquant, addon non parsé |
| veilleur en jeu | toutes les 30 s, si armé | ce que seul le jeu sait, et il écrit un rapport |

Le hook est versionné dans `.githooks/` : `git config core.hooksPath .githooks` une fois par clone.

---

## 5. Coût

Environ une demi-journée par élément du tableau, extraction et vérification comprises — davantage pour le n° 2
et le n° 4, qui vivent dans la fonction la plus dense du projet.

Il n'y a pas de raccourci honnête : la valeur vient de l'étape 3, et l'étape 3 demande de retrouver le défaut
d'origine. Un élément livré est utile seul, donc le chantier peut s'arrêter à n'importe quelle ligne du tableau
sans rien laisser d'inachevé.

---

## 6. Ce que la verification en jeu a rapporte (2026-09-10)

L'etape 5 n'est pas une formalite : la premiere seance de verification a trouve **deux defauts** dans un element
deja marque « fait », tous deux dans la meme regle, et aucun des deux n'etait visible hors ligne.

**Le contexte, corrige par le joueur.** Clarion Call **ouvre** le 5e slot de song, il ne le maintient pas : la 5e
song survit a la fin du buff et se rechante par-dessus sans CC. Le slot ne se referme qu'en **perdant** cette
song. Les deux regles ci-dessous etaient bâties sur l'hypothese inverse.

**Defaut 1 — une heure de silence achetee avec un SP2.** `song_unrecoverable` n'exigeait que `songCount >= base`
et Clarion Call en recast. Or `songCount` compte les sorts distincts vivants sur **tous** les allies : un allie
dissipe ne le fait pas bouger tant qu'un autre porte la song. La condition tenait donc pendant tout le recast, et
**chaque** perte de song sur un allie etait supprimee -- alerte annulee, entree du moniteur liberee, sans un mot.
C'est la regle 10 dans sa forme la plus pure. Mesure : six songs vivantes, six `unrecov=1`.
Corrige par `stillOnYou` (ton `0x063`) : perdre la song **sur toi**, c'est ca, le slot qui se referme.

**Defaut 2 — la base apprise pouvait etre empoisonnee.** `clarion_learn` n'etait protege que pendant le recast.
En rechantant la 5e song pendant ce recast on arrive a « Clarion Call pret, 5 songs », que l'ancienne porte lisait
comme une fenetre honnete : `base = 5` pour la session. Corrige par un verrou `slotOpen`, libere seulement par une
liste de songs **vide**.

**Ce qu'on ne testera pas en seance dediee.** Clarion Call, c'est une heure de recast. Plutot qu'attendre, chaque
`//aio oblog` imprime desormais une ligne `SONGSLOT count= base= valid= slot= ccUp= ccRecast=` : la reponse tombe
d'un combat ordinaire. C'est la forme generale a preferer -- rendre l'etat observable la ou le joueur passe deja,
plutot que lui demander une manipulation qu'il ne fera qu'une fois.


**Defaut 3 — une song sur deux n'etait surveillee par personne.** Un barde tient Valor Minuet IV *et* V, ou deux
Marches : des sorts DIFFERENTS sur un MEME statut. Le moniteur gardait une entree par (personne, statut), donc la
seconde ecrasait la premiere -- la perdre ne levait aucune alerte, la ligne disparaissait simplement. Mesure : 4
songs, 6 entrees, Minuet V surveillee nulle part.

Corrige en trois temps : une entree par (personne, statut, **sort**) ; la presence se **compte** au lieu de se
tester (le `0x076` ne nomme pas les sorts, mais il dit combien d'exemplaires : 2 entrees / 1 exemplaire = une est
partie) ; et l'emission lit le meme verdict que l'elagage, au lieu d'en refaire une troisieme copie.

Le premier jet de ce correctif s'est trompe de cle -- il classait les soeurs par la naissance de l'entree, alors
qu'une entree est REUTILISEE a chaque re-cast et peut dater de vingt minutes. Resultat en jeu : le modele gardait
Minuet V, le moniteur gardait Minuet IV. Le classement utilise desormais le CAST (`startMs` cote allie, expiration
cote soi), c'est-a-dire exactement ce que compare l'elagage. **Verifie en jeu le 2026-09-10** : un seul `198` chez
la cible, les deux etages nomment la meme song, et l'alerte sort pour l'autre.


**Defaut 4 — une alerte par personne pour un seul evenement.** Une song AoE s'affiche GROUPEE tant qu'elle tient
(`Valor Minuet V (AoE 3)`) et devenait une ligne rouge PAR PERSONNE des qu'elle tombait : deux ici, **six** en
party pleine, pour un seul fait. Les deux cas veulent l'inverse l'un de l'autre, et le compte les separe : tout le
monde la perd d'un coup -> c'est la SONG qui est finie, une ligne suffit ; une seule personne la perd -> c'est une
dissipation, et le nom est tout l'interet de l'alerte. Une alerte groupee ne porte pas de numero, meme raison
qu'une ligne saine groupee. La chaine des six suppressions n'a pas bouge : la boucle ne fait plus que COLLECTER
qui alerte, le regroupement se fait apres, quand on voit l'ensemble.

**Defaut 5 — une liste d'avant le zone decidait de ce qui avait survecu au zone.** Le cache `0x076` n'est pas vide
au changement de zone : il garde le dernier jeu recu. `listReady` ne verifiait que la non-nullite du pointeur, donc
il repondait « prete » sur un contenu pre-zone. Le controle post-zone y voyait les songs, concluait qu'elles
avaient survecu, repassait en surveillance normale -- et la vraie liste, en arrivant, faisait une alerte rouge par
song et par allie. Corrige via `stampMs` : une liste anterieure au zone ne decide plus rien. C'est la regle que
l'elagage applique deja (`model/song_slot.h`) ; `listReady` etait le seul endroit qui l'enfreignait. Avec un
garde-fou regle 10 : si aucune liste fraiche n'arrive apres 60 s, on reprend le cache **et on l'ecrit dans le
log** -- une attente ne doit pas devenir un silence permanent, et surtout pas discrètement.
**Verifie en jeu 2026-09-10, dans les deux sens** : plus d'alerte fantome au retour de zone (0 entree contre 12
avant), et une vraie perte alerte toujours, nommee. Le second sens est le seul qui compte vraiment : faire taire un
faux positif en tuant le vrai positif aurait ete un moins bon echange que le bug de depart.


**Defaut 6 — une base non encore apprise etouffait les alertes.** La base apprise ne survit ni a un changement de
job ni a un rechargement du plugin : elle repart de zero et remonte une rotation a la fois. Or la regle disait
« AU MOINS la base » -- avec base = 1 et quatre songs, perdre l'une d'elles donnait `3 >= 1` et etait supprimee.
Le defaut 1, rendu par un simple rechargement. Mesure 2026-09-10 (`SONGSLOT count=1 base=1` apres un reload).
Le sens reel de la regle est « la song EN TROP est partie, et rien d'autre » : tu tenais base+1, tu en perds une,
tu es EXACTEMENT a la base. C'est desormais `== base`, ce qui rend une base sous-evaluee inoffensive.

**Restent a jouer en jeu** (les captures precedentes ne les ont pas exercees) :

| manip | ce qu'elle vise | pourquoi elle a rate la premiere fois |
|---|---|---|
| chanter PUIS appeler un trust | le retardataire (element 2) | **fait 2026-09-10** : aucune entree fantome, `(AoE 2)` juste |
| rechanter, puis Troubadour | le remplacement (element 1) | **fait 2026-09-10** : une seule ligne, reestampillee, 355s -> 730s |
| laisser un trust te buffer | l'attribution au lanceur (element 6) | aucun buff lance par un trust dans la capture |
| rechanter la meme song, puis sous Troubadour | le remplacement (element 1) | la bande `songtape` s'etait refermee avant |

Note utile pour la relecture : il n'existe **aucun** chemin qui credite retroactivement un membre arrive apres un
cast -- les entrees ne naissent que de la liste de cibles du paquet d'action (`party_state.cpp:1330-1360`). Un
membre qui porte une song la portait deja au moment ou elle a ete chantee.
