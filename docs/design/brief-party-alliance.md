---
title: Brief Design — Party / Alliance
summary: Le brief (en français) pour un designer UI / IA « design » — concepts de box party/alliance, contraintes absolues, data, gameplay et livrables attendus.
source: BRIEF_DESIGN_PARTY.md
---
# Brief Design — Modules **Party / Alliance** (AioHUD)

> **Pour qui :** un designer UI (humain ou Claude « design »).
> **Ce qu'on attend de toi :** des **concepts de box** party/alliance, modernes, lisibles, condensés — pas du code.
> **Posture :** je te briefe comme un lead UI briefe son équipe. Voilà le produit, les contraintes dures, la data, le gameplay. **Trouve les meilleures solutions.** Sois créatif, ose plusieurs directions, justifie tes choix par la lisibilité et le gameplay.
> **Liberté :** propose **autant de concepts que tu juges pertinent**. Le nombre de chiffres exacts n'est pas la priorité — les **contraintes et les besoins** le sont. Tu as le droit d'inventer.

---

## 0. Le produit en une page

**AioHUD** est une interface **repensée de zéro** pour **Final Fantasy XI** (via Windower 4), dessinée **en direct par-dessus le jeu**. Elle remplace l'affichage natif du jeu par un overlay moderne. Le premier module — celui qui nous occupe — est le **module Party / Alliance** : la liste des membres du groupe et de l'alliance.

**La mission de ce module :** afficher l'état de tous les membres de façon **instantanément lisible en plein combat**, tout en étant **assez compact** pour ne pas noyer l'écran (jusqu'à **18 personnages** affichés en même temps), et **recouvrir proprement la fenêtre native** de FFXI (on ne doit plus voir l'UI d'origine derrière).

---

## ⚠️ À LIRE EN PREMIER — les contraintes **ABSOLUES** (un 1er passage en a raté plusieurs)

Ces cinq points ne sont **pas négociables**. Un concept qui en viole un est **hors-sujet**, même s'il est joli.

1. **FOND OPAQUE, BLOC CONTIGU — pas de tuiles séparées, pas de transparence.**
   Chaque box doit **masquer à 100 % la fenêtre native** de FFXI derrière elle. Donc : **fond OPAQUE** (aucune transparence qui laisse voir l'UI d'origine) et **un seul panneau plein et contigu**. **PAS de cartes/tuiles isolées flottant sur le jeu, une par joueur** → un concept « tuiles de verre séparées » (type *Slate*) est **REFUSÉ** : les espaces entre tuiles laissent voir le natif et le fond du jeu. Les membres sont des **lignes/cellules d'un même panneau plein**, collées, sans trou.

2. **3 BOXES INDÉPENDANTES — pas une pile unique de 18.**
   *Party*, *Alliance 1*, *Alliance 2* sont **3 panneaux distincts**, chacun **positionné et dimensionné indépendamment** par l'utilisateur (drag / zones), chacun **collé à SA propre fenêtre native**. Ce n'est **PAS** une seule colonne de 18 membres empilés ancrée dans un coin. **Conçois UNE box réutilisable** (party) **+ sa variante alliance** ; tu peux montrer les 3 ensemble pour le stress-test, mais elles restent **détachables et repositionnables**.

3. **COUVERTURE PAR-BOX : ancrée en bas, grandit vers le haut, plancher 100 %.**
   La fenêtre native party **change de hauteur selon le nombre de membres (1→6)**. La box reste **ancrée par le bas** et **grandit vers le haut** pour couvrir l'empreinte native à tout effectif. Elle **ne rétrécit jamais** sous cette empreinte (**plancher = 100 %**, elle ne peut que grossir). La couverture est **vérifiée box par box** (chaque alliance couvre sa propre fenêtre native, via des marqueurs séparés).

4. **RÉSOLUTION-INDÉPENDANT, Y COMPRIS WIDE / ULTRAWIDE.**
   Tout se raisonne en **fractions d'écran et proportions**, **jamais en pixels absolus**. Le layout doit couvrir le natif **et rester lisible** en **16:9, 21:9 (ultrawide), 32:9 (super-ultrawide) et 4:3** — la fenêtre native se positionne différemment selon le ratio, la couverture doit rester valide partout, sans déformation ni trou. Tu peux **rendre les maquettes à 1080p** pour l'illustration, mais **exprime les tailles en proportions** (% de la hauteur d'écran, em de police relative), pas en px figés.

5. **UN CONCEPT À LA FOIS.**
   Livre **un seul concept par passage**, **poussé à fond** (maquette + toutes les specs), pour qu'on itère dessus avant de passer au suivant. **Pas 5 concepts survolés d'un coup.**

---

## 1. Contexte gameplay — le **pourquoi**

FFXI est un MMO où le joueur **regarde la party en permanence**. Selon son rôle, il en tire des infos vitales, vite :

- **Soigneur / support :** scanne les **HP** de 6 (voire 18) personnes en continu → repérer qui descend, qui est critique, **au coup d'œil**. C'est l'info n°1.
- **Portée de sort :** un heal / buff ne porte qu'à une certaine **distance** (en *yalms*). Savoir qui est **à portée** avant de lancer évite de gaspiller un cast.
- **Ciblage :** le joueur a une **cible** (`<t>`) et une **sous-cible** (`<st>`). Il faut voir **qui est ciblé** sans ambiguïté (ex. soigner la sous-cible tout en gardant le mob en cible).
- **Fenêtre d'attaque (TP) :** le **TP** (0→3000) conditionne les *Weapon Skills*. **≥ 1000 = WS prêt**, **3000 = aftermath** (état spécial). Timing offensif.
- **Buffs / debuffs :** suivre les états (protégé, empoisonné, silence…) — jusqu'à ~20 par membre, **uniquement pour sa propre party** (le jeu n'envoie pas les buffs des alliés).
- **Rôles sociaux :** *party leader*, *alliance leader*, *quartermaster* (gestion du butin) — utile mais secondaire.
- **Cast en cours :** voir qu'un membre **incante** (quel sort, avancement) aide à coordonner.

**Le mot d'ordre :** **lecture instantanée** (hiérarchie visuelle forte : les HP dominent), **densité** (18 lignes tiennent à l'écran), **zéro ambiguïté** (cible, critique, à portée).

---

## 2. Contraintes **dures** (non négociables)

### 2.1 Couverture du natif — **OPAQUE & CONTIGU** (rappel des contraintes 1 & 3)
- **Fond OPAQUE + panneau CONTIGU** : la box masque **100 %** de la fenêtre native. **Aucune transparence** qui laisse voir l'UI d'origine, **aucune tuile/carte séparée** (les trous laissent voir le natif). Les membres = lignes d'un même panneau plein, collées.
- **Ancrée en bas, grandit vers le haut, plancher 100 %** : elle ne peut que grossir, jamais rétrécir sous l'empreinte native.
- L'empreinte native **varie selon le nombre de membres** (1 à 6) : plus de monde = fenêtre native plus haute.
- **Couverture vérifiée box par box** (voir 2.2).

### 2.2 Échelle & indépendance
- **3 boxes INDÉPENDANTES** : **Party** (≤6), **Alliance 1** (≤6), **Alliance 2** (≤6) → jusqu'à **18 membres**. Chacune **positionnée/dimensionnée séparément**, collée à **sa propre** fenêtre native. **Pas** une pile unique.
- Chaque box **s'auto-dimensionne** à son contenu et se ré-ancre.
- **Conçois UNE box réutilisable** (party) **+ variante alliance** (voir 2.3). Gère **1 → 6 membres** proprement.

### 2.2b Résolution & ratios (rappel contrainte 4)
- **Fractions d'écran / proportions**, jamais de px absolus. Un layout sauvegardé s'adapte à **n'importe quel écran et ratio**.
- Doit couvrir le natif **et** rester lisible en **16:9, 21:9 (ultrawide), 32:9 (super-ultrawide), 4:3**. La fenêtre native **se place différemment selon le ratio** → la couverture doit rester valide partout, sans trou ni déformation.
- Maquettes rendues à 1080p pour l'illustration, mais **tailles pensées en proportions**.

### 2.3 Différences Party vs Alliance
- **Alliance : pas de buffs** (le jeu ne les transmet pas). Donc pour les alliés : nom, HP/MP/TP, job, distance, cast, marqueurs — mais **pas d'icônes de buff**. C'est une opportunité pour une **version encore plus dense**.
- La **Party** a en plus une **petite boîte flottante « Cost / Next »** (coût en MP du sort survolé dans le menu, et *recast*) collée en haut de la box party.

### 2.4 Rendu technique (à respecter, mais permissif)
Le moteur est **DirectX 8 fixed-function**, dessiné à la main. **Ce qui est disponible :**
- **Quads colorés à dégradé** (rectangles, dégradés verticaux/4-coins), **glow additif** (lumière qui s'accumule → néon).
- **Formes vectorielles anti-aliasées maison** : disques, anneaux/arcs, segments de cercle, losanges, triangles — avec **bord adouci (feathering)**.
- **Textures bitmap** : atlas de police (glyphes), icônes, **et textures custom qu'on peut fabriquer**. *On a déjà produit des fioles au liquide qui coule (texture animée par scroll d'UV), des embouts en verre, des icônes de buff, des dots leader.* → **Les matières texturées sont possibles.**
- Coût : ~18 lignes × plusieurs éléments par frame. Rester **raisonnable** par ligne (mais on tient déjà des fioles texturées animées, donc de la matière est OK).

> **Important — textures & assets :** si un concept a besoin de **textures custom** (icônes, fonds, matières, effets, cadres, atlas), **dis-le explicitement et décris-les** (format, contenu, animation éventuelle). **On peut les produire.** Idem pour des **polices** particulières. Ne te bride pas là-dessus : demande ce dont tu as besoin.

---

## 3. Le **modèle de données** (ce qu'une ligne peut afficher)

Par membre, classé par priorité. Un concept **doit** traiter les *Critiques*, **devrait** traiter les *Utiles*, **peut** intégrer les *Optionnels* (ou les proposer en option).

| Donnée | Détail | Priorité |
|---|---|---|
| **Nom** | Jusqu'à ~18 caractères, tronqué si besoin | **Critique** |
| **HP** | Pourcentage **et** valeur brute. Couleur par palier (plein→vide) : vert → jaune → orange → rouge. **Alarme visible ≤ 25 %** (clignotement). | **Critique** |
| **Cible / sous-cible** | Qui est `<t>` (cible) et `<st>` (sous-cible). Doit être **sans équivoque**. | **Critique** |
| **MP** | Pourcentage + valeur. Bleu. | Utile |
| **TP** | 0 → 3000. **≥ 1000 = WS prêt** (mise en avant/glow). 3000 = aftermath (état spécial). | Utile |
| **Job** | Job **principal** + **sub-job** (ex. WHM/BLM). | Utile |
| **Distance** | En *yalms* (0 → 30+). Seuils de **portée de cast** : proche (bleu) < ~10, marginal (jaune) ~10–20.8, hors portée (rouge) > ~20.8. | Utile |
| **Cast en cours** | Nom du sort (tronqué) + **progression** + type (magie / capacité / WS). Apparaît/disparaît. | Utile |
| **Buffs / debuffs** | Jusqu'à ~20 icônes. **Party uniquement.** | Optionnel/Utile |
| **Marqueurs de rôle** | *Alliance leader*, *party leader*, *quartermaster* (3 pastilles distinctes). | Optionnel |
| **États** | **Hors-zone** (affiche le nom de la zone au lieu des vitals), **KO** (0 HP, nom en rouge), **hors portée** (grisé/voilé). | Utile |

**Hiérarchie visuelle attendue :** les **HP** dominent, le **nom** est immédiatement associé, la **cible** saute aux yeux, le reste (MP/TP/distance/job) est secondaire mais lisible.

---

## 4. L'existant (pour **situer**, pas pour copier)

On a déjà, en jeu et fonctionnel :
- **8 styles de jauge** au choix par box : *Fiole* (texture liquide animée + verre), *Barres*, *Segments* (batterie), *Minimal* (fin trait + gros chiffre), *Sphère* (bulle qui se remplit), *Anneau* (jauge circulaire), *Cristal* (losange), *Texte* (le chiffre seul, coloré/animé).
- **Config par box indépendante** (Party / Alliance 1 / Alliance 2) : taille, style de jauge, hauteur/largeur de barre, badge de job, casts, distance, bordure.
- **Typographie par élément** (Nom, HP, MP, TP, Cast, Badge, Distance, Interface, Cost box) : police, taille, contour, gras, italique, MAJUSCULES, couleur.
- **Effets** : glow WS-ready, alarme HP critique, **ligne de niveau lumineuse** (repère du %), curseur « main » animé, cadre de sélection façon verre, **ligne/repère de niveau** qui épouse la forme.
- Skin de **fenêtre native FFXI** (9-slice) optionnel comme cadre.

**But du brief :** dépasser tout ça. Propose des **directions neuves**, plus modernes, plus ergonomiques, plus condensées — pas des variantes des 8 styles ci-dessus.

---

## 5. La **demande**

Invente des **concepts de box** party/alliance. **UN SEUL CONCEPT À LA FOIS** (voir contrainte 5) : présente-le en entier, attends le retour, puis passe au suivant. Ne déballe pas 5 concepts d'un coup.

Directions possibles (liste non limitative, inspire-toi mais dépasse) :
- **Barres modernisées** (mais repensées : hiérarchie, densité, micro-typo).
- **Radial / circulaire** (jauges rondes, cadrans, orbites).
- **Ultra-condensé** (mono-ligne très dense, « ticker », tout en une bande).
- **Hybrides** (ex. HP en barre + TP en radial + buffs en micro-grille).
- Toute idée moderne (glassmorphism sobre, neon HUD, minimal éditorial…).

> ⚠️ **PAS** de « cartes / tuiles séparées » (une carte par joueur avec des trous entre) : le fond doit rester **un bloc opaque contigu** (contrainte 1). Les membres sont des **lignes d'un même panneau plein**, pas des vignettes isolées.

**Chaque concept doit :**
1. **Couvrir le footprint natif** en **bloc opaque contigu** (grandir vers le haut, s'ancrer en bas, fractions d'écran, tenir en 16:9 / 21:9 / 32:9 / 4:3).
2. Afficher la **data critique lisible d'un coup d'œil** (HP dominant, cible évidente).
3. **Scaler de 1 à 6 membres** proprement, en **une box réutilisable** + sa **variante alliance condensée** (sans buffs) — **3 boxes indépendantes**, pas une pile de 18.
4. Optimiser **lisibilité de combat + densité** (18 lignes possibles).

---

## 6. **Livrables attendus** — **un concept à la fois**

Présente **un seul concept par message**, complet, puis attends le retour avant le suivant. Pour le concept livré, fournis :

1. **Nom + pitch** (1 phrase : l'idée et pour qui c'est le meilleur).
2. **Maquette** — ASCII art et/ou description spatiale précise : **où va chaque donnée** (nom, HP, MP, TP, job, distance, cast, buffs, marqueurs, curseur cible).
3. **Logique de dimensionnement** — comment la box **scale de 1 à 6 membres**, comment elle **couvre le natif**, et à quoi ressemble la **version alliance** (plus dense, sans buffs).
4. **Hiérarchie visuelle** — ce qui domine, ce qui est secondaire, comment l'œil circule.
5. **Style visuel** — palette, formes, matières, animations (WS-ready, alarme HP, cast, ciblage).
6. **Assets nécessaires** — **liste explicite** des **textures / icônes / polices custom** requises, avec description (contenu, format, animation). *Rappel : on peut les fabriquer.*
7. **Faisabilité DX8** — ce qui est vectoriel (quads/arcs/disques AA, glow) vs texturé.
8. **Lisibilité & gameplay** — **pour / contre**, dans quels cas ce concept brille, ses limites.

**Bonus appréciés :** variantes de **densité** (confort ↔ compact), gestion des **états** (KO, hors-zone, hors portée), micro-animations qui **servent l'info** (pas décoratives).

---

## 7. Annexe — repères (indicatifs, tu peux t'en écarter)

**Sémantique couleur actuelle** (à conserver ou réinterpréter, mais garder le sens) :
- **HP** : dégradé vert (plein) → jaune (~50 %) → orange (~25 %) → rouge (vide) ; **alarme ≤ 25 %**.
- **MP** : bleu.
- **TP** : terne sous 1000 ; **rose vif + glow ≥ 1000** (WS prêt) ; état spécial à 3000.
- **Distance** : bleu (à portée) / jaune (marginal) / rouge (hors portée).
- **Marqueurs** : *alliance leader* = blanc, *party leader* = jaune, *quartermaster* = vert.
- **Cible** : cadre/curseur or pour `<t>`, bleu océan pour `<st>`.
- **États** : nom rouge = KO, grisé = hors-zone, voile sombre = hors portée.

**Rappels techniques** : DX8 fixed-function ; quads dégradés + glow additif + formes vectorielles AA (disque/anneau/segment/losange/triangle) + textures bitmap custom possibles ; ~18 lignes/frame ; tout en fractions d'écran (résolution-indépendant) ; alliance sans buffs ; party a une boîte flottante « Cost / Next ».

**Objectif final** : l'**UI de party la plus moderne et lisible possible** pour FFXI — qui recouvre le natif, tient la densité de 18 membres, et se lit **d'un coup d'œil** en plein combat. Ose. Demande les assets dont tu as besoin.

## See also
- [Party visual system & config](party-visual-system.md)
- [Edit-layout GUIDES & ZONES](edit-zones.md)
- [Member buffs](../game-data/member-buffs.md)
- [Cast bar](../game-data/cast-bar.md)
- [HUD design](../tech-stack/hud-design.md)
