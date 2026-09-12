---
title: Audit complet du 2026-09-12 — synthèse
summary: Huit axes en parallèle selon docs/audits/audit-protocol.md, 93 constats, passe adverse sur les S1. Ce fichier est la synthèse ; les rapports par axe sont à côté.
---

# Audit complet — 2026-09-12

Huit auditeurs en parallèle, un par axe du protocole (`docs/audits/audit-protocol.md`), chacun avec son
périmètre de fichiers et la consigne d'attaquer d'abord le code du jour. **93 constats**, dont 5 annoncés S1.
Puis la **passe adverse du §8** sur chaque S1 — l'étape que les trois audits précédents avaient sautée. Elle a
retiré un constat et descendu un autre : c'est exactement son rendement attendu.

Rapports par axe (dossier de travail NON suivi, ) : `A-invariants`, `B-gpu-rendu`, `C-memoire-paquets`, `E-config`,
`FG-plugin-build`, `HI-observabilite-tests`, `J-docs`, `K-entrees-hostiles`.

---

## 1. Le contexte qui a orienté l'audit

Quatre changements datant du jour même ont été attaqués en premier (règle zéro : le code récent est le moins
relu). **Trois d'entre eux portaient un défaut**, et c'est le meilleur argument pour avoir commencé par là :

| Changement du jour | Ce que l'audit y a trouvé |
|---|---|
| Le chargement de config REMPLACE (`5164129`) | **S1** : il expose 3 champs que `persist_eq` ne compare pas → réglage perdu au disque |
| Le clip par sous-viewport (`e1501d6`) | **S2** : les contournements du clip mort subsistent et sont devenus faux ; 9 sites de `rrect_clip_*` non convertis |
| `capwatch` sur 12 tables (`88e0093`) | **S1** : 2 des 12 sont dans `#ifdef AIOHUD_PROBES` ; **S3** : `CAP_HOLD` mal contracté au site `ease()`, `watch_tables()` pas « une fois par frame » |
| La retraite de `tmTrackOff` (non commité) | rien — aucun résidu, et le test des clés retirées vise le bon risque (l'ombrage de préfixe) |

---

## 2. Les S1, après la passe adverse

| # | Constat | Verdict adverse | État |
|---|---|---|---|
| 1 | `iconPack` / `tmSortDur` / `tmSortRec` écrits mais absents de `persist_eq` → le choix de planche d'icônes revenait au défaut à chaque relance, **et était détruit sur le disque** par la ré-écriture de `config.txt` au démarrage | vérifié, **tient** | **corrigé** + verrou par octets |
| 2 | Les 3 veilleurs Timers dans `#ifdef AIOHUD_PROBES` → absents de toute release (`//aio watch` : 12 ici, 10 chez un testeur) | vérifié (imbrication du préprocesseur), **tient** | **corrigé**, forme release recompilée |
| 3 | `//aio doctor` ne consulte jamais le registre `selftest`, et le veilleur périodique est opt-in → les 13 diagnostics passifs ne tournaient chez personne | vérifié (un seul appelant de `selftest_run_now`), **tient** | **corrigé** |
| 4 | Le cache d'état se désynchronise au-delà de 32 buffs alliés | **TIENT, descendu en S2** : aucun accès hors bornes ; corruption d'état réparable au prochain 0x063 | **corrigé** (cap + saut de section) |
| 5 | `layout.json` lu sans plafond → crash au démarrage | **RÉFUTÉ** : `/EHsc-` = `/EHs` + `/EHc-`, donc EH C++ **actif** et `__except` attrape (mesuré, `0xE06D7363`) ; seuil réel ~1,8 Go ; le fichier fait 8,5 Ko et est réécrit par `deploy.bat` | reclassé **S3** (le défaut est le commentaire qui prétend une symétrie avec `texture.cpp`, plafonné à 1 Mo) |

**Ce que la passe adverse a corrigé chez moi aussi** : j'aurais accepté l'argument `/EHsc-` sans le mesurer.

---

## 3. Les grappes — une cause, plusieurs symptômes

### 3.1 Le stencil mort (B, A, J)
`minimap.cpp:679` mesure qu'aucun depth-stencil n'est lié au moment du dessin. Le clip de la config a été
porté sur un sous-viewport le 2026-09-12 ; **`rrect_clip_begin/end` ne l'a pas été** et garde 9 sites vivants
(`target.cpp` ×6, `liquid_bars.cpp:609`) : masque inopérant **et** 8 `DrawPrimitiveUP` gaspillés par appel,
≈ 430 par frame en alliance avec le style Vial. Trois conséquences supplémentaires :
- les contournements écrits pour le clip mort subsistent (`config_page.cpp:1036`, `:2203`, `:2288`) et sont
  devenus nuisibles : dans l'onglet Update, un release ouvert perd sa carte et son titre au premier cran de
  molette, le texte flotte ;
- `docs/tech-stack/stencil.md` et `d3d8-rendering.md` documentent le mécanisme comme fonctionnel ;
- le clip viewport de la minimap est hors pile, sans restauration panique, avec une règle d'arrondi différente.

### 3.2 Le harnais en jeu, trois trous en série (A, H+I, E)
Compilé hors des releases (S1-2) · jamais consulté par `doctor` (S1-3) · et l'anti-doublon du registre
s'indexe sur un identifiant **constant**, donc seul le premier constat de chaque espèce atteint un rapport
(`selftest.cpp:80-88`). Chacun suffit à annuler les deux autres. S'y ajoutent : `g_fmPeak` échantillonné
seulement dans le check (30 s) alors que son jumeau l'est par frame ; `selftest_add`/`seen_for` qui débordent
en silence — les deux seules tables du projet à le faire, au sommet du dispositif qui existe pour ça.

### 3.3 Le travail répété à 60 Hz sur un échec (B+D)
`ensure_materials` rejoue 4 textures procédurales par frame (~8 M opérations) ; `minimap_help_moon` re-cuit
25 lunes indéfiniment ; `WindowSkin::load` tente 20 à 32 `CreateFileA` par frame pour toute la session.
Aucune n'a de plateau, alors que le projet applique ce motif ailleurs (`tex_retry`, `retry_clock`).

---

## 4. Deux mesures sur le processus lui-même

- **57 % des références `fichier:ligne` des docs vivantes sont fausses** (26 sur 46 vérifiées une par une),
  dont trois déjà signalées le 2026-07-25 et qui ont **dérivé davantage** depuis.
- **Le repli VS2017 épinglé par `build.bat` ne compile pas le projet** (2 × C3493, mesuré). Et aucun PDB
  n'est produit ni publié : un crash de testeur est illisible à jamais.
- Le protocole d'audit lui-même sous-estime la suite de tests d'un facteur ~16 (« 75 checks » contre 1216) et
  omet l'audit du 2026-08-06 de sa liste « déjà tranché ».

---

## 5. Corrigé dans la foulée (2026-09-12)

1. `persist_eq` + `config_defaults` : les 3 champs profil manquants, et `selfTest` protégé comme `favColors`.
2. **Nouvel oracle de test** : le round-trip de config se compare désormais **par octets** et non plus via
   `persist_eq` — le comparateur étant écrit à la main, tout test qui s'en sert est aveugle exactement là où il
   est incomplet. C'est ce qui rend la classe entière détectable, pas seulement ces trois champs.
3. Les 3 veilleurs Timers sortis du `#ifdef` ; forme release recompilée pour le prouver.
4. `doctor` consulte le registre, journalise « %d module(s), %d constat(s), veilleur ARMÉ/off », remonte les
   bloqueurs dans le résumé en chat et dit comment armer le périodique.
5. Cache d'état : cap `obn <= OB_MAX` (résidu du passage de 32 à 128 la veille) **et** saut de la section
   rejetée — sans quoi un fichier écrit par un build futur désynchroniserait tout ce qui suit.

---

## 6. Catégorie de défaut à ajouter au protocole (§9.4)

**« Un contournement écrit pour un mécanisme cassé devient un défaut le jour où le mécanisme est réparé. »**
Trois instances ici, toutes nées du clip stencil mort, toutes devenues fausses le même jour. Corollaire pour
l'auditeur : quand un correctif rétablit un mécanisme, chercher les contournements qui le supposaient mort —
ils ne sont pas signalés par le compilateur et leurs commentaires affirment désormais le contraire du vrai.

**Seconde catégorie : le plafond asymétrique.** Un cap relevé aux sites de production et oublié au garde de
lecture d'un cache (`OB_MAX` 32 → 128 le 2026-09-10, garde de lecture inchangé). Le tag de version du cache
n'a pas protégé, parce qu'il n'encode que `sizeof(struct)`, que le changement ne touchait pas.
