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
| 5 | Aller-retour config et profils | `model/config_rules.h` | 14 | **fait** (28 cas, 10 mordent) -- verifie en jeu |
| 6 | Attribution d'un buff à son lanceur | `model/cast_match.h` | 8 | **fait** (13 cas, 3 mordent) -- verifie en jeu |
| 7 | Zone tracker : Limbus, Omen, Abyssea, Gaol | `model/party_state_zonetracker.cpp` | 5 | partiel (`t_limbus`, `t_omen`) |
| 8 | Roster : ordre, trusts, hors-zone | `model/party_state_roster.cpp` | 3 | à faire |
| 9 | Skillchains et fenêtres | `model/skillchain.cpp` | — | fait (`t_skillchain`) |
| 10 | Groupes de buffs et ordre du bandeau | `model/buff_groups.h` | — | fait (`t_buffgroups`) |
| 11 | Icônes d'équipement : lecture des DAT + cache | `gfx/gear_dat.h` | patch 2026-09-10 | **fait** (`t_geardat`, 161 cas, 5 mutations mordent : 16/4/1/1/1) + **canari en jeu** (`ui/gear_canary.cpp`) -- vérif en jeu à faire |

**Élément 11 : ajouté parce qu'un patch du client a cassé ce que ce harnais excluait par principe.** Le plan
rangeait « les fichiers du jeu » avec les offsets mémoire, dans la moitié que seul le jeu peut juger. Le 2026-09-10
les DAT d'objets sont passés à des enregistrements de 0x1400 octets ; le décodeur supposait 0xC00 et « réussissait »
sur de mauvais enregistrements. Trois jours sans un mot. Deux leçons pour la suite du chantier : une lecture de
fichier du jeu doit **se prouver elle-même** (id et en-tête dans l'enregistrement), et la moitié « en jeu » a besoin
d'un **second témoin** qui ne passe pas par le code testé (empreintes d'icônes compilées, pas le cache disque que la
réparation réécrit). La minimap y gagne `MAP.LOAD_FAILED` et la vérification biSize/bpp de sa branche 8 bits.

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

---

## 7. Element 5 -- l'aller-retour config et profils (2026-09-12)

La decision extraite dans **`src/model/config_rules.h`** (pure : ni fichier, ni device, ni memoire jeu), en deux
fonctions. **28 cas ajoutes a `tests/t_config.cpp`, 10 mutations verifiees**, source mutee -> le test tombe,
source restauree -> il passe.

### Ce que l'element a corrige

1. **Un chargement de profil SUPERPOSAIT au lieu de remplacer.** Une cle absente du fichier gardait la reponse du
   profil precedent, et la sauvegarde suivante l'ecrivait dans le fichier -- deux profils melanges, sans rien
   pour l'expliquer. **Mesure** : `assets/default_profile.txt` porte 106 cles la ou l'ecrivain en emet 130
   (`partyShow`, `allyShow`, `iconpack`, tout le bloc Debuffs, `mm5`, les cinq blocs zone-tracker...). Charger
   « Default » gardait donc le reglage courant pour toutes celles-la. Desormais : **un fichier de config EST une
   config complete**, ce qu'il ne dit pas vaut le defaut.
2. **Le parse construit un brouillon, valide en une seule affectation.** Un chargement qui n'aboutit pas laisse
   la config vivante intacte au lieu d'un etat a moitie par defaut.
3. **« Reset all settings » n'est plus une liste ecrite a la main.** C'etaient ~300 affectations, et le seul champ
   qu'elle avait oublie (`scTP`) a suffi a laisser une ligne TP cachee survivre a une reinitialisation complete.
   **Mesure avant echange** : les 137 litteraux de l'ancienne liste etaient *tous* identiques aux defauts de la
   structure -- l'echange ne change donc rien de visible. Les deux exceptions volontaires sont conservees et
   documentees : `lang` (jamais reinitialise) et les zones du mode edition (un trace de l'utilisateur).
4. **La sanitisation couvre maintenant tous les champs numeriques**, pas 16 sur 101 : styles de texte
   (`size`/`outline` n'etaient bornes nulle part), multiplicateurs, opacites, luminosites, positions, compteurs.
   Les bornes sont **deliberement plus larges que les sliders** : un chargeur ne doit pas *deplacer* une valeur
   qu'un build passe autorisait. Un test dit exactement cela (`sanitise(x) == x` sur tout ce que l'UI produit).
5. **NaN.** `v < lo` et `v > hi` sont tous deux faux pour un NaN : le clamp min/max classique le laisse passer,
   et `sscanf("%f")` lit « nan » depuis un fichier edite a la main. Il repart au **defaut** du champ, pas au
   plancher -- un plancher d'opacite a 0, c'est une boite invisible.

### Ce que le test a trouve des sa premiere execution

**`char epTrack[32] = "briareus"` perdait sa valeur.** MESURE sur cette chaine d'outils (MSVC, `/std:c++17`) :
un initialiseur de membre par defaut sur un **tableau de char** est applique a un objet **local** et **abandonne**
sur un objet de duree de stockage **statique**. `static const UiConfig d{}` sortait avec `epTrack = ""` alors que
tous les autres defauts etaient justes. L'ancien `reset_ui_config` construisait sa reference exactement comme ca :
**« Reset all settings » effacait le NM suivi au lieu de le remettre a son defaut**, depuis toujours. Ecrit sous
forme de liste d'elements (`{ 'b','r',... }`), il survit aux deux. La mutation M9 remet le litteral et le test
tombe.

### Le piege de methode, a garder

La premiere version de l'invariant de reset se servait de `scribble` (ecrit a la main) pour salir la config.
**La mutation M4 -- un reset qui garde volontairement UN champ -- passait.** Normal : `scribble` ne couvre que
les champs auxquels quelqu'un a pense, et le champ que la production avait reellement oublie (`scTP`) etait
justement un de ceux-la. Un invariant bati sur un echantillon ecrit a la main est aveugle exactement la ou le
defaut vit. Corrige par `dirty_every_field()` : on remplit **toute** la structure d'un motif d'octets et on ne
repare que ce qui doit rester structurellement legal pour que `persist_eq` puisse la parcourir (les compteurs
qui bornent ses boucles, les deux chaines qu'elle compare). Ce qui reste intouche garde son 0x5A et tombe,
quel que soit son nom.

### Le seul changement de comportement a annoncer

Au demarrage, `load_ui_config()` charge `config.txt` **puis** re-applique le profil actif. Avec le remplacement,
le profil actif gagne en entier : un reglage change dans l'UI mais **jamais sauve dans le profil** repart au
defaut au prochain lancement, au lieu de survivre. L'ancien comportement ne le preservait de toute facon que
pour les cles que le profil ne portait pas encore -- c'est-a-dire un sous-ensemble arbitraire et inexplicable.
A dire dans les notes de release.

### Verifie en jeu (2026-09-12)

Changement de profil dans les deux sens, « Reset all settings », et une relecture des panneaux : aucun ecart.

Mesure faite sur les profils REELS de la machine de dev avant le test, parce que le remplacement les concerne :
quatre profils portent la totalite des cles, quatre en manquent deux (`buffOrder`, `buffGroupOff`) et
`Tetsouo THF-WAR` en manque quinze (tout le bloc Debuffs, `distcol`, `mm5`, `plreqbox`, `ztaby`...). Ceux-la
repartent donc au defaut pour ces reglages au lieu d'heriter du profil precedent -- c'est le correctif, et le
remede tient en une action : charger le profil puis « Save changes » une fois, il se reecrit complet.

### Reste du a l'element 5 Les entiers de **mode / theme / variante** (~60 champs) restent hors du sanitiseur, volontairement :
chacun demande la lecture de son panneau pour etre borne honnetement, et ils sont gardes a leur site de dessin.

---

## 8. Le pont en jeu — l'autre moitié du harnais (2026-09-12)

Ce document affirmait, §1 et §7, que la moitié « en jeu » est **séparée et le restera** : aucun test ne peut
prouver que `*(g+0x40)+0x02` est la zone, donc rendu, offsets et variance de région étaient laissés à
`//aio doctor` et à un œil devant l'écran.

**C'était vrai tant que le seul témoin était le plugin.** Windower lit les mêmes faits par un chemin
totalement différent — sa propre couche mémoire, ses propres handlers de paquets. C'est donc un **oracle
indépendant**, et là où deux témoins indépendants peuvent être confrontés, un test existe.

### Ce que c'est

| | |
|---|---|
| `scripts/aiotest/aiotest.lua` | un addon Windower **de développement** (jamais livré : `package.bat` exclut `scripts\`). Il lit un fichier de requête, exécute les commandes qu'il nomme, attend, et écrit un instantané de la vision de **Windower**. Il n'affirme rien. |
| `scripts/igtest.py` | le côté qui pose les questions **et qui juge**. Les assertions vivent ici pour être modifiables et rejouables sans recharger l'addon ni le jeu. |

Installation : copier `scripts/aiotest/` dans `<windower>\addons\aiotest\`, puis `//lua load aiotest`.
Les chemins sont dérivés de `deploy.local.bat` — une seule source de vérité avec `deploy.bat`.

**Transport par fichiers, pas socket** : aucun port, aucun pare-feu, ça marche client minimisé ou joueur AFK,
ça survit à un crash des deux côtés, et les deux moitiés se lisent dans un éditeur quand ça tourne mal.
LuaSocket EST disponible (CurePlease et XivParty s'en servent) et pourrait porter une version interactive
plus tard ; le batch n'en a pas besoin.

**Format `clé=valeur` à plat** : la lib json livrée par Windower ne sait que *parser*, pas encoder, et un
encodeur écrit à la main est exactement le genre de code faux pendant une semaine sans que personne le voie.

### Les commandes

```
python scripts/igtest.py ping          le pont répond-il
python scripts/igtest.py snap          tout ce que Windower voit (options : --want, --cmd)
python scripts/igtest.py crosscheck    LA confrontation
python scripts/igtest.py selftest      prouve que chaque contrôle sait ÉCHOUER
python scripts/igtest.py watch         crosscheck en boucle, pour une soirée de jeu
```

### Les cinq premières confrontations, et pourquoi celles-là

| Contrôle | Ce qu'aucun test hors ligne ne pouvait dire |
|---|---|
| **zone** | l'offset que le plan citait comme l'exemple même de l'inprouvable, et qu'un patch client déplace |
| **self id** | la clé de tous les filtres « à moi / pas à moi » et du nom du fichier de cache |
| **main job** | pilote la liste de buffs suivis et le profil automatique |
| **taille du roster** | nous parcourons un tableau de membres ; eux interrogent leur propre structure de party |
| **nombre de buffs** | notre `u16[32]` terminé par `0xFF` contre leur liste |

Première exécution, 2026-09-12 : **5 passés, 0 échoué**, et la zone a bougé (248 → 230) entre deux mesures —
ce qui prouve que la comparaison porte sur du frais et non sur un cache.

### `selftest` : l'étape 3 appliquée au pont lui-même

Un test vert qui n'est jamais passé au rouge ne prouve que son exécution. `igtest.py selftest` prend **une**
mesure réelle, puis casse chaque comparaison à son tour et exige un `FAIL`. Un contrôle qui reste vert avec une
valeur qui ne peut pas correspondre est **décoratif** — et c'est pire qu'absent, puisqu'il est compté comme de
la couverture. Résultat du 2026-09-12 : **5 contrôles sur 5 mordent.**

### Ce que le pont ne fera jamais

Juger un pixel (un flou, une ligne noire, un glow qui bave) et fabriquer une situation qui demande d'être
quelque part (Abyssea, Odyssée, un vrai combat). Ce soir a montré que cette moitié compte : aucun test hors
ligne n'aurait vu des bouts de barre devenus carrés — seul un œil l'a vu, en une minute.

### Ce qu'il rend possible ensuite

L'addon peut **agir** (`windower.send_command`), donc la forme suivante est un scénario rejouable : lancer un
sort, attendre, et confronter la durée que le HUD affiche à celle que le serveur a réellement donnée. C'est le
module qui a coûté le plus de bugs au projet (timers de buffs alliés, songs), et il se teste aujourd'hui en
jouant une soirée.
