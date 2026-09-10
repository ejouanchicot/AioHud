# A verifier : le modele songs BRD, reduit a 4 affirmations

**Contexte pour qui lit ca sans historique.** On ecrit un plugin d'overlay pour Final Fantasy XI
qui affiche les songs de barde (BRD) et alerte quand l'une tombe. Apres une journee de
corrections au symptome qui ont empile sept regles contradictoires, on a tout arrete et reduit
le systeme a quatre affirmations. **Avant de reecrire le code dessus, on veut qu'elles soient
attaquees.**

**Ce qu'on attend.** Confirmer, infirmer, ou dire "aucune source". Une case vide est un
resultat, pas un oubli. **Ne pas combler une inconnue par une hypothese plausible** — c'est
exactement l'erreur qui a produit les sept regles.

**Marques employees :** `[MESURE]` = observe dans nos logs de paquets, extrait cite.
`[DIT]` = affirme par le joueur (barde experimente). `[RE]` = lu dans le desassemblage de
Timers.dll, le plugin Windower de reference.

---

## Affirmation 1 — Une song est une INSTANCE

Un cast, une personne, un instant, une expiration. Elle nait du paquet d'action `0x028`,
qui porte l'id du **sort**.

**Pourquoi ca compte :** le paquet `0x076` (buffs des membres de la party) ne transporte
qu'un id d'icone/statut, pas le sort. Minuet III, IV et V sont des songs **differentes**
occupant des slots **differents**, mais elles partagent le meme id.

`[MESURE]` Un allie portant deux Minuets apparait comme `... 198 198 ...` — deux presences
indistinguables. Le sort n'est connaissable qu'au moment du cast.

**Question 1.1** — Confirmez-vous que plusieurs tiers d'une meme famille de song
(Minuet III + IV + V, Honor March + Victory March) coexistent sur des slots distincts ?
Existe-t-il des exceptions ou des familles qui, elles, se remplacent entre tiers ?

---

## Affirmation 2 — Le TEMPS RESTANT est la seule cle

Trois questions qu'on traitait separement n'en font qu'une :

| question | reponse |
|---|---|
| quelle song le jeu ejecte-t-il quand on depasse le plafond ? | celle a qui il reste le moins de temps |
| quelle song expire en premier ? | la meme, par definition |
| laquelle de deux jumelles (meme id, sorts differents) vient de tomber ? | la meme |

`[MESURE]` Teste en jeu : au plafond, c'est bien **la plus courte duree restante** qui saute.
Cela contredit un thread du forum Square Enix qui affirme que c'est **la plus ancienne**.
Les deux regles coincident dans presque tous les cas observables — une "dummy song" est
chantee sans equipement de duree, donc a la fois la plus ancienne et la plus courte — ce qui
explique qu'on puisse coder l'une en croyant valider l'autre.

**Question 2.1** — Une source ecrite confirme-t-elle "plus courte duree restante" ?
**Question 2.2** — Y a-t-il un cas connu ou cette regle ne s'applique pas
(ex. une song protegee par Tenuto, une song sous Clarion Call) ?

---

## Affirmation 3 — Le `0x076` COMPTE, il ne NOMME pas

Il dit combien d'icones d'une famille sont presentes sur une personne, jamais lesquelles.
L'identite vient du `0x028`, la quantite du `0x076`, les expirations exactes du `0x063`
(pour soi uniquement — aucun paquet ne donne la duree restante d'un buff sur un allie).

**Question 3.1** — Confirmez-vous qu'aucun paquet serveur ne transporte la duree restante
d'un buff porte par un **allie** ? (On a deja une preuve par desassemblage, on cherche une
contradiction eventuelle.)

**Question 3.2** — Quand une song se termine sur quelqu'un, le serveur emet-il un message
d'action (`0x029`) ? Et surtout : **un ecrasement volontaire** — chanter une song de plus
au plafond, ce qui en ejecte une — emet-il quelque chose, ou le remplacement est-il
silencieux ? C'est la question qui decide de toute notre architecture de detection.

---

## Affirmation 4 — Le PLAFOND EST PAR PERSONNE, pas par barde  ← LA PLUS FRAGILE

C'est l'affirmation la plus recente et la moins sourcee. **Si elle est fausse, la reecriture
prevue est fausse.**

`[DIT]` Pianissimo est une JA qui rend la prochaine song mono-cible : seule la personne visee
la recoit.

`[MESURE]` Au meme instant, dans nos logs :

    allie (0x076) : March, BALLAD III, UN Minuet, Madrigal
    barde (0x063) : March, Minuet IV, Minuet V, Madrigal

Quatre songs chacun, **mais pas les memes**. Le Ballad pose en Pianissimo sur l'allie a pris
la place d'**un Minuet chez lui**, pendant que les deux Minuets tenaient **chez le barde**.

`[DIT]` Le joueur decrit ca comme normal : "le pianissimo ballad 3 a remplace son minuet".

**On en deduit** que le nombre de songs est compte **par personne portant les songs**, et non
comme un total detenu par le barde.

**Question 4.1** — Confirmez-vous que la limite s'applique **par cible** ?

**Question 4.2 — la plus importante.** Si deux bardes chantent sur la meme personne,
cette personne porte-t-elle :
  (a) 4 songs au total, les deux bardes se disputant les memes slots ?
  (b) 4 songs **du barde A** plus 4 **du barde B** ?
  (c) autre chose ?

**Question 4.3** — Quel instrument determine le plafond sur une cible : celui du **barde qui
chante**, forcement — mais est-ce evalue **au moment du cast** ou **en continu** ?
Autrement dit : un barde qui tient 4 songs avec un Daurdabla (+2 slots) et qui **change
d'instrument** perd-il des songs sur-le-champ, ou tiennent-elles jusqu'a expiration ?

> Un seul indice trouve jusqu'ici, page Clarion Call de BG Wiki : *"If you already have 3+
> songs active via instruments like Daurdabla, you must maintain that instrument equipped to
> access additional slots beyond three."* La phrase est ambigue et c'est la seule.

**Question 4.4** — Les trusts bardes (Ulmia, Joachim...) suivent-ils les memes regles ?
Leurs songs occupent-elles les memes slots que celles du joueur sur une meme cible ?

---

## Ce qui est deja regle — ne pas y revenir sauf contradiction

| point | etat | source |
|---|---|---|
| une "fake song" (Gold Capriccio, Goblin Gavotte) occupe un slot | confirme | forum SE — c'est l'objet meme de la technique |
| Clarion Call : effet 3 min, recast 1 h, ouvre un 5e slot | confirme | BG Wiki + `[MESURE]` 180 s |
| la 5e song survit a la fin de l'effet et se rechante par-dessus | confirme | `[DIT]` + BG Wiki |
| Troubadour : x2 sur la duree | confirme | BG Wiki + `[MESURE]` |
| Marcato : terme plat +1 s/niveau de job point, 20 max | confirme | BG Wiki + `[MESURE]` a 0 s d'ecart |
| Marcato/Soul Voice x1.5 : **seulement** familles Hymnus, Mazurka, Scherzo | confirme | `[RE]` Timers.dll + `[MESURE]` a 0 s d'ecart sur March |
| Soul Voice n'ajoute **rien** a la duree des autres familles | mesure | `[MESURE]` 352 s prevus, 352 s reels |
| une song **peut** etre rechantee meme si sa duree restante depasse sa duree de base | mesure, **contredit BG Wiki** | `[MESURE]` : 11 min sous Troubadour, rechantee sans Troubadour, ecrasee a 5 min |

Ce dernier point merite un mot : la page Song de BG Wiki affirme *"The same song cannot be
reapplied until its current duration drops below the base (unenhanced) duration"*. La mesure
en jeu dit l'inverse. **Si vous avez une source qui explique cette divergence** (regle
supprimee par une mise a jour ? condition manquante ?), elle nous interesse.

---

## Ce qu'on fera de vos reponses

Les affirmations 1 a 4 deviennent le modele du code. Chaque regle ecrite devra pointer vers
l'une d'elles. Une regle qui n'en decoule pas sera refusee — c'est ainsi qu'on evite de
re-empiler sept rustines.

**L'affirmation 4 est le point de bascule.** Si le plafond est par barde et non par cible, ou
si deux bardes partagent les slots d'une cible, la structure de donnees change entierement.
