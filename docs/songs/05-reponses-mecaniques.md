# Reponses aux questions mecaniques BRD — sourcees

**Date :** 2026-09-10. **Repond a :** `01-questions-mecaniques.md`.

Ce document ne remplace pas le tien : il le remplit la ou une source existe, et laisse
explicitement vide la ou il n'y en a pas. Une case vide n'est pas un oubli, c'est un resultat.

## Comment lire

| marque | signification |
|---|---|
| `[WIKI]` | trouve sur BG Wiki ou FFXIclopedia, URL donnee en bas |
| `[FORUM]` | forum officiel Square Enix ou FFXIAH, consensus de joueurs |
| `[CONTRA]` | **deux sources disent des choses differentes** — ne pas coder dessus |
| `[INCONNU]` | aucune source atteignable. Je n'ai pas devine. |

Plusieurs pages de BG Wiki renvoient 403 a la lecture automatisee (les pages de categorie,
Soul Voice, Gjallarhorn, Marsyas, Daurdabla). Les trous ci-dessous viennent en partie de la.

---

## 1. Le plafond de songs

**1.1 — Sans instrument ni Clarion Call.** `[WIKI]`
2 songs avec un instrument ordinaire. **1 seule** si aucun instrument n'est equipe, ou si
BRD est le sous-job.

**1.2 — Les instruments.** `[WIKI]` `[CONTRA]`

| instrument | slots | source | fiabilite |
|---|---|---|---|
| Terpander | +1 (3 total) | page Terpander : « Grants an additional song effect ». Note explicite : **ne se cumule pas avec Daurdabla** | sure |
| Daurdabla | +2 (4 total) | page Song + Community Bard Guide | sure |
| Blurred Harp | +1 (3 total) | page Song, groupe « three songs » | moyenne |
| Miracle Cheer | +1 (3 total) | page Song, meme groupe | moyenne |
| Loughnashade | **conflit** | page Song le classe dans le groupe « three songs » (+1). Le Community Bard Guide du **meme wiki** ecrit « +2 additional song slots (4 total) » | **a trancher en jeu** |
| Gjallarhorn | inconnu | page 403 | — |
| Marsyas | inconnu pour les slots | page Song le mentionne pour **+50% de duree**, jamais pour un slot | — |

Maximum absolu : **5**, atteint uniquement avec un instrument +2 et Clarion Call.

**1.3 — Merits / job points.** `[WIKI]` partiel
Les 10 categories de job points BRD sont : Soul Voice Effect, Clarion Call Effect, Minne
Effect, Minuet Effect, Pianissimo Effect, Song Accuracy Bonus, Tenuto Effect, Lullaby
Duration, Marcato Effect, Requiem Effect. **Aucune ne donne de slot.**
Les merits, eux, je ne les ai pas verifies. Ne pas conclure.

**1.4 — Changement d'instrument en plein combat.** `[INCONNU]`
Un seul indice, page Clarion Call : « If you already have 3+ songs active via instruments
like Daurdabla, you must maintain that instrument equipped to access additional slots beyond
three. » Ca suggere que le slot d'**instrument**, contrairement au slot Clarion Call,
n'est pas acquis une fois pour toutes. Mais la phrase est ambigue et c'est la seule.

> **Test :** tenir 4 songs sous Daurdabla, switcher vers un instrument sans bonus, regarder.
> Une song tombe-t-elle sur-le-champ, ou tiennent-elles toutes jusqu'a expiration ?

---

## 2. L'ejection

**2.1 — Laquelle part au plafond.** ~~`[CONTRA]`~~ **TRANCHE EN JEU le 2026-09-10.**

> **`[MESURE]`** C'est **la plus courte duree restante** qui saute.
> BG Wiki avait raison, le forum SE avait tort. Le `[CONTRA]` ci-dessous est resolu.

Texte conserve pour memoire (il explique pourquoi les deux regles etaient indiscernables) :

Deux sources, deux regles differentes :

- BG Wiki, page Song : « Casting additional songs beyond this limit will overwrite the song
  with **the shortest remaining duration**. »
- Forum officiel SE, thread *Dummy songs on BRD* : « **the oldest** song in the rotation gets
  replaced. »

Ces deux regles **coincident dans presque tous les cas observables**, ce qui explique qu'on
puisse coder l'une en croyant valider l'autre :
- Une dummy song est chantee sans gear de duree, donc elle est a la fois la plus ancienne
  (chantee avant les vraies) et la plus courte en restant. Les deux regles la designent.
- Ta `[MESURE]` (Victory March pousse Minuet V) ne les departage pas non plus, sauf si tu
  connais les temps restants exacts au moment du cast.

> **Le seul test qui tranche :** arriver au plafond avec, cote a cote,
> une song **ancienne mais a longue duree restante** (chantee sous Troubadour)
> et une song **recente mais a courte duree restante** (chantee sans rien).
> Chanter une song de plus.
> - la recente-courte part → la regle est « **moins de temps restant** »
> - l'ancienne-longue part → la regle est « **plus ancienne** »
>
> Sans ce test, toute regle de plafond codee restera une hypothese, quelle que soit la
> quantite d'observations accumulees.

**2.2 — Meme regle pour un Pianissimo ?** `[INCONNU]`

**2.3 — Message a l'ejection.** `[INCONNU]` — c'est ta capture 2, aucun wiki ne repond.

---

## 3. Le remplacement

**3.1 — Rechanter la meme song.** `[WIKI]`
Oui, mais **pas toujours** : voir 3.2. Ce n'est pas inconditionnel.

**3.2 — Le refresh qui echoue.** ~~`[WIKI]`~~ **REFUTE EN JEU le 2026-09-10.**

> **`[MESURE]`** Une song portee sous Troubadour, ~11 min restantes, rechantee par le **meme
> sort sans Troubadour** : elle est **bien ecrasee**, et retombe a ~5 min.
>
> La regle wiki citee ci-dessous predit l'inverse (11 min > duree de base -> le re-cast doit
> echouer). Elle est donc fausse telle qu'ecrite, ou obsolete. **Rien a modeliser.**
>
> Une mesure directe prime sur le wiki : c'est le jeu qui arbitre.

Texte wiki conserve pour memoire :
Page Song : « The same song cannot be reapplied until its current duration drops below the
**base (unenhanced) duration**. »
Donc une song chantee sous Troubadour ne se laisse pas ecraser par une version normale tant
qu'il lui reste plus que sa duree de base. Le wiki ne dit **pas** si le jeu emet un message
dans ce cas. A ajouter aux captures.

**3.3 — Deux tiers d'une meme famille cohabitent.** `[INCONNU]` cote wiki
Aucune page atteignable ne l'ecrit noir sur blanc. Honor March / Victory March sont deux
sorts distincts, donc le cas est trivial. Pour Minuet IV + V, la meilleure preuve disponible
est **ta propre mesure** : `0x076 ... 198 198`, deux presences du meme statut. Je la
considere plus fiable que le wiki sur ce point precis.
Exceptions eventuelles : inconnu.

**3.4 — « Une song plus longue remplace une plus courte de la meme famille ».** `[WIKI]`
Cette regle **n'existe pas**. Ce qui existe est 3.2, et c'est presque l'inverse : une
instance **plus courte** ne peut pas ecraser une instance **plus longue du meme sort**.
La regle 5 confondait vraisemblablement les deux.

---

## 4. Les job abilities

**4.1 — Troubadour.** `[WIKI]`
x2 sur la duree. Duree du JA : 60 s. Recast : 10 min. Le multiplicateur est applique
**en dernier**, apres tous les autres bonus. Il s'applique aux songs chantees pendant
que le JA est actif.

**4.2 — Marcato.** `[WIKI]` + `[INCONNU]` sur ta restriction
Page Marcato : x1.5 sur l'effet, et x1.5 sur la precision magique **ou** la duree selon la
song. **Multiplicatif avec Troubadour** (« multiplicative with Troubadour effect too »).
Soul Voice + Marcato **ne se cumulent pas** : « SV cancels out Marcato or the modifier is
capped at 100% ».
Terme additif separe : job points *Marcato Effect*, +1 s de duree par niveau, 20 niveaux max,
soit **+20 s** au maximum. Independant de la famille de song.

> **La restriction familles 11/12/14 de `party_state.cpp:1243` n'apparait sur aucune page
> que j'ai pu lire.** Je ne peux ni la confirmer ni l'infirmer. Elle merite d'etre en tete de
> ta liste du §7 : si elle est fausse, elle l'est silencieusement, sur toutes les songs de
> ta rotation.

**4.3 — Soul Voice, Nightingale.** `[WIKI]` partiel
Nightingale : divise par 2 le temps d'incantation **et** le recast des songs. Recast du JA :
10 min. Aucun effet de duree documente.
Soul Voice : pages BG Wiki et FFXIclopedia toutes deux inaccessibles (403 / 402). **Aucune
source.** Ton code le traite comme x1.5 sur les memes familles que Marcato — meme statut
inconnu.

**4.4 — Clarion Call, fermeture du slot.** `[WIKI]` — et ta question avait une 3e reponse
Niveau 96. **Duree d'effet : 3 minutes.** Recast : 1 heure.
Ta `[MESURE]` de 180 s est confirmee par une source independante — bon signe pour ta sonde.

Donc ce qui gouverne le slot n'est **ni** l'instant de la perte **ni** la fin du recast, mais
la fenetre d'effet de 3 minutes. Page BG Wiki : le slot reste utilisable tant que la song est
rafraichie avant expiration ; si elle expire ou si tu meurs, tu ne peux pas le repeupler
« unless the ability remains active ».

Reformule : **le slot est repeuplable tant que l'effet de 3 min tourne. Passe ce delai, la
song survit et se rechante par-dessus, mais si tu la perds elle est perdue jusqu'au prochain
Clarion Call.** C'est coherent avec ce qui a ete dit en session.

---

## 5. Les autres bardes

**5.1** `[INCONNU]` — **5.2** `[INCONNU]` — **5.3** `[INCONNU]`

Aucune source atteignable sur les trois. C'est le bloc le plus mal couvert du document.
Avec un deuxieme barde sous la main, c'est 5 minutes de test en jeu et ca vaut mieux que
n'importe quelle page de wiki.

---

## 6. Pianissimo

**6.1** `[INCONNU]` — **6.2** `[INCONNU]`
La page Pianissimo donne l'effet (niveau 20, recast 5 s, dure 60 s ou jusqu'a la prochaine
song, limite la prochaine song a une cible, cast ramene a 4 s) mais **ne dit rien** sur la
consommation de slot. Aucune autre page atteignable non plus.

> **Test :** Daurdabla (4 slots), chanter 3 songs AoE puis 1 song en Pianissimo sur une
> personne. Tenter une 5e. Si elle ejecte quelque chose, la Pianissimo comptait.

---

## 7. Les fake songs

**7.1 — Une fake song occupe-t-elle un slot ?** `[FORUM]` — **OUI, confirme.**
Forum officiel Square Enix, thread *Dummy songs on BRD* : les dummy songs occupent bien un
slot de buff actif, et c'est precisement l'objet de la technique — « having the 4 song buffs
is the only way to overwrite with full potency songs ». On chante les dummies pour remplir
les slots, puis on les ecrase avec les vraies songs a pleine potence.

**Donc le defaut identifie dans ton etat des lieux est reel** : `songCount` ignore exactement
les songs dont le role est d'occuper un slot. Les regles 2 et 7 du 2026-09-10 sont bien a
jeter, pas a corriger.

**7.2 — Ejectee en priorite ?** `[INCONNU]` en tant que regle.
Mecaniquement : si 2.1 est « moins de temps restant », les dummies partent naturellement en
premier parce qu'elles sont chantees sans gear de duree. C'est probablement **pourquoi** la
technique fonctionne. Ce n'est pas une regle separee, c'est un effet de bord de 2.1 —
a valider par le test de 2.1.

**7.3** — Question de design, pas de mecanique. « Masque » est une decision d'affichage,
« compte » est une decision de modele. Les deux sont independantes.

---

## Ce qui reste sans source, par ordre de degat

1. **La restriction familles 11/12/14** sur `m3` (4.2, 4.3). Codee en dur, aucune source,
   affecte silencieusement toutes les songs de la rotation.
2. **La regle d'ejection exacte** (2.1). Deux sources en desaccord, un seul test les departage.
3. **Le plafond apres un switch d'instrument** (1.4). Un indice ambigu, rien de plus.
4. **Loughnashade : +1 ou +2 ?** (1.2). Le wiki se contredit lui-meme.
5. **Pianissimo consomme-t-il un slot** (6.1). Rien nulle part.
6. **Les autres bardes** (5.1, 5.2, 5.3). Rien nulle part.
7. **Soul Voice** (4.3). Toutes les pages inaccessibles.

## Les 4 tests en jeu qui rapportent le plus

| # | test | tranche |
|---|---|---|
| 1 | ancienne-longue vs recente-courte au plafond | 2.1, 7.2, et la base de toutes les regles de plafond |
| 2 | switch d'instrument en tenant 4 songs | 1.4, et le fait meme d'apprendre un plafond |
| 3 | 3 AoE + 1 Pianissimo, tenter une 5e | 6.1, 6.2 |
| 4 | deux bardes, meme song, meme cible | 5.1, 5.2 |

Aucun de ces quatre n'a besoin de la sonde. Ils se font a l'oeil, en jeu, en quelques minutes.

---

## Sources

- BG Wiki — Song : https://www.bg-wiki.com/ffxi/Song
- BG Wiki — Terpander : https://www.bg-wiki.com/ffxi/Terpander
- BG Wiki — Troubadour : https://www.bg-wiki.com/ffxi/Troubadour
- BG Wiki — Marcato : https://www.bg-wiki.com/ffxi/Marcato
- BG Wiki — Pianissimo : https://www.bg-wiki.com/ffxi/Pianissimo
- BG Wiki — Clarion Call : https://www.bg-wiki.com/ffxi/Clarion_Call
- BG Wiki — Job Points : https://www.bg-wiki.com/ffxi/Job_Points
- BG Wiki — Bard : https://www.bg-wiki.com/ffxi/Bard
- BG Wiki — Community Bard Guide : https://www.bg-wiki.com/ffxi/Community_Bard_Guide
- FFXIclopedia — Clarion Call : https://ffxiclopedia.fandom.com/wiki/Clarion_Call
- Forum Square Enix — Dummy songs on BRD : https://forum.square-enix.com/ffxi/threads/58897
- FFXIAH — Daurdabla, Song Overwrite Order : https://www.ffxiah.com/forum/topic/30990/daurdabla-song-overwrite-order/