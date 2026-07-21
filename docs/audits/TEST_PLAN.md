# AioHUD — Plan de test en jeu

Checklist pour valider les changements de la session (fixes UI, audit 🔴🟠🟡🟢🔵, icônes de job,
couleurs de rôle, rangement assets). Chaque test cible des changements précis.

**Déploie d'abord :**
```
//unload AioHud   →   deploy.bat   →   //load AioHud
```
Si `//load` crashe ou n'affiche rien → problème de base (SEH/init) : stoppe et signale-le.

---

## 0. Smoke (base)
- [O] Le plugin charge, la party s'affiche **instantanément** au load (pas d'attente de paquets).
- [O] Change de zone une fois → la party se **ré-affiche** correctement après le zoning (device-lost + rechargement textures).

## 1. Rendu party/alliance — RÉGRESSION (ne doit RIEN changer visuellement)
*Vérifie : batching coins `rrect`, `setup_tex_state`, `party_gauge`→`setup_color_state`, font bind 1×.*
- [ ] Boîtes party / alliance 1 / alliance 2 : coins arrondis nets, fond opaque, **pas de "ligne noire"** haut/bas, pas de coin bizarre.
- [O] Jauges HP/MP/TP dans **tous** les styles (Config → Gauge Style) : Fiole, Barres, Segments, Minimal, Sphère, Anneau, Cristal, Texte — remplissage correct, bout plat + level line, pas de scintillement.
- [O] **HP ≤ 25 %** → clignotement rouge. **TP ≥ 1000** → pulse rose (WS-ready). Glows respirent normalement.
- [O] Texte (noms, valeurs) : net, contours OK, pas de flou.
- [O] Ouvre le **Help** (config, jauges live après du texte) → jauges s'affichent correctement.

## 2. Icônes de job + couleurs de rôle — NOUVEAU
*Config → Job Badge → Icons.*
- [O] Job Badge = **Icons** : emblèmes **remplissent** la case (plus de grosse marge), **centrés**.
- [O] Couleurs : **WHM/RDM/SMN = vert**, **GEO/COR/BRD = jaune**, **RUN/PLD = bleu**, **reste = rouge**. Bordure du badge = même couleur.
- [O] Slider **Badge Size** (apparaît seulement si Job Badge ≠ Off) 50 %→200 % → badge grossit/rétrécit, la box se ré-adapte.
- [O] Job Badge = **Off / Main / Main+Sub** → icône disparaît, texte du job s'affiche, pas de crash.

## 3. Config UI — flèches & toggles
*Vérifie : collision de slot hover corrigée, animations flèches retirées, bordure toggle state-aware.*
- [O] Survole la flèche **droite `›` de Job Badge** → la flèche gauche `‹` de **Gauge Style ne clignote PAS** (le bug d'origine).
- [O] Flèches `‹ ›` au survol : **pas d'animation** (teinte instantanée, pas de balayage/pulse).
- [O] Toggles verts (HP on / TP on / Casts on…) au survol → **bordure verte** (rouge si OFF), pas blanche.
- [O] Push buttons (Éditer Dispo / Default / Save) au survol → balayage vitré + halo (inchangé).

## 4. Profils + commandes `//aio` — BUGS CORRIGÉS
*Dispatch profile-first, catch-all guardé.*
- [O] `//aio profile save mydemo` → sauvegarde un profil **"mydemo"** (avant : capté par la branche demo). Vérifie via `//aio profile list`.
- [O] `//aio profile save myconfig` puis `//aio profile load myconfig` → charge bien (nom contenant "config").
- [O] `//aio profile list` avec **plusieurs profils aux noms longs** → s'affiche sans crash (pas d'overflow).
- [O] `//aio xyzblabla` (bidon, **sans chiffre**) → affiche `unknown command`, **ne touche PAS** HP/MP/TP.
- [X] `//aio hp 50` et `//aio 100 50 30` → remplissent toujours HP/MP/TP.
Ici ce n'est que pour les grosse fiole qu'on affiche plus.
- [O] `//aio party demo`, `//aio alliance1 demo`, `//aio sim 2` → marchent comme avant.
`//aio party demo`, `//aio alliance1 demo`, //aio sim 2 ne fait rien

## 5. Édition de layout + zones
*persist_eq zonePanel, parse zone.*
- [O] `//aio edit` → **drag une box** puis quitte → position gardée.
- [O] Active **Rules**, **déplace le panneau Zones** → l'indicateur "non sauvegardé" (Save doré) **s'allume**.
- [O] Crée une **zone**, renomme-la avec un **nom commençant par un chiffre** (« 2nd wall »), save, `//unload`+`//load` → nom **rechargé intact** (pas tronqué, pas de rôle parasite).

## 6. Casting bar
*getbits borné.*
- [O] Un membre **incante un sort** → la **cast bar** apparaît, bon nom + progression (la détection de cast n'est pas cassée).
pas de barre juste le nom

## 7. Skin de fenêtre
*draw_window état 2D complet.*
- [O] Active un **thème de cadre** (Config → Box Theme, ≠ None) → cadre 9-slice correct, **pas de disparition/coupe**. Change de thème plusieurs fois.

## 8. Distance
*entity_xz factorisé.*
- [O] Active **Distance** → chaque membre affiche sa distance (yalms), couleur par palier (bleu/jaune/rouge). Éloigne-toi → chiffre monte, rouge > ~20.8.

## 9. Curseur / cible / marqueurs / buffs
*setup_tex_state (curseur, dots, buffs).*
- [O] Cible un membre (`<t>`) → **curseur main** dessus. Sous-cible (`<st>`) → curseur bleu sur le bon membre. Le curseur **glisse** entre les lignes.
- [O] **Menu → Party → Distribution** → le **cadre de sélection** suit le membre survolé (même hors zone).
- [O] Marqueurs Leader / Alliance / QM s'affichent, animation pop OK.
Par contre si on est lock sur un joueur ça ne fonctionne pas.
- [O] **Buffs** (party only) à gauche, 2 rangées, nets.

## 10. Robustesse (edge cases)
- [O] Change de zone plusieurs fois d'affilée, entre/sors de party, invoque/renvoie des **trusts** → pas de crash, roster toujours correct (packet-safety + roster + save-throttle).
- [O] Reste 5-10 min en combat, party pleine → **pas de micro-freezes** réguliers (`save()` ne s'écrit plus à chaque paquet 0x0DD).

---

## Priorité si peu de temps
Les plus critiques (crash / régression fonctionnelle) : **0** (smoke + zoning), **4** (profils/commandes),
**6** (cast), **7** (skin), **10** (stabilité). Le reste est surtout visuel.

En cas d'échec : note le **numéro du test** + ce que tu vois → ça pointe directement le commit concerné.

## Correspondance test → commit
| Test | Commit(s) concerné(s) |
|---|---|
| 0, 10 | `161a2ff` 🔴 safety (packet SEH, create_locked, tier_ clamp) |
| 1, 9 | `e81a821` 🟢 (rrect batching, font bind) · `39b45ad` 🟡 (setup_tex_state) · `e1c9bf5` 🔵 (setup_color_state) |
| 2 | `a0a4880` (role colours) + les fixes icônes/badge-size antérieurs |
| 3 | fixes UI (uid stepper, toggles, arrow anim) |
| 4 | `87c2e4d` 🟠 (dispatch profile-first, catch-all) + `161a2ff` (profile _snprintf) |
| 5 | `87c2e4d` 🟠 (persist_eq zonePanel, parse zone) |
| 6 | `87c2e4d` 🟠 (getbits borné) |
| 7 | `87c2e4d` 🟠 (draw_window état 2D) |
| 8 | `120b6ae` 🟡 (entity_xz) · `28fffa3` 🟡 (data_root) |
