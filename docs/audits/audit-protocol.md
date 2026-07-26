---
title: Protocole d'audit AioHUD — le prompt, et les règles qui le rendent utile
summary: Le brief technique à donner aux auditeurs (humains ou agents) pour un audit complet. Standard de preuve, axes, barème de sévérité, format de sortie, et la liste de ce qui est DÉJÀ tranché et ne doit pas être re-litigé.
---

# Protocole d'audit AioHUD

Ce document est le **prompt**. Il se donne tel quel à un auditeur — un agent par axe, ou une personne qui
prend les axes l'un après l'autre. Il est écrit pour ce projet précis : un auditeur générique produit des
constats génériques, et trois audits ont déjà montré que c'est du bruit.

> **Règle zéro.** Un audit qui ne trouve que des défauts anciens n'a pas fait son travail. Le code le plus
> récent est le moins relu, donc le plus suspect. Commence par `git log` et attaque ce qui a bougé.

---

## 0. Mission

Tu audites **AioHUD**, un plugin overlay **C++ 32 bits, Direct3D 8 fixed-function**, injecté dans le
processus de Final Fantasy XI via Windower 4. Tu cherches des **défauts réels, démontrables**, pas des
opportunités d'amélioration. Ta sortie sert à décider quoi corriger cette semaine.

Le critère de succès n'est pas le nombre de constats. C'est : **combien de tes constats survivent à une
tentative sérieuse de réfutation, et combien auraient été trouvés autrement.**

---

## 1. Le système, en faits (ne les redécouvre pas)

| | |
|---|---|
| Taille | ~33 000 lignes hors tables générées, 133 fichiers `src/` + `include/` |
| Plus gros fichiers | `config_page.cpp` 1827, `party_state.cpp` 1812, `party.cpp` 1295, `ui_config.cpp` 1241, `hud_timers.cpp` 1174, `game_mem.cpp` 1063, `aiohud.cpp` 1057 |
| Contrainte plateforme | D3D8 **fixed-function** : aucun shader. L'anti-aliasing est de la géométrie feathered à la main |
| Modèle d'exécution | Rendu mono-thread dans le processus du jeu ; un mauvais pointeur **crashe le client**, pas un script |
| Couches | `ui → model + gfx` ; `model → rien de ui/gfx` ; `gfx → rien`. Cette règle est vérifiable et doit l'être |
| Sources de données | Lecture mémoire du client sous SEH + hooks de paquets. Rien n'est fiable par construction |
| Tests | Une suite offline (75 checks) sur le cœur pur, bloquante en CI. **Aucun test de rendu, aucun test d'intégration** |
| Build / release | `build.bat` (MSVC), `tests.bat`, `package.bat`. La CI construit la release **depuis le tag poussé** : seul ce qui est commité est publié |
| Diagnostic | Sondes `//aio <cmd>` écrivant dans `Windower\plugins\aiohud_debug.log`. `aiohud_probes.cpp` est **untracked** → absent des releases |
| Docs | `docs/<sujet>/`, un sujet par fichier. `/docs/*` est gitignore **sauf** `game-data/` et `audits/` |

**Contexte terrain qui change les priorités :** il y a un testeur sur une install **NA sous Program Files**.
Plusieurs bugs ne se sont manifestés que chez lui (variance de région/chemin, DAT temporairement illisibles).
Un défaut qui ne se voit pas sur la machine du dev n'est pas un défaut mineur.

---

## 2. Standard de preuve — non négociable

Un constat sans ces quatre éléments n'est pas un constat, c'est une opinion. Il sera jeté.

1. **Localisation** — `fichier:ligne`, avec le code cité (3-10 lignes). Pas de « quelque part dans le module X ».
2. **Scénario d'échec concret** — des entrées ou un état précis → le comportement erroné observable.
   « Cette fonction pourrait déborder » est rejeté. « Avec `n = 17`, la boucle écrit `arr[17]` sur un tableau
   de 16, écrasant `y` déclaré juste après » est accepté.
3. **Confiance déclarée** — `CONFIRMÉ` (tu as relu le code appelant et la donnée, le chemin est atteignable)
   ou `PLAUSIBLE` (le défaut est réel en lecture mais tu n'as pas pu prouver l'atteignabilité). Aucun autre niveau.
4. **Tentative de réfutation** — pour chaque constat, écris **ce que tu as essayé pour te prouver que tu as
   tort**, et pourquoi ça n'a pas marché. Si tu n'as pas essayé, marque `NON RÉFUTÉ` : c'est un aveu, pas une note.

**Interdits explicites** (ils ont tous été produits par des audits précédents et ont coûté du temps) :

- « Envisager d'ajouter des tests » sans nommer **le test précis** et **le bug qu'il aurait attrapé**.
- « Ce fichier est trop long / cette fonction est trop grosse » sans montrer un **défaut** que la taille cause.
- Des métriques sans défaut (« 47 occurrences de X ») — sauf si le nombre EST le problème et que tu le démontres.
- Recopier un commentaire du code comme s'il s'agissait de ta découverte. Le code est très commenté ; cite-le
  comme source si tu t'appuies dessus.
- Signaler un comportement **documenté comme intentionnel** sans traiter l'argument écrit sur place.

**Sur-vente : le vrai risque.** Au dernier audit, 4 affirmations d'agents étaient sur-vendues et ont dû être
corrigées après relecture. Un constat faux coûte plus cher qu'un constat manqué : il consomme une correction,
et une « correction » d'un non-bug introduit un vrai bug. Quand tu hésites, descends la confiance.

---

## 3. Déjà tranché — ne pas re-litiger

Ces points ont été analysés, décidés, et la décision est écrite dans le code ou dans un rapport. Les
re-signaler comme des défauts est du bruit. Tu peux les contester **uniquement** avec un élément nouveau,
et alors tu dois traiter l'argument d'origine explicitement.

| Sujet | Décision | Où c'est écrit |
|---|---|---|
| `clang-tidy` | Outil **local**, délibérément **hors CI** | `scripts/README.md`, commit `2a72ba8` |
| Modèle additif pour la durée Regen | **Testé, pire** que le multiplicatif. Ne pas y revenir | `docs/game-data/buffs-and-timers/buffs-on-allies.md` |
| Cycle d'écrasement des DoT (Burn→Frost→…) | La boucle de retrait est **correcte** ; réfuté en jeu | `docs/notes/audit-technique-2026-07-26.md` |
| `TexRetry` par handle pour tout **sauf** le buff atlas | Un propriétaire unique pour l'atlas, per-handle ailleurs | `src/ui/tex_retry.h` |
| Pas de `.clang-format` | Le formatage est déjà uniforme (0 tabulation, 0 Allman) | audit 2026-07-26 |
| Indi- : pas de ligne par allié (hors Entrust) | L'aura suit le porteur, il n'y a rien à décompter | `docs/game-data/buffs-and-timers/geomancy-duration.md` |
| Durées de buffs alliés estimées | **RE-prouvé** : le client n'a aucun timer pour autrui | mémoire + `buffs-on-allies.md` |

Il y a ~27 blocs marqués `deliberately` / `do not` / `Superseded` dans le code. **Cherche-les avant de
signaler quoi que ce soit** : `grep -rn "deliberately\|DELIBERATE\|do not switch\|Superseded" src/`.

**Lis aussi**, avant de scanner quoi que ce soit : `docs/notes/audit-global-2026-07-25.md` §10 (garde-fous
proposés) et §11 (bilan de l'audit précédent), et `docs/notes/audit-technique-2026-07-26.md`. Beaucoup de ce
qu'ils listent a été corrigé depuis — vérifie avant de le re-signaler.

---

## 4. Priorité : le code récent

Avant tout scan large, fais `git log --oneline -30` et relis **en priorité** ce qui a changé la semaine
passée. Ces changements-ci sont neufs, peu relus, et chacun a une hypothèse de risque nommée :

| Changement | L'hypothèse à attaquer |
|---|---|
| Atlas d'icônes à **propriétaire unique** (`buff_atlas.cpp`) | 9 consommateurs empruntent un handle. Y a-t-il un chemin où un consommateur l'utilise **après** `forget`/`dispose` ? Un ordre de destruction où le HUD libère avant qu'un widget ne dessine ? Un consommateur qui le stocke plus longtemps qu'une frame ? |
| **Plateau de retry** (minimap, gear icons) | Le coût au plateau est-il vraiment négligeable **dans le pire cas** (16 slots bloqués × décodes/frame) ? Le compteur peut-il déborder ? Le sentinel 255 reste-t-il inatteignable par saturation ? |
| `ROW_TOGGLE` / `ROW_CHOICE` (42 lignes converties) | Une ligne convertie a-t-elle changé de géométrie ? Deux contrôles partagent-ils un `CTRL_ID` ? Un champ `bool` passé en `int*` quelque part ? |
| Sérialiseur `TextStyle` | Round-trip **exact** ? Une clé écrite mais non relue ? Le préfixe `text` / `textP` / `textA` est-il vraiment sans collision ? |
| `geoReplaced` (Timers/Indi-) | Peut-il supprimer une alerte **légitime** ? Le repli par statut 542-556 peut-il attraper un buff non-géomancie ? |
| Correctif debuffs `eb5b457` | La suppression est inférée sur tout le set : peut-elle effacer une ligne vivante ? |

---

## 5. Les axes

Un auditeur par axe. Chaque axe liste **ce qu'il faut aller regarder**, pas des généralités. Si un axe ne
produit rien, dis-le explicitement — un résultat négatif documenté évite de re-scanner au prochain audit.

### A. Invariants de correction & règle 10
La règle la plus coûteuse du projet : **un échec transitoire ne doit jamais devenir un état permanent, ni
silencieux.** Six bugs utilisateurs en une seule journée en sont venus.
- Cherche `if (!tried)`, `= true;` de latch, `= 255`, budget épuisé sans reprise, `static bool done`.
- Pour chaque : que se passe-t-il si la **première** tentative rate ? Y a-t-il un événement qui réarme ?
- « Vide » vs « indisponible » : un lecteur qui renvoie 0/liste vide pour un échec **et** pour un vrai vide.
  Cherche les retours sans drapeau `ok`/`valid`, et les appelants qui traitent 0 comme une autorité.
- « Succès partiel » : un chemin qui valide une étape intermédiaire au lieu du produit final.
- Corollaire : le **chemin de succès** est-il instrumenté ? Une sonde qui ne loggue que l'échec ne prouve rien.

### B. Durée de vie GPU & cycle du device (D3D8)
- `on_device_lost()` doit **oublier** (mettre à 0), **jamais** `Release` — le device peut être mort.
  `dispose()` libère (device vivant). `ensure()` recrée paresseusement.
- Inventorie **chaque** handle de texture : qui le crée, qui l'oublie, qui le libère. Cherche l'asymétrie :
  un `forget` sans `dispose` (fuite par cycle unload/load) ou un `dispose` sans `forget` (handle mort réutilisé).
- Handles **empruntés** (nouveauté) : un consommateur qui libère ce qu'il n'a pas créé = double Release.
- État D3D laissé sale : blend additif non réinitialisé, texture encore bindée pour le widget suivant.

### C. Lecture mémoire & parsing de paquets
- **Chaque** lecture mémoire du jeu doit être sous SEH (`safe_read` / bloc gardé) et validée (`valid_ptr`).
  Un mauvais pointeur doit dégrader en no-op. Cherche une déréférence nue.
- **Marche à pas variable** : la position des cibles dans un paquet d'action dépend du nombre d'effets.
  Toute lecture à pas FIXE (`base + i * 123`) est un bug latent — il en restait une en juillet.
- Bornes : indices issus du paquet utilisés dans un tableau fixe sans clamp.
- Une seule source de vérité par offset. Un offset dupliqué qui dérive est invisible jusqu'à un patch client.

### D. Performance & hygiène de rendu
- **Aucune allocation par frame.** Cherche `new`, `malloc`, `std::string`, `std::vector` dans un chemin de draw.
- Travail fait à 60 Hz qui pourrait être fait une fois : recherche linéaire par frame, `strlen` en boucle,
  scan d'entités, `GetFileAttributes`, ouverture de fichier.
- Nombre de draw calls et changements d'état par frame : un widget qui bind/unbind par icône.
- Écriture disque dans un chemin interactif (le drag de slider écrivait la config à 60 Hz — corrigé, vérifie
  qu'il n'y a pas d'équivalent ailleurs).

### E. Config, persistance & compatibilité
- **Round-trip** : toute clé écrite est-elle relue, avec le même tableau et la même taille ?
- Compatibilité ascendante : un fichier d'une version antérieure charge-t-il ? Les champs ajoutés en fin de
  ligne gardent-ils un défaut sain quand la ligne est courte ?
- Écriture atomique : un fichier lu par un autre client pendant l'écriture ne doit pas être vu tronqué.
- Un échec d'écriture doit être **rapporté**, jamais annoncé comme un succès.

### F. Frontière plugin & variance de région/version
- L'ABI Windower est reversée : toute hypothèse sur les valeurs passées aux callbacks doit être défendable.
  Les builds NA et EU passent des valeurs **différentes** aux mêmes callbacks (cf. `reference/keyboard-input.md`).
- Chemins : tout chemin relatif est un bug potentiel sous Program Files. Les chemins doivent dériver du DLL.
- Registre : les recherches d'install doivent couvrir le même ensemble de clés partout (elles ont divergé).

### G. Build, CI, release, chaîne d'approvisionnement
- La CI construit depuis le **tag** : quelque chose de non commité peut-il changer le binaire publié ?
- `aiohud_probes.cpp` est untracked : quelle capacité de diagnostic **manque** dans une release ? (C'est un
  défaut de production, pas un détail : un testeur ne peut pas capturer ce qui n'est pas compilé chez lui.)
- L'updater : peut-il downgrader, écraser une build locale, échouer silencieusement ?
- Le build est-il reproductible ? Y a-t-il un warning désactivé qui masque un vrai bug (`/W4` sans `/WX`) ?

### H. Observabilité & diagnosticabilité
Axe prioritaire, et le plus négligé. Question centrale :
> **Un bug rapporté par un testeur peut-il être tranché avec UN seul fichier de log, sans aller-retour ?**
- Pour chaque symptôme utilisateur plausible (icône manquante, carte noire, timer faux, ligne absente),
  demande-toi : quelle ligne de log l'explique ? Est-elle **compilée dans la release** ? Faut-il l'armer avant
  que le bug arrive (piège : on ne peut pas armer une sonde après coup si l'état a déjà abandonné) ?
- Les logs derrière `#ifdef AIOHUD_PROBES` sont **absents des releases**. Chaque log de défaillance
  permanente devrait être hors du garde. Vérifie-les un par un.

### I. Testabilité
- La suite offline couvre le cœur pur. **Qu'est-ce qui est testable sans le jeu et n'est pas testé ?**
  (parsing de config, tables générées, math de durée, JSON, skillchain, géométrie de layout.)
- Pour chaque bug corrigé le mois dernier : un test offline l'aurait-il attrapé ? Si oui, il manque.
- Ne propose pas de « framework de test » ; propose des cas nommés dans la suite existante.

### J. Documentation & intégrité de la connaissance
- La doc dit-elle encore la vérité ? Les références `fichier:ligne` dérivent (plusieurs pointaient vers
  `hud.cpp` après un split). Échantillonne-les et vérifie.
- Une fonctionnalité retirée toujours documentée, ou une règle documentée que le code ne suit plus.
- `CLAUDE.md` est-il exact ? Il s'est déjà trompé sur les sondes livrées.

### K. Robustesse aux entrées non fiables
Tout ce qui entre vient du jeu, du disque, ou d'un autre client : paquets, `layout.json`, config, profils,
DAT du jeu, réponses de l'updater.
- Bornes de récursion, tailles de buffers, `%s` sur des données non terminées, `sprintf` vers un tableau court.
- Un fichier corrompu doit dégrader proprement, jamais crasher le client du joueur.

### L. Lisibilité & surface d'API
Le « readability review » : est-ce que quelqu'un d'autre pourrait maintenir ça ?
- Fonctions dont le nom ment sur ce qu'elles font ou sur leurs effets de bord.
- Duplication **sémantique** (deux implémentations de la même règle qui peuvent diverger) — plus grave que la
  duplication syntaxique.
- Un `draw()` qui mute l'état du modèle (déjà vu : `draw` gelait un invariant quand la boîte était masquée).

---

## 6. Barème de sévérité

| Niveau | Définition | Exemple réel du projet |
|---|---|---|
| **S0** | Crash du client, corruption ou perte de données du joueur | débordement de pile dans le parseur JSON sur un `layout.json` corrompu |
| **S1** | Comportement faux **visible** par l'utilisateur, ou état cassé qui persiste | un Indi- remplacé qui reste en « OUT » permanent |
| **S2** | Dégradé, latent, ou visible seulement dans une configuration particulière | une marche à pas fixe qui perd des lignes sur un cast AoE chargé |
| **S3** | Maintenabilité : rien de cassé aujourd'hui, mais le prochain changement se trompera | une règle implémentée à deux endroits qui peuvent diverger |

Un S3 est légitime **s'il nomme le changement futur qui se plantera**. Sinon c'est une préférence de style.

---

## 7. Format de sortie

Un fichier par auditeur, puis une synthèse. Structure imposée :

```
# Audit <axe> — <date>

## Couverture
Fichiers lus INTÉGRALEMENT : …
Fichiers survolés (et pourquoi) : …
Ce que je n'ai PAS pu couvrir : …          <- obligatoire, une omission tue un audit

## Constats
### [S1] Titre court et factuel
- Où : src/ui/foo.cpp:123-130
- Code :
      <citation>
- Scénario d'échec : <entrées/état précis → comportement observable>
- Pourquoi c'est réel : <le chemin d'appel, la donnée qui y arrive>
- Réfutation tentée : <ce que j'ai essayé pour me prouver que j'ai tort, et pourquoi ça tient>
- Confiance : CONFIRMÉ | PLAUSIBLE
- Correction suggérée : <une phrase — le diff n'est PAS demandé>

## Vérifié et SAIN
<liste courte : ce que tu as regardé sérieusement sans rien trouver. Évite de re-scanner au prochain audit.>
```

**La section « Vérifié et SAIN » est obligatoire.** Sans elle, l'audit suivant repart de zéro sur le même
terrain — c'est exactement ce qui s'est passé entre les audits du 23 et du 25 juillet.

---

## 8. Passe de vérification adversariale

Après la collecte, **avant** la synthèse : chaque constat de sévérité S0/S1 passe devant un auditeur
**différent** dont la seule mission est de le **réfuter**. Consigne à cet auditeur :

> Ce constat est probablement faux. Trouve pourquoi. Relis le code appelant, cherche le garde en amont,
> cherche le commentaire qui explique que c'est intentionnel, cherche le test qui le couvre. Conclus
> `RÉFUTÉ` (avec la preuve) ou `TIENT` (avec ce que tu as vérifié). En cas de doute, `RÉFUTÉ`.

Un constat qui ne survit pas est **retiré du rapport**, pas rétrogradé — mais il est listé dans une section
« réfutés » avec la raison, pour que personne ne le retrouve dans six mois et le re-signale.

---

## 9. Livrables

1. `docs/notes/audit-<date>.md` — le rapport complet (dossier de travail, gitignore).
2. Une **synthèse triée par sévérité**, avec pour chaque entrée : sévérité, confiance, coût estimé de la
   correction (S/M/L), et si un test offline peut la verrouiller.
3. Une section **« bilan de l'audit précédent »** : ce qui a été corrigé depuis, ce qui reste ouvert, ce qui a
   été explicitement décidé de ne pas faire.
4. La mise à jour de ce protocole si tu as trouvé une catégorie de défaut qu'il ne demandait pas de chercher.

---

## 10. Ce qui rend cet audit différent des trois précédents

- Il commence par le **code récent**, pas par un scan uniforme.
- Il exige une **tentative de réfutation** par constat, et une passe adversariale sur les S0/S1.
- Il exige de documenter les **résultats négatifs**.
- Il traite la **diagnosticabilité** (axe H) comme un axe de premier plan : un bug qu'un testeur ne peut pas
  capturer coûte plus cher qu'un bug moyen, et c'est le mode de défaillance dominant de ce projet.
- Il interdit de re-litiger les décisions déjà écrites, tout en autorisant à les contester **avec un fait neuf**.
