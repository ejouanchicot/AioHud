---
title: Refonte de l'en-tête et du chrome de config — 2026-09-05
summary: Session nocturne d'une centaine de commits sur l'overlay de configuration — fronton pleine largeur, logotype cuit et sa lueur, système à deux métaux, liserés, accordéon animé. Cinq bugs de fond, dont trois de la même famille. Les règles à retenir sont en fin de document : c'est la partie qui a de la valeur.
---

# Refonte de l'en-tête et du chrome de config — 2026-09-05

## Ce qui a été construit

**Le fronton.** L'en-tête était une bande de 50 px avec l'emblème collé au bord gauche, la croix et le
sélecteur de langue collés au bord droit, et toute la largeur d'une page plein écran morte au milieu. C'est
maintenant une plaque **pleine largeur** (elle touche le haut et les deux bords de l'écran), haute de 88 px,
éclairée de l'intérieur par trois lumières dérivantes à périodes non commensurables, avec une parallaxe au
curseur. Ce qui reste aligné sur la marge de page, c'est le **contenu** de la plaque, pas la plaque : la
surface déborde, l'alignement non.

**Le logotype.** `scripts/gen_logo.py` cuit `assets/logo_src/aiohud_logo.png` en `assets/aiohud_logo.raw`
(1024×256 BGRA) et génère `src/ui/logo_metrics.h`, qui dit **où l'illustration se trouve dans la toile**.
L'en-tête se positionne à partir de ces métriques : un rendu d'une autre proportion refait la mise en page sans
qu'on touche à `config_page.cpp`. Le script gère les deux formes qu'un modèle d'image produit (verrou déjà
horizontal, ou emblème empilé sur le mot à recomposer), décide de la décontamination du halo **sur preuve
mesurée**, et étend les couleurs opaques dans la zone transparente.

**La lueur du logotype.** Seconde passe texturée de la même illustration, masquée par ses propres glyphes, en
**multiplication** (`SRCBLEND = DESTCOLOR`, `DESTBLEND = ONE` → `dst × (1 + src)`), la source étant l'alpha du
glyphe répliqué dans les trois canaux (`D3DTA_ALPHAREPLICATE`) multiplié par le profil porté par le diffuse.
Profil : ellipse penchée, cosinus surélevé au carré, sur une grille 32×8 de `tquad4`. Un interrupteur
`GLEAM_ON` est conservé — c'est lui qui a identifié le coupable après six réécritures.

**Deux métaux.** `C_METAL*` (l'or de l'illustration) et `C_STEEL*` (acier froid), tous deux **fixes** et non
dérivés du thème. Règle : **l'or dit « marque ou sélection », l'acier dit « structure ».** Fronton en or ;
onglets, conteneur, séparateur de la barre latérale et cadre du preview en acier. Tous les liserés sont d'une
**couleur unie** — pas de biseau : un liseré délimite, il ne se regarde pas.

**L'accordéon.** Les cinq sections du panneau Party se déplient au lieu d'apparaître, via
`cat_fold` / `cat_fold_clip` / `cat_fold_end` (`config_controls`). L'en-tête de section est devenu une **barre
de titre** dont la forme dit ce qu'elle possède : arrondie sur quatre coins fermée, carrée du bas ouverte,
soudée à son panneau.

## Les bugs de fond, et pourquoi ils sont arrivés

**1. Trois « tentes » prises pour des rectangles.** `soft_blob` est une tente bilinéaire : sa décroissance est
linéaire et son contour extérieur est fait de **quatre droites**. À la taille de l'objet qu'elle éclaire, elle
se lit comme un rectangle flou à bords nets. Trois instances ont été trouvées successivement — les traînées
obliques de la plaque, la lumière traversante, puis **la tente pulsante derrière le logo**, qui a survécu à six
réécritures de la lueur sans jamais être suspectée.

**2. La lumière fabriquée à partir du logo.** Toutes les premières versions dérivaient la lueur des couleurs de
l'illustration. Via `MODULATE` elle est proportionnelle au texel → l'or clair écrête, et la frontière de la
zone écrêtée est un bord dur mobile. Via `D3DTA_COMPLEMENT` elle est proportionnelle à `1 − texel` → les ombres
remontent plus que les hautes lumières, le modelé s'aplatit, la lettre paraît opaque. **Aucune des deux n'était
réglable** : c'est ce que font ces opérations.

**3. Le RGB des pixels transparents mis à zéro.** Le GPU interpole couleur et alpha **indépendamment** : le
filtrage bilinéaire et chaque niveau de mip mélangent le RGB des voisins transparents dans le bord visible.
Mesuré : luminance médiane 62 sur les bords contre 117 sur les pleins — le mot était dessiné avec un halo noir
en permanence. Et `1 − noir = blanc`, donc le complément allumait précisément ce halo à pleine puissance.

**4. `C_GOLD` est un alias de `C_ACCENT`.** Deux tours de « bordures dorées » n'ont rien changé de visible
parce que rien n'était doré : tout se dessinait dans la couleur d'accent du thème, un bleu acier chez
l'utilisateur.

**5. Le stencil ne s'imbriquait pas.** `clip_rect_begin` remettait sa zone à 0 et écrivait 1 ; `clip_rect_end`
coupait le stencil. Correct tant que rien ne s'imbriquait — or le contenu des modules est dessiné dans un
découpage de **défilement**, et l'accordéon découpe dedans. À partir de la première section, le débordement
d'une page longue n'était plus contenu. Corrigé par un comptage de profondeur (INCRSAT / DECRSAT).

## Les règles à en tirer

1. **Un cadre se dessine PAR-DESSUS ce qu'il encadre.** Le rebord du conteneur était coupé par la barre
   latérale, qui démarre exactement à `ix` et se dessinait après. Tout autre ordre le laisse à la merci du
   prochain dessin — et chaque onglet dessine autre chose.
2. **Une variable globale empruntée doit être rendue propre AU POINT D'EMPRUNT.** `g_fade` est global,
   `ROW_BAND` y écrit à chaque ligne, et la barre de profil est dessinée avant la restauration de fin de
   module : elle a clignoté dès qu'une section a fait varier `e`.
3. **`soft_blob` a des bords droits.** Il n'est utilisable que si ses bords tombent **hors** de la surface
   éclairée. Quand une de ces tentes se révèle coupable, balayer les autres avant de toucher à autre chose.
4. **Quand un changement énorme ne change rien, l'objet décrit n'est pas l'objet modifié.** Ne pas régler une
   septième fois : **couper le suspect** (une ligne) et poser une question binaire. Deux sondes — période à
   2,5 s, puis image figée — ont tranché ce que quatre relectures du code n'avaient pas tranché.
5. **Un pixel transparent a quand même une couleur.** Étendre les couleurs opaques vers l'extérieur ; ne jamais
   mettre le RGB à zéro.
6. **La lumière sur du métal MULTIPLIE.** Ajouter une constante aplatit par les deux bouts ; un *screen blend*
   aplatit par le bas. `dst × (1 + k)` conserve les rapports, donc le relief.
7. **Une couleur d'identité qui doit s'accorder à une texture cuite ne peut pas suivre le thème.**
8. **Un contour qui suit un rayon se dessine comme la forme agrandie, puis recouverte.** Des droites posées le
   long d'un rectangle arrondi lui coupent les coins.
9. **Un trou rectangulaire ne peut pas être encadré par un contour arrondi** — combler les quatre écoinçons.
10. **Le découpage au stencil doit compter sa profondeur**, sinon un `end()` interne annule le découpage externe
    pour le reste de la frame.
11. **Une garde qui refuse doit garder sa comptabilité équilibrée** : un `clip_rect_begin` refusé avale son
    `end()`, sinon il dépile un niveau qu'il n'a jamais empilé.
12. **Caler l'ILLUSTRATION au pixel, pas son quad** : le quad est plus haut que l'art d'une marge fractionnaire.
13. **La barre latérale est dessinée par trois onglets.** Toute modification doit toucher les trois — sinon elle
    « ne prend pas » selon l'onglet ouvert.
14. **Une capture d'écran tranche ce que quatre lectures du code ne tranchent pas.** Les bandes verticales et le
    rectangle glissant ont tous deux été identifiés sur image, pas par raisonnement.

## Où reprendre

**Le prochain sujet est le point 1 ci-dessous : le vide de la colonne de contrôles.** Tout le reste de la
session est terminé, construit et testé (`build` 0 warning, 967 checks). L'état exact se relit dans
`git log --oneline -40` : les commits partent de la lueur du logotype et finissent au déploiement de
l'accordéon.

## Ce qui reste, par ordre d'importance

1. **Le vide.** Cinq en-têtes repliés sur une colonne de 900 px : c'est le défaut le plus visible de l'écran, et
   aucune bordure ne le rattrape. Deux pistes — sections sur **deux colonnes**, ou **colonne de contrôles plus
   étroite** avec le preview qui prend la place. Arbitrage de goût, à trancher.
2. ~~Déployer `cat_fold` aux autres modules.~~ **FAIT** (commit `7cc3a1b`) : 30 sections dans 10 fichiers, par
   transformation scriptée. Reste hors périmètre volontairement : le changelog de l'onglet Update et les groupes
   de Debug, qui sont des éléments d'une liste défilante avec leur propre arithmétique en `y`, pas des panneaux
   basés sur `ry`.
3. **Dédupliquer les trois copies de la barre latérale** (Configuration / Help / Debug).
4. **Discipline de `g_fade`** : le rendre non hérité — chaque bloc autonome le pose à son entrée plutôt que de
   dépendre de ce que le précédent a laissé. La règle 2 est un pansement sur un point d'emprunt ; il y en a
   d'autres.
5. **Un helper `metal_edge()`** pour que les liserés (fronton, onglets, conteneur, séparateur) ne puissent plus
   diverger : ils sont aujourd'hui quatre écritures du même geste.
