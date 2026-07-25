# Les chants du barde — comment ça marche vraiment

*Ce que j'ai compris des chants en construisant tes Timers. Version humaine : pas de code,
pas de jargon, juste le fonctionnement réel du jeu.*

---

## En une phrase

Un barde entretient **quelques buffs sur lui et ses proches**, chacun avec sa propre durée
qui s'écoule, qu'il doit **re-chanter avant qu'ils tombent** — et le jeu rend le suivi
piégeux parce que certaines chansons se ressemblent trop à ses yeux.

---

## 1. Un chant, c'est un buff que tu poses sur les gens autour de toi

Quand tu chantes, le buff se pose sur **toi et les gens de ta party assez proches** — pas
sur toute l'alliance, juste ceux dans le rayon au moment du chant.

> **⚠️ Rien à voir avec les « bulles » du GEO.** Un Indi-/Geo- crée un **champ persistant**
> (qui te suit ou reste au sol) et qui **pulse** en continu. Un chant, non : il s'applique
> **une seule fois** à chaque personne présente, puis **chacun porte sa propre copie** avec
> sa propre minuterie qui descend. Tu ne « maintiens » pas un champ — tu re-chantes pour
> ré-appliquer avant que ça tombe.

Trois choses à retenir :

- **Tu es toujours touché toi-même.** Un chant part de toi ; tu reçois toujours ta propre
  copie du buff. (C'est important : si un chant n'est **plus** sur toi, c'est qu'il s'est
  fait éjecter — voir §2.)
- **Tout le monde touché en même temps a la même durée.** Le chant s'applique d'un coup, au
  même instant, avec la même minuterie pour tous.
- **Trop loin = pas touché.** Quelqu'un hors du rayon quand tu chantes ne reçoit rien. S'il
  avait déjà une vieille version, il **la garde** (plus courte) — il n'est pas rafraîchi.

---

## 2. Tu as un nombre limité de « places » de chant

C'est le cœur de tout. Un barde ne peut entretenir qu'un **nombre limité de chants à la
fois**.

| Situation | Nombre de chants |
|---|---|
| De base | 2 |
| Avec le bon instrument + mérites | davantage (ton setup habituel) |
| **Clarion Call** (grosse capacité) | +1 temporaire, le temps de placer un chant en plus |

> **La règle qui pique :** quand tes places sont **pleines** et que tu chantes une chanson
> de plus, **la plus vieille saute**. C'est voulu — mais c'est exactement ce qui faisait
> croire aux Timers qu'un chant avait été « perdu » alors que tu l'avais remplacé exprès.

---

## 3. Les grandes familles de chants

Chaque famille fait un type de chose. En gros :

| Famille | À quoi ça sert |
|---|---|
| **Marches** (Honor, Victory, Advancing…) | Vitesse d'attaque / haste |
| **Minuets** | Attaque |
| **Madrigals** | Précision |
| **Ballads** | Régénération de MP (pour les mages) |
| **Paeons** | Régénération de HP |
| **Preludes** | Précision/attaque à distance (rangers) |
| **Etudes** | Une statistique précise (STR, DEX…) — souvent **mono-cible** |
| **Madrigals / Scherzos / Carols** | Précision, défense élémentaire, etc. |
| **Threnodies, Lullabies, Elegies** | Débuffs sur les **ennemis** (pas des buffs) |

Tu jongles surtout avec **2 chansons offensives + les buffs utiles** selon la party.

---

## 4. Certaines chansons sont des « jumelles » aux yeux du jeu

Voilà le vrai piège, celui qui a causé la moitié des bugs qu'on a corrigés.

Deux chansons **différentes** peuvent partager **la même icône / le même type** dans le
jeu. Exemples :

- **Honor March** et **Victory March** → même « case ».
- **Valor Minuet IV** et **Valor Minuet V** → même « case ».

Le jeu, quand il te dit « untel a ce buff », ne précise **pas laquelle des deux**. Donc si
tu as Honor March et que ton pote a Victory March, de l'extérieur ça ressemble au **même**
buff — et sans précaution, le compteur les mélange. C'est pour ça qu'on a dû rajouter des
gardes pour ne pas afficher un faux « (AoE 2) ».

---

## 5. La durée, et ce qui l'allonge

La durée de base d'un chant est courte (~2 minutes). Ce qui l'étire :

- **Troubadour** → **double** la durée. C'est le gros levier.
- **Ton instrument + ton stuff « song duration »** → un pourcentage en plus.
- **Soul Voice / Marcato** → surtout de la **puissance**, mais aussi un peu de durée sur
  certaines familles.

> À noter : **Clarion Call** et **Tenuto** ne touchent **pas** la durée. Clarion Call ajoute
> une *place*, Tenuto *protège* un chant — mais ni l'un ni l'autre n'allonge la minuterie.

---

## 6. Tes capacités (JA) — ce qu'elles font vraiment

| Capacité | Ce qu'elle fait | En clair |
|---|---|---|
| **Nightingale** | Réduit le recast des chants | Tu enchaînes tes chants plus vite |
| **Troubadour** | Double la durée | Tes chants tiennent 2× plus longtemps |
| **Soul Voice** | Gros boost de puissance | Tes chants tapent bien plus fort (limité dans le temps) |
| **Marcato** | Boost la puissance du **prochain** chant | Un one-shot pour un chant clé |
| **Pianissimo** | Rend le prochain chant **mono-cible** | Tu poses un chant sur **une seule** personne |
| **Clarion Call** | Ajoute une **place** de chant | Tu tiens un chant de plus, temporairement |
| **Tenuto** | **Protège** un chant sur toi | Il ne se fait pas éjecter par le suivant |

**Nightingale + Troubadour** (« NT ») sont la paire classique : tu poses ta rotation vite
(Nightingale) et elle tient longtemps (Troubadour). C'est ce que le petit tag `(NT)` dans
les Timers t'indique.

---

## 7. Re-chanter, rater quelqu'un, remplacer

Trois situations du quotidien :

- **Re-chanter pour rafraîchir.** Tu relances tes chants avant qu'ils tombent. Tout le monde
  à portée repart à plein.
- **Rater quelqu'un.** Si une personne est hors de portée quand tu re-chantes, **elle reste
  sur l'ancienne version** (plus courte). Elle va tomber avant les autres → il faut penser à
  la re-chanter. *(C'est le « retardataire » nommé qu'on affiche maintenant.)*
- **Remplacer exprès avec Pianissimo.** Tu poses un chant précis sur **une** personne. Si
  ses places sont pleines, un de ses chants saute — **volontairement**. *(On ne sonne plus de
  fausse alerte pour ça.)*

---

## 8. Deux bardes en même temps

Chaque barde entretient **ses propres** chants. Les buffs des deux **s'additionnent** sur la
party (dans la limite des places de chacun).

Le hic : si les deux posent une chanson **du même type** (les fameuses jumelles du §4), le
jeu a du mal à dire **qui** a chanté **quoi** — c'est le cas le plus délicat à suivre, et
celui où l'affichage peut encore se tromper de propriétaire dans de rares cas.

---

## Le résumé de poche

1. **Un chant = un buff posé sur toi et tes proches** (pas un champ au sol comme le GEO), même durée pour tous.
2. **Places limitées** → un chant de trop éjecte le plus vieux (c'est normal).
3. **Des chansons sont jumelles** aux yeux du jeu → source n°1 de confusion.
4. **Troubadour double la durée**, ton stuff l'allonge, Soul Voice/Marcato boostent la
   puissance.
5. **Pianissimo** = mono-cible, **Clarion Call** = une place en plus, **Tenuto** = protège.
6. **Hors de portée** = pas rafraîchi → cette personne tombe en premier.

---

*Document compagnon : `docs/architecture/timers-songs-brd.md` (la version technique, ce que le plugin fait pour
chaque cas).*
