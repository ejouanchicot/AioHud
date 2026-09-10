# Questions sur les mecaniques BRD — a faire trancher hors session

**A quoi sert ce document.** Chaque question ici est un endroit ou le code a devine.
Tant qu'elles n'ont pas de reponse, tout ce qu'on ecrira sera encore de l'inference.
Reponses attendues : la regle exacte + la source. Un "je crois que" ne suffit pas.

---

## 1. Le plafond de songs

1.1 Combien de songs sans instrument ni Clarion Call ?

1.2 Quels instruments donnent des slots, et combien chacun ?
    (Daurdabla, Terpander, Gjallarhorn, Marsyas, Blurred Harp, Blurred Harp +1...)

1.3 Les merits / job points ajoutent-ils des slots ? Si oui, combien et lesquels ?

1.4 Le plafond change-t-il **instantanement** quand on change d'instrument en plein combat ?
    Autrement dit : si je tiens 4 songs avec un Daurdabla et que je reswitch,
    est-ce que j'en perds sur-le-champ, ou est-ce qu'elles restent jusqu'a expiration ?

> **Pourquoi ca compte :** le code apprend aujourd'hui le plafond en observant. Si le plafond
> depend de l'instrument, apprendre est structurellement faux — il apprendra le maximum
> sous Daurdabla et le gardera apres le switch.

## 2. L'ejection

2.1 Au plafond, quand on chante une song de plus, **laquelle** part ?
    La plus ancienne ? Celle a qui il reste le moins de temps ? La premiere de la liste ?

2.2 Cette regle est-elle la meme quand c'est un **Pianissimo** qui prend le slot ?

2.3 Le jeu affiche-t-il un message quand une song est ejectee ? Lequel, mot pour mot ?

## 3. Le remplacement

3.1 Rechanter la meme song : elle se remplace toujours ?

3.2 Il a ete dit qu'un refresh peut **echouer silencieusement** tant que la duree restante
    depasse la duree de base non-buffee. Quelle est la regle exacte ?
    Le jeu dit-il quelque chose quand le refresh echoue ?

3.3 Deux tiers d'une meme famille (Minuet IV et V, Honor et Victory March) :
    ils cohabitent sur deux slots, confirme ? Y a-t-il des exceptions ?

3.4 Une song plus longue remplace-t-elle une plus courte de la meme famille,
    ou est-ce que ca n'existe pas ?

## 4. Les job abilities

4.1 Troubadour : x2 sur la duree, confirme ? Ca s'applique au moment du **cast** uniquement ?

4.2 Marcato : x1.5 multiplicatif (et non +20 s). Se cumule-t-il avec Troubadour ?
    Dans quel ordre ?

4.3 Soul Voice, Nightingale : effet sur la duree, ou seulement sur la puissance / le temps
    d'incantation ?

4.4 Clarion Call : le 5e slot survit a la fin du buff et se rechante par-dessus.
    Si on **perd** la 5e song pendant que le recast tourne, le slot se referme-t-il
    **immediatement**, ou reste-t-il ouvert jusqu'a la fin du recast ?

## 5. Les autres bardes

5.1 Un autre barde en party : ses songs occupent-elles **mes** slots sur les cibles,
    ou chaque barde a-t-il ses propres slots par personne ?

5.2 Que se passe-t-il si deux bardes chantent la meme song sur la meme personne ?

5.3 Les trusts bardes (Ulmia, Joachim...) suivent-ils les memes regles ?

## 6. Pianissimo

6.1 Une song en Pianissimo consomme-t-elle un slot **chez le lanceur** aussi,
    alors qu'elle ne se pose que sur une personne ?

6.2 Compte-t-elle dans le plafond de la meme facon qu'une song AoE ?

---

## 7. Les fake songs (dit en session le 2026-09-10)

Gold Capriccio, Goblin Gavotte : chantees uniquement pour **occuper un slot**, jamais pour leur
effet. Elles sont volontairement **masquees dans la config** — on ne veut pas les voir dans Timers.

**Le defaut que ca revele.** Le code calcule `songCount` en sautant tout ce dont `song_family()`
vaut 0 (`hud_timers.cpp`, boucle `seenSp`) — c'est-a-dire exactement ces songs-la. Le jeu, lui,
les compte : 2 fake + 3 vraies = 5 slots pleins, le code en voit 3.

Toutes les regles de plafond ajoutees le 2026-09-10 (`clarion_learn`, `song_unrecoverable`,
`song_evicted`) comparent ce nombre a un plafond. Elles reposent donc sur un compte qui ignore
les songs dont le role EST d'occuper les slots.

7.1 Confirmer : une fake song occupe bien un slot au meme titre qu'une vraie ?

7.2 Est-elle ejectee en priorite quand on atteint le plafond, ou suit-elle la meme regle
    que les autres (cf. 2.1) ?

7.3 A cacher dans l'affichage, mais a COMPTER dans les slots : c'est bien ce qu'on veut ?
    (autrement dit : "masque" est une decision d'affichage, pas de modele)
