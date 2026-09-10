# Ce que j'ai compris du systeme de songs — a confronter aux vraies sources

**Statut : NON VERIFIE.** Ce document n'est pas une specification. C'est le modele mental
avec lequel j'ai code le 2026-09-10, ecrit pour etre **contredit**. Chaque affirmation porte
sa source et son degre de certitude, pour qu'un lecteur — humain ou autre IA — puisse la
verifier ligne a ligne sans avoir a deviner d'ou elle sort.

## Comment lire

| marque | signification | fiabilite |
|---|---|---|
| `[CODE]` | lu dans le depot, fichier:ligne donne | haute — mais dit ce que le code FAIT, pas ce qui est JUSTE |
| `[MESURE]` | observe dans un log de cette session, extrait cite | haute pour le fait, faible pour la generalisation (souvent une seule observation) |
| `[DIT]` | affirme par le joueur en session | haute — c'est lui l'expert du jeu |
| `[SPEC]` | ecrit dans `docs/architecture/timers-songs-brd.md` (2026-07-23) | moyenne — jamais reverifie depuis |
| `[SUPPOSE]` | **mon hypothese, jamais verifiee** | **nulle. Ce sont ces lignes qu'il faut attaquer en premier.** |

---

## 1. Les objets, et leur confusion

C'est ici qu'est la faute d'origine de toute la journee.

Le systeme manipule quatre choses que j'ai traitees comme une seule :

| objet | ce que c'est |
|---|---|
| **statut** | un id que le serveur met sur une personne. Ex. `198`. Plusieurs songs le partagent. |
| **sort** | ce que le barde a chante. Ex. `397` Valor Minuet IV, `398` Valor Minuet V. |
| **instance** | UN cast, sur UNE personne, a UN instant. C'est ca, "une song". |
| **ligne** | ce qui est dessine. Peut representer plusieurs instances (une ligne AoE). |

`[MESURE]` Minuet IV et Minuet V portent tous deux le statut `198`, et le `0x076` d'un allie
les montre comme deux presences indistinguables :

    0x076  Kaories  n=4 : 214 214 198 199

`[CODE]` Le moniteur d'alertes indexait ses entrees par (personne, **statut**) — donc une
seule entree pour deux songs. La seconde n'etait surveillee par personne.

`[DIT]` **Les tiers d'un meme song ne sont PAS la meme song.** Minuet III, IV, V sont des songs
distinctes qui occupent des slots distincts ; elles partagent seulement leur **id d'icone**.
Dans ce code le nombre du `0x076` sert d'ailleurs directement d'index d'icone
(`hud_timers.cpp`, `bufs[nb].icon = ob[i].status`) — "statut" et "icone" sont le meme nombre.

**Donc ce nombre ne nomme pas une song.** Il dit "il y a un Minuet actif", jamais lequel.
L'identite ne peut venir que du `0x028`, qui porte l'id du SORT au moment du cast.

| source | ce qu'elle sait |
|---|---|
| `0x028` | **quelle** song, sur qui, quand — l'identite, et elle seule |
| `0x076` | **combien** d'icones de cette famille sont presentes — jamais lesquelles |
| `0x063` | tes expirations exactes — la cle du temps restant |

**L'erreur de la journee tient en une phrase : j'ai demande au `0x076` de repondre a
"laquelle", alors qu'il ne sait repondre qu'a "combien".**

**Consequence.** Sans notion d'instance, "combien" et "laquelle" deviennent indecidables, et
on les remplace par des heuristiques de comptage. C'est ce que j'ai fait sept fois.

---

## 2. D'ou vient l'information, et ce que chaque source NE PEUT PAS dire

`[SPEC]` `[CODE]`

| source | contenu | limite dure |
|---|---|---|
| `0x028` paquet d'action | qui a chante quoi, sur qui, quand | rien apres le cast |
| `0x029` message d'action | un statut s'est termine sur une cible | porte un **statut**, jamais un sort |
| `0x063` ordre 9 | **tes** statuts + expirations exactes | toi seulement |
| `0x076` | les statuts des 5 membres de ta party | ni sort, ni duree, ni lanceur. Jamais l'alliance |
| memoire | tes propres statuts | toi seulement |

`[CODE]` `party_state.cpp:1638` — le `0x029` est lu, mais **uniquement pour les debuffs sur
les mobs** : `if (tdebuffs_[s].id != tid) continue;`. Rien ne l'ecoute pour les buffs.

`[SUPPOSE]` Qu'un `0x029` soit emis quand une song se termine sur un allie. **Jamais observe.**
C'est la capture 2 du protocole qui doit trancher — et si un ecrasement volontaire n'emet rien,
plusieurs regles ecrites aujourd'hui disparaissent sans remplacement.

---

## 3. Ce que je crois des regles du jeu

### 3.1 Les slots

`[DIT]` Le plafond depend de l'**instrument** equipe. Ce n'est pas une constante a observer.

`[SUPPOSE]` Base 2 songs, +2 avec un instrument +2, +1 avec Clarion Call, maximum 5.
Les nombres viennent d'une phrase du joueur en debut de session, pas d'une source.

`[MESURE]` Le plafond observe ce jour-la etait **4** : le cast d'une 5e song a ejecte
Gold Capriccio (`DROP ... slot rule`).

`[DIT]` Les **fake songs** (Gold Capriccio, Goblin Gavotte) sont chantees pour **occuper un
slot**. Elles sont masquees dans la config parce qu'on ne veut pas les voir.

`[CODE]` **Et elles ne sont pas comptees.** La boucle qui calcule `songCount` saute tout ce
dont `song_family()` vaut 0 — exactement ces songs-la. Le jeu compte 5 slots pleins, le code
en voit 3. **Toutes** les regles de plafond ecrites le 2026-09-10 reposent sur ce nombre.

### 3.2 Clarion Call

`[DIT]` CC **ouvre** le 5e slot ; il ne le maintient pas. La 5e song survit a la fin du buff et
se rechante par-dessus. Le slot ne se referme qu'en **perdant** cette song.

`[SUPPOSE]` Que le slot se referme **immediatement** a la perte, et pas a la fin du recast.
C'est ce que le code fait aujourd'hui. Non verifie.

`[MESURE]` CC a dure 180 s et a permis une 5e song (`0x076 n=6 : 512 214 198 198 199 207`).

### 3.3 Remplacement et ejection

`[MESURE]` Rechanter le meme sort rafraichit **la meme** ligne : meme entree, expiration
avancee de 14 194 ticks, soit exactement les 236 s ecoulees entre les deux casts.

`[DIT]` Deux tiers d'une meme famille **cohabitent** (Minuet IV + V). Ma regle "une song plus
longue remplace la plus courte" est fausse — le joueur l'a retiree explicitement.

`[MESURE]` Au plafond, chanter une nouvelle song en ejecte une. Victory March chantee,
Minuet V partie au meme instant (`age=15516ms` contre `lostAgo=15500ms`).

`[SUPPOSE]` Que la song ejectee est **celle a qui il reste le moins de temps**. Coherent avec
l'observation ci-dessus, mais une seule observation ne prouve pas une regle.

`[DIT]` Un refresh peut **echouer silencieusement** tant que la duree restante depasse la duree
de base non-buffee. **Le code ne modelise pas ca du tout.**

### 3.4 Durees

`[SPEC]` `dur = 120 x m1 x m2 x m3 + a3`, avec m2 = Troubadour (x2), m3 = Soul Voice ou Marcato
(x1.5) **seulement sur les familles 11/12/14**, a3 = Marcato seul (job points).

`[MESURE]` Un cast sous Nightingale + Troubadour + Marcato :

    CAST Honor March  dur=730s  m1=2.96  m2=x2  a3=20      (355 x 2 + 20 = 730)

`[DIT]` "Marcato = x1.5 multiplicatif et pas +20 s".

**Contradiction apparente, et je pense qu'elle n'en est pas une** : la mesure montre `m3`
absent et seulement le terme plat `a3`. Si Honor March n'appartient pas aux familles 11/12/14,
les deux affirmations sont vraies en meme temps. **A confirmer** — c'est exactement le genre
de point ou j'ai tendance a trancher trop vite.

---

## 4. Ce que je crois du comportement voulu a l'ecran

`[SPEC]` Une song AoE = **une** ligne groupee `(AoE N)`, N = personnes touchees.
Une song Pianissimo = une ligne **par personne**, nommee.
Un retardataire (quelqu'un rate par un re-cast) = liste **par son nom**, en bloc separe.

`[SPEC]` Une alerte rouge quand une song focus disparait, avec 5 suppressions : cast d'un
autre, Pianissimo, 5e song CC, hors zone, grace de zone.

`[DIT]` Une perte subie alerte ; un remplacement **voulu** n'alerte pas.

`[MESURE]` `0x076` a **5 emplacements fixes**, les inutilises remplis de zeros.

`[SUPPOSE]` Qu'un trust n'apparait jamais dans le `0x076`, donc "inconnu, on n'alerte jamais".
**Ecrit dans le code, jamais verifie.** Les logs montrent `<no cache>` pour un trust, ce qui
est compatible avec "absent du paquet" comme avec "present sous un autre id".

---

## 5. Ce que j'ai change le 2026-09-10, et sur quelle base

Sept corrections, chacune declenchee par un symptome rapporte, chacune ajoutant une regle.

| # | ce que j'ai fait | base | tient encore ? |
|---|---|---|---|
| 1 | exiger que la song ait quitte **ta** liste avant de taire une alerte | `[MESURE]` 6 songs vivantes marquees irrecuperables | oui |
| 2 | verrou empechant d'apprendre un plafond fausse | `[DIT]` la 5e survit au buff | **repose sur un compte faux** (3.1) |
| 3 | une entree de surveillance par **sort**, plus par statut | `[MESURE]` Minuet V surveillee nulle part | oui |
| 4 | presence decidee en **comptant** les exemplaires | `[MESURE]` `198 198` devient `198` | oui |
| 5 | alertes regroupees quand tout le monde perd d'un coup | `[DIT]` 2 lignes pour un evenement | oui |
| 6 | refuser une liste de buffs anterieure au zone | `[CODE]` le cache n'est pas vide au zone | oui |
| 7 | ne pas alerter sur une ejection au plafond | `[MESURE]` Victory March pousse Minuet V | **repose sur un compte faux** (3.1) |

**Deux d'entre elles (2 et 7) sont a jeter**, pas a corriger : elles comparent a un plafond un
nombre qui exclut les songs dont le role est d'occuper les slots.

---

## 6. Ce que j'ai cru et qui etait faux

A lire comme un avertissement sur mes angles morts.

| j'ai cru | la verite | qui a corrige |
|---|---|---|
| la fin de Clarion Call retire la 5e song | elle survit et se rechante | le joueur |
| Marcato = +20 s plat | x1.5 multiplicatif (nuance en 3.4) | le joueur |
| une song plus longue remplace la plus courte | les tiers cohabitent | le joueur |
| le plafond est une constante a observer | il depend de l'instrument | le joueur |
| les fake songs ne comptent pas | elles occupent un slot | le joueur |
| classer deux songs jumelles par age de l'entree | l'entree est reutilisee au re-cast ; c'est le **cast** qui compte | un log |
| aucune spec n'existait | 367 lignes dans le depot, jamais lues | le depot |

**Le motif est constant** : a chaque fois j'ai comble une inconnue par une hypothese
plausible au lieu de la marquer comme inconnue.

---

## 7. Les affirmations a attaquer en premier

Par ordre de degat si elles sont fausses :

1. **Une fake song occupe un slot** (3.1). Si oui, `songCount` est faux et deux regles sautent.
2. **Un ecrasement volontaire emet-il un `0x029` ?** (2). Si non, le probleme se dissout.
3. **Quelle song est ejectee au plafond** (3.3). Une seule observation.
4. **Le plafond exact par instrument** (3.1). Des nombres sans source.
5. **Un trust est-il absent du `0x076`** (4). Ecrit dans le code, jamais verifie.
6. **Le refresh qui echoue silencieusement** (3.3). Pas modelise du tout.

---

## 8. Ce qui restera de l'inference quoi qu'il arrive

A dire d'avance, pour ne pas annoncer une solution complete qui ne l'est pas :

- **Laquelle de deux songs de meme statut vient de tomber.** Le `0x029` porte un statut, le
  `0x076` une presence. Aucun paquet ne nomme le sort a la destruction.
- **Si une song perdue etait recuperable.** Aucun paquet ne parle de slots. Ca depend du
  plafond et de la disponibilite de Clarion Call, deux choses qu'il faut deduire.
- **Ce qui est tombe pendant un ecran de chargement.** Aucun message n'arrive quand le client
  ne recoit pas. Seul l'instantane au retour peut resynchroniser.

---

## 9. Ce que les sources ont corrige (2026-09-10, apres `05-reponses-mecaniques.md`)

Les reponses sourcees sont arrivees APRES la redaction de ce document. Les sections ci-dessus
sont laissees telles quelles — c'est un instantane de ce que je croyais. Voici les deltas.

### Confirme

| ce que je supposais | verdict |
|---|---|
| base 2 songs, +2 instrument, +1 Clarion Call, max 5 | `[WIKI]` **juste** — avec une precision : **1 seule** song sans instrument, ou en BRD sous-job |
| Clarion Call dure 180 s | `[WIKI]` **juste** — ma `[MESURE]` de 180 s est confirmee par une source independante |
| une fake song occupe un slot | `[FORUM]` **confirme.** C'est meme l'objet de la technique : remplir les slots puis les ecraser a pleine potence |
| `a3 = +20 s` mesure sur un cast Marcato | `[WIKI]` **explique** : job points *Marcato Effect*, +1 s par niveau, 20 niveaux — le terme du modele est juste |

**Consequence directe :** les regles 2 et 7 du 2026-09-10 sont **a jeter**, comme annonce en §5.
Le compte qu'elles comparent a un plafond exclut precisement les songs faites pour occuper les slots.

### Corrige

**Clarion Call — ma regle etait incomplete.** Je codais « le slot se referme des que tu perds
la song ». `[WIKI]` : le slot est **repeuplable tant que l'effet de 3 minutes tourne**. Passe
ce delai la song survit et se rechante par-dessus, mais une perte est definitive jusqu'au
prochain Clarion Call. Il y a donc **une fenetre de 3 min** que je n'avais pas du tout.

**"Une song plus longue remplace la plus courte" — l'erreur est identifiee.** Cette regle
n'existe pas. Ce qui existe est presque l'inverse, et le code ne le modelise pas :

> `[WIKI]` **Une song ne peut pas etre rechantee tant que sa duree restante depasse sa duree
> de base non-buffee.** Donc une song posee sous Troubadour REFUSE d'etre ecrasee par une
> version normale, silencieusement.

C'est une regle entiere absente du modele, et elle explique une classe de symptomes qu'on n'a
jamais su nommer : « j'ai rechante et il ne s'est rien passe ». **Aucune source ne dit si le
jeu emet un message dans ce cas** — a ajouter aux captures.

### Refute par la mesure (et donc : rien a coder)

Le wiki annoncait qu'une song **refuse d'etre rechantee** tant que sa duree restante depasse
sa duree de base non-buffee. J'allais l'ajouter au modele comme une regle manquante.

**`[MESURE]` 2026-09-10 : c'est faux.** Une song sous Troubadour a ~11 min restantes,
rechantee par le meme sort **sans** Troubadour, est bien ecrasee et retombe a ~5 min.
La regle wiki predisait un echec. Il n'y a donc aucun "refresh qui echoue silencieusement"
a modeliser, et ma vieille regle "une song plus longue remplace la plus courte" n'avait pas
davantage de fondement dans l'autre sens.

Note de methode : cette regle avait deja survecu a un aller-retour — je l'avais inventee, le
joueur l'avait corrigee, le wiki l'avait ressuscitee sous une forme inversee. Il a fallu une
mesure pour la tuer. **Une source ecrite n'est pas une mesure.**

### Tranche par la mesure — UNE seule cle gouverne tout

**`[MESURE]` 2026-09-10 : au plafond, c'est la song a la PLUS COURTE DUREE RESTANTE qui saute.**
BG Wiki avait raison, le forum SE avait tort. Le `[CONTRA]` qui bloquait toute regle de
plafond est resolu.

**Et ca simplifie le modele au lieu de l'alourdir.** Trois questions qu'on traitait separement
n'en font qu'une :

| question | reponse |
|---|---|
| quelle song le jeu ejecte-t-il au plafond ? | la plus courte restante |
| quelle song expire en premier ? | la plus courte restante, par definition |
| laquelle de deux jumelles (Minuet IV/V, meme statut) vient de tomber ? | la plus courte restante |

**Une seule cle : le temps restant.** J'avais ecrit trois regles avec trois criteres differents
— age du cast, naissance de l'entree du moniteur, comptage d'exemplaires. C'etaient trois
approximations de la meme grandeur, et leurs desaccords sont exactement les bugs de la journee
(le modele gardait Minuet V pendant que le moniteur gardait Minuet IV).

Note : cette cle etait **disponible depuis le debut**. Le `0x063` donne tes expirations exactes,
et une copie alliee porte son `expTick` fige au cast. Rien n'obligeait a approximer.

### Reste indecidable, et c'est le point qui bloque tout

**Quelle song est ejectee au plafond.** `[CONTRA]` — deux sources, deux regles :

- BG Wiki : celle a **la plus courte duree restante**
- Forum SE : la **plus ancienne**

Les deux **coincident presque toujours** : une dummy song est chantee sans gear de duree, donc
elle est a la fois la plus ancienne et la plus courte. Ma `[MESURE]` (Victory March pousse
Minuet V) ne les departage pas non plus.

> **Un seul test les separe :** au plafond, avoir cote a cote une song **ancienne mais longue**
> (chantee sous Troubadour) et une song **recente mais courte** (chantee sans rien), puis
> chanter une song de plus. La recente-courte part -> regle « moins de temps restant ». 
> L'ancienne-longue part -> regle « plus ancienne ».

Tant que ce test n'est pas fait, **toute** regle de plafond reste une hypothese, quel que soit
le nombre d'observations accumulees. C'est exactement le piege dans lequel je suis tombe.

### Ferme par la mesure : la restriction familles 11/12/14 est JUSTE

J'avais mis ce point en tete des risques en ecrivant "aucune source". **Les deux moities de
cette phrase etaient fausses.**

**La source existait**, nommee dans l'en-tete du fichier meme, deux lignes au-dessus de la
formule : `song_dur.h` reproduit le modele de **Timers.dll** (`FUN_10007a40`), et le decode
vit dans `scratchpad/timers_song.txt`. Sa ligne 32 dit textuellement :

    SOUL VOICE (0x34=52) : if song family in {Hymnus(11),Mazurka(12),14} -> m3 += 0.50

**Et le serveur la confirme.** `[MESURE]` 2026-09-10, `//aio songdur`, trois casts d'Honor
March (famille 8) :

| cast | modele | serveur | ecart |
|---|---|---|---|
| nu | 353 s | 352 s | -1 s |
| Marcato | 372 s | 372 s | **0 s** |
| Soul Voice | 352 s | 352 s | **0 s** |

- **Marcato** n'apporte que son terme plat `a3 = 20 s`. Pas de x1.5.
- **Soul Voice n'apporte RIEN** a la duree sur cette famille — ni `m3`, ni `a3`.

Si `m3` s'appliquait, le cast Soul Voice aurait dure ~533 s au lieu de 355 : l'ecart etait
impossible a manquer. Le `[INCONNU]` sur Soul Voice est leve, au moins pour les familles de
la rotation.

**Lecon de methode.** J'ai crie au loup sur du code correct, apres avoir declare "aucune
source" sans avoir lu l'en-tete du fichier que je critiquais. C'est le meme defaut que le
reste de la journee, dans l'autre sens : j'ai comble une inconnue par une hypothese au lieu
d'aller voir. Verifier coute une commande ; l'alerte, elle, aurait pu couter une refonte.

### LE PLAFOND EST PAR PERSONNE, PAS PAR BARDE

`[DIT]` Pianissimo rend la prochaine song mono-cible : **seule la personne visee est touchee.**

Ma question "un Pianissimo consomme-t-il un slot chez le LANCEUR" n'avait donc pas de sens.
Le slot est consomme **chez la personne touchee**. Le plafond est **par cible**.

`[MESURE]` Le log le montre noir sur blanc. Au meme instant :

    0x076 Kaories : 214 196 198 199     March, BALLAD III, UN Minuet, Madrigal
    toi (0x063)   : 417  397 398 400    March, Minuet IV, Minuet V, Madrigal

Quatre songs chacun, **mais pas les memes**. Le Ballad Pianissimo a pris la place d'un Minuet
**chez elle**, pendant que les deux Minuets tenaient **chez toi**. Le joueur l'avait dit le
jour meme : "le pianissimo ballad 3 a remplace son minuet, c'est normal" — je l'avais note
comme une confirmation de suppression d'alerte, sans voir ce que ca disait du modele.

**Consequence, et elle est plus grave que celle des fake songs.**

`songCount` compte aujourd'hui "les sorts distincts vivants sur l'ENSEMBLE des allies"
(`hud_timers.cpp`, boucle `seenSp`). C'est un nombre centre sur le barde, et **il n'existe
nulle part dans le jeu**. Le vrai compte est **par cible** : Kaories a ses slots, toi les
tiens, Monberaux les siens. Une song AoE remplit un slot chez tout le monde a la fois ;
un Pianissimo en remplit un chez une seule personne.

Les regles de plafond du 2026-09-10 **comparaient un nombre global a une limite locale**.
Elles ne pouvaient pas marcher, meme une fois les fake songs comptees. C'est la deuxieme
raison, independante, de les jeter plutot que de les corriger.

**Ce que ca implique pour la reecriture :** il n'y a pas "un" compte de songs. Il y en a un
**par personne**, et chacun se lit directement — le `0x076` pour un allie, le `0x063` pour toi.
Aucune inference n'est necessaire pour l'obtenir.

### Corrections apportees par la relecture externe (2026-09-10)

**TENUTO CASSE L'AFFIRMATION 2, et il n'etait nulle part dans le modele.**

`[BG]` Page Tenuto : *"If the next song you cast affects yourself, it will not subsequently be
overwritten by other songs."* Une song sous Tenuto est **immunisee contre l'ejection**. Ce
n'est pas une modulation de duree, c'est une exception categorique.

Le bon enonce devient :

> Parmi les songs **ejectables**, c'est celle a qui il reste le moins de temps qui saute.
> Une song sous Tenuto n'est pas ejectable.

Le flag `tenuto` appartient donc a l'**instance**, aux cotes du sort et de l'expiration, et il
vient du `0x028` comme les tags SV/NT/TR/M. **Le code ne le lit pas** : `party_state.cpp:1208`
ignore explicitement le statut 455 ("no duration effect, deliberately not read"). C'etait juste
pour la duree, et faux pour l'ejection — la raison du retrait ne couvrait pas cet usage.

`[DIT]` **TESTE EN JEU le 2026-09-10 : Tenuto ne releve PAS le plafond.** Chanter sous Tenuto
alors qu'on est au plafond ejecte quand meme une song ; Tenuto rend seulement la song ainsi
posee non-remplacable par la suite. Le "cinq songs Tenuto" du wiki est donc simplement le
maximum normal, toutes pouvant etre sous Tenuto. **L'affirmation 4 n'est pas touchee.**

Le modele se precise sans se compliquer :

| | |
|---|---|
| plafond | inchange par Tenuto |
| victime a l'ejection | la plus courte **parmi les ejectables** (non-Tenuto) |
| une song posee sous Tenuto | non-ejectable jusqu'a expiration ou dissipation |

`[SUPPOSE]` **Cas limite non teste** : si TOUTES tes songs sont sous Tenuto et que tu es au
plafond, que se passe-t-il au cast suivant ? Le cast echoue, ou quelque chose saute quand meme ?
Marginal, mais c'est le genre d'etat que le code doit savoir ne pas savoir.

**LA CLE DE COMPTAGE EST LE COUPLE (CHANTEUR, CIBLE), pas la cible seule.**

`[FFXICLO]` Page Song : *"A character may only have one Enhancing Song effect on him/her
**per player singing**."* Deux bardes ne se disputent pas les slots d'une cible : chacun a son
propre compte sur chaque personne.

Ma mesure Pianissimo predisait deja exactement ca, et elle le disait seule. Elle a maintenant
une source. Reserve du relecteur : FFXIclopedia est le wiki historique et la formulation est
anterieure aux instruments a slots — **un test avec un vrai second barde vaut mieux**.

**LA REGLE D'EJECTION EST CLOSE, ET LA CONTRADICTION EST EXPLIQUEE.**

`[FFXICLO]` desamorce le forum SE explicitement : *"the song effect with the least duration
remaining is **not necessarily the song that was cast earliest**"*. Le thread du forum decrivait
le cas des dummy songs, ou les deux regles coincident. C'est bien la duree restante.

**RESERVE SUR L'AFFIRMATION 1.** On ecrit "Minuet III + IV + V" mais la mesure montre **deux**
Minuets et la source **deux** tiers. Rien n'etablit trois. Ecrire "au moins deux" tant que ce
n'est pas teste.

**LE TERME TENUTO DANS LA FORMULE DE DUREE — deja tranche, mais a re-verifier.**

Le relecteur signale un job point *Tenuto Effect* (+2 s/rang, 40 max) qui manquerait a la
formule. Notre RE le dit aussi (`timers_song.txt:36` : `TENUTO -> a3 = merit[0x142] * 2`).
Mais `party_state.cpp:1244` documente un retrait **delibere** :

> *"neither Clarion Call nor Tenuto touches song duration (confirmed by the player, who mains
> the job) -- Both used to feed a bogus duration bonus here, and the exclusive ternary also let
> either of them SUPPRESS Marcato's real one."*

**MESURE, ET LE RELECTEUR AVAIT RAISON.** Mon hypothese ("le job point prolonge la protection,
pas la song") etait fausse.

| | modele | serveur | ecart |
|---|---|---|---|
| avant correction | 353 s | 393 s | **+40 s (10 %)** |
| apres correction | 393 s | 392 s | **-1 s (0 %)** |

Le -1 s est l'arrondi habituel, deja present sur le cast nu. Le terme est exact.

`[DIT]` Le joueur est Master, tous les job points pleins : 20/20 en *Marcato Effect* (+1 s/rang
= +20) et 20/20 en *Tenuto Effect* (+2 s/rang = +40). Les deux mesures tombent dessus.

**C'etait un vrai bug de production**, pas une subtilite de harnais : depuis juillet, toute song
chantee sous Tenuto etait annoncee 40 s trop courte. Aucun test unitaire ne pouvait le trouver
— la formule etait juste selon sa propre definition, c'est la definition qui avait perdu un terme.

**La forme du defaut merite d'etre retenue.** Le retrait de juillet corrigeait un VRAI bug (un
ternaire exclusif laissait Tenuto effacer le terme de Marcato). Le diagnostic etait juste, le
remede trop large : on a supprime le terme au lieu de le faire s'additionner. C'est exactement
le motif des sept regles du 2026-09-10 — un symptome reel traite en enlevant plus que la faute.

`a3` est desormais une **somme** : Marcato +20, Tenuto +40, les deux ensemble +60 — un etat que
l'ancien code ne pouvait pas produire.

**Clarion Call reste dehors.** Le RE lui donne le meme terme (`timers_song.txt:35`), mais
personne ne l'a mesure. Il rentrera quand une capture le dira, pas avant.

**Piege de vocabulaire, corrige dans le code.** `game_mem.cpp` parlait de *"BRD merit levels"*
pour des **job points**. Marcato et Tenuto n'ont aucune categorie de merite — ce sont des job
points. La confusion a coute un echange entier avec le joueur, qui cherchait dans le mauvais menu.

### Tenuto GELE la song : elle ne se rechante pas non plus

`[DIT]` `[MESURE]` 2026-09-10. Tenuto ne protege pas seulement contre l'ejection par une AUTRE
song : il empeche aussi de **rechanter la meme song** sur soi. La song est figee jusqu'a son
expiration.

Coherent avec le texte du wiki — un re-cast EST un ecrasement — mais on ne l'avait jamais
formule, et ca change le modele : **une song sous Tenuto ne se rafraichit pas.** A mettre en
regard de l'affirmation 5 ("une song se rechante toujours par-dessus elle-meme"), qui devient :
*sauf si elle est sous Tenuto*.

**Trace dans le log, et le code avait raison.** Le cast sous Clarion Call a produit :

    SONGDUR spell=417 ... tc=2 nTgt=2 aoeSelf=0

`aoeSelf=0` alors que le joueur portait bien une March : le serveur a repondu "sans effet" sur
lui (sa March protegee par Tenuto), et `is_no_land_msg` l'a correctement refuse. J'allais
classer cette incoherence apparente comme un defaut a investiguer. **C'etait un comportement de
jeu, correctement capte par du code ecrit avant qu'on comprenne pourquoi.**

### Clarion Call et la duree : NON MESURE, et une deduction retiree

Le RE (`timers_song.txt:35`) donne a Clarion Call le meme terme plat que Tenuto. **Personne ne
l'a mesure**, et le recast d'une heure fait que ca ne sera pas mesure de sitot. Le code le
laisse dehors : on n'ajoute pas un terme qu'on n'a pas vu.

**Deduction retiree.** J'avais conclu "CC n'ajoute rien" en calculant, depuis l'expiration de
Clarion Call et celle de la March, que la seule duree possible etait 355 s. Le raisonnement
supposait que cette expiration venait du cast fait sous CC. Elle venait de la song PRECEDENTE,
protegee par Tenuto et donc jamais remplacee. **Toute la deduction tombe.**

C'est le meme defaut que toute la journee, dans sa forme la plus pure : une valeur indirecte
prise pour une mesure, presentee comme "fortement etabli". Le joueur l'a rattrape avec une
information que le log ne pouvait pas porter. **Une deduction n'est pas une mesure, meme quand
l'arithmetique tombe juste.**

### Le modele de duree est CLOS, et entierement mesure

Sept mesures contre le `0x063` du serveur, six a ecart nul, une a -1 s d'arrondi :

| situation | terme | ecart |
|---|---|---|
| nu | 0 | -1 s |
| Marcato | +20 s | 0 s |
| Tenuto | +40 s | -1 s |
| Clarion Call | +40 s | 0 s (apres correctif) |
| **Marcato + Tenuto** | **+60 s** | **0 s** |
| Soul Voice | 0 (famille 8) | 0 s |
| Troubadour | x2 | 0 s |

**Les termes s'ADDITIONNENT** — mesure du 2026-09-10, a3 = 60 s confirme a la seconde.
"Le dernier gagne", que les trois affectations du desassemblage auraient aussi autorise,
est ecarte.

**Bilan : deux vrais bugs de production**, tous deux presents depuis juillet 2026. Toute song
chantee sous Tenuto OU sous Clarion Call etait annoncee 40 s trop courte. Aucun test unitaire
ne pouvait les voir — la formule etait juste selon sa propre definition, c'est la definition
qui avait perdu un terme. Seule une comparaison avec le serveur pouvait les trouver, et l'outil
pour ca (`//aio songdur`) existait deja depuis des mois.

**Reste infere, et pas cher a se tromper** : que Tenuto ET Clarion Call ensemble comptent une
seule fois. Meme octet, donc un seul terme est la lecture honnete, mais la paire n'a jamais ete
chantee (une heure de recast sur CC).

### Sans source du tout

Par ordre de degat, et le premier est du code a nous :

1. ~~**La restriction familles 11/12/14**~~ — **FERMEE** : sourcee (RE de Timers.dll) et confirmee par le serveur, voir ci-dessus.
   Si elle est fausse, elle l'est **silencieusement sur toutes les songs de la rotation**.
2. **Soul Voice** — toutes les pages inaccessibles. Le code le traite comme Marcato, statut inconnu.
3. **Pianissimo consomme-t-il un slot** — rien nulle part.
4. **Les autres bardes** (partage de slots, meme song, trusts bardes) — rien nulle part.
5. **Le plafond apres un switch d'instrument** — un seul indice ambigu.
6. **Loughnashade : +1 ou +2 slots** — le wiki se contredit lui-meme.

### Les 4 tests en jeu qui rapportent le plus

Aucun ne demande la sonde. Ils se font a l'oeil, en quelques minutes.

| # | test | tranche |
|---|---|---|
| 1 | ancienne-longue vs recente-courte au plafond | la regle d'ejection — **et la base de toutes les regles de plafond** |
| 2 | switch d'instrument en tenant 4 songs | si un plafond peut seulement etre **appris**, ou doit etre **lu** |
| 3 | 3 AoE + 1 Pianissimo, tenter une 5e | si un Pianissimo consomme un slot |
| 4 | deux bardes, meme song, meme cible | tout le bloc "autres bardes", vide aujourd'hui |
