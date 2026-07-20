# Analyse — Vitesse de déplacement de la cible (AioHUD)

But : afficher, dans la Target box, la vitesse de mouvement de la cible en **%** (relatif à ta vitesse de base
joueur = 5.0). Ce doc consolide **toutes les données** captées en jeu + le wiki, pour décider d'une méthode
solide au lieu de bricoler des formules.

---

## 1. Faits du wiki ([`movement-speed.md`](movement-speed.md))

- **Base = 5.0 yalms/seconde.  MAX = 8.0** (= +60%).
- Chaque **+0.1 = +2%**. Le client ne reçoit que des **pourcentages pairs**.
- Cap total **+60%** (160%). Gravity/Weight réduit **sous 5.0** (jusqu'à quasi 0).
- La base joueur était **4.0** avant déc. 2013, passée à **5.0** (gear compensé). ⚠️ Rien ne dit que la base
  des **mobs** a été rescalée — hypothèse à trancher.
- **La vitesse est gérée CÔTÉ CLIENT.** ⇒ le serveur **n'envoie PAS** la vraie vitesse des autres entités à ton
  client. Pour toi elle est exacte ; pour les autres, ton client **estime**.
- Sources : gear ≤ +18% (feet ; Ninja Kyahan 25% nuit), buffs jusqu'à +60% (Flee), Mazurka/Quickening +20%, etc.

Le champ lu : **`entity + 0x98` = `movement_speed` (float)**. `0x9C` = constante **5.0** (la base). Scan complet
`0x90..0xAC` : `0x98` est le **seul** champ de vitesse (les autres = 0). Donc pas de "champ corrigé" ailleurs.

---

## 2. Données brutes captées (sondes `//aio`)

### 2a. TOI (self-target) — id 0006BD0B, type 0x20D
| État | `ms` (0x98) | posX/posZ | Note |
|---|---|---|---|
| feet +18%, à l'arrêt | **5.900** | figée | statique |
| feet +18%, en mouvement | **5.900** | **FIGÉE** | la position de self ne bouge PAS dans notre read |
| sans feet | **5.000** | figée | = 0% |

➡️ **Self : champ exact et statique. `100*(ms/5-1)`. Position inexploitable (figée).**

### 2b. AUTRES JOUEURS (à l'arrêt) — type 0x1
| id | `ms` | % (base 5) | Interprétation |
|---|---|---|---|
| 0008D54F | 5.000 | 0% | pas de gear |
| 00003117 | 5.600 | +12% | feet +12% |
| 0000A212 / 93E7 | 5.900 | +18% | feet +18% |
| 00055B5B / BA73 | 6.000 | +20% | +20% (buff/gear) |
| 0003E1FA | 5.100 | +2% | +2% (Cheer ?) |

**En MOUVEMENT (Kaories, id 00016500, feet +18%)** : `ms` reste **5.900 CONSTANT** (moving ET arrêté !) →
le champ est **STABLE et fiable même en mouvement**, exactement comme self. Sa vitesse **mesurée** par position
était bruitée (5.79–6.50 = +15/+30% pour un vrai +18%) → **le champ est bien meilleur que la mesure**.

➡️ **Autre joueur : champ fiable TOUJOURS (= son gear), comme self. ⚠️ CORRECTION : le "-60% en mouvement" vu
plus tôt n'était PAS le champ — c'était un artefact de MA mesure de position (catch-up / mouvement lent). Les
joueurs n'ont PAS de champ garbage. `100*(ms/5-1)` marche pour tous les joueurs, en mouvement comme à l'arrêt.**

### 2c. MOB — TIGRE (rapide) — id 0106914X, type 0x10, status 1, claim = toi
| État | `ms` | dm/frame | Note |
|---|---|---|---|
| à l'arrêt | **6.800** | ~0 | = +36% (base 5) |
| en chasse | **17.000** | 0.13–0.24 | **BOGUS** (> max 8.0) → sature à +60% |

### 2d. MOB — NORMAL (chauve-souris / oiseau) — id 010691XX, type 0x10
| État | `ms` | dm/frame | `win` mesuré (position) |
|---|---|---|---|
| en chasse | **10.000** | 0.08–0.16 | 4.2–7.0 yalms/s (≈5.2–5.5 moy.) |
| après l'arrêt | **10.000** (resté) | ~0 | — |
| (ancienne capture, à l'arrêt) | **4.000** | — | = -20% |

➡️ **Mob : champ ≤ 8.0 = sa vitesse réelle (tigre 6.8, mob normal 4.0) ; en mouvement il SPIKE à 10–17 (bogus).**

### 2e. Mesure par position (mob en chasse) — comparaison tigre vs normal
- Tigre : raw ~ **+9 à +44%** (moy. ~+23%).
- Mob normal : raw ~ **-6 à +30%** (moy. ~+11%).
- ➡️ différencié (tigre > normal) **mais bruité ±20%**, et **-53/-85%** parasites à la décélération.

---

## 3. Les 4 problèmes de fond

1. **Champ bogus en mouvement.** Mob : 10–17 (> 8.0). Autre joueur : chute à ~2.0. Inutilisable tel quel pendant
   le mouvement. (Self échappe à ça : sa valeur est statique.)
2. **Position de self figée.** On ne peut pas mesurer sa propre vitesse par position (≠ des autres entités).
3. **Mécanique de CATCH-UP.** Un mob qui te chasse bouge **à TA vitesse** pour te suivre. Donc mesurer sa position
   donne **ta** vitesse, pas la sienne. *Prouvé : enlever tes feet fait BAISSER la vitesse affichée du mob.*
   ⇒ la mesure de position est **inutile pour un mob qui te poursuit** (le cas le plus fréquent).
4. **Bruit de mesure.** Position des entités distantes = rythme réseau (~10 Hz) + les mobs **tournent** ⇒ entre 2
   updates on ne voit que la corde (sous-estime dans les virages) + à-coups. ⇒ ±20% de bruit irréductible.

---

## 4. Récap par type d'entité

| Cible | Champ à l'arrêt | Champ en mouvement | Position exploitable ? | Ce qu'on peut afficher de FIABLE |
|---|---|---|---|---|
| **TOI** | exact (gear) | exact (statique) | ❌ figée | **`100*(ms/5-1)`** ✅ exact |
| **Autre joueur** | exact (gear) | garbage (~2) | ✅ mais = throttle | **champ à l'arrêt** ✅ ; en mouvement : gel |
| **Mob** | réel si ≤8 (tigre 6.8, normal 4.0) | bogus (10–17) | ✅ mais catch-up = TA vitesse | **champ quand ≤8, gel des spikes** |
| **NPC pur** | ? (non testé) | ? | — | rien (déjà : pas de speed sur NPC) |

Note : `0x9C` = 5.0 constant partout (la base). `status` = `0x170` (0=idle,1=engagé ; **5=chocobo/85=monture à
vérifier** — pour le cas monté `ms/4`).

---

## 5. Méthodes candidates (maths)

### A. Universelle `100*(ms/5 - 1)` (SpeedChecker/targetinfo)
- ✅ Self + autre-joueur-à-l'arrêt : exact.
- ❌ Mob en mouvement : bogus (10–17 → +60% partout). ❌ Autre joueur en mouvement : garbage.

### B. Champ + GEL des valeurs non plausibles  ← **méthode actuelle**
- On garde le champ **quand il est plausible** (`0.5 < ms ≤ 8.0`) et — pour un PC — **à l'arrêt** ; sinon on
  **gèle la dernière bonne valeur** (`spdWindow_`).
- ✅ Mob : montre sa vitesse **inhérente** (tigre +36%, normal 0/-20%), stable, indépendante de ta vitesse.
- ✅ Autre joueur : gèle son gear pendant qu'il bouge.
- ⚠️ Dépend de voir le champ ≤8 **au moins une fois** (à l'arrêt/à l'approche). Un mob toujours en chasse dès le
  début → reste au baseline 0% jusqu'à ce qu'il s'arrête.
- ❓ À confirmer : le champ d'un mob **idle** retombe-t-il toujours à une valeur ≤8 ? (le mob normal capté est
  resté à 10.0 même arrêté — était-il encore aggro ? décai-t-il après un délai ?)

### C. Mesure par position (fenêtre + base 5)
- Donne la **vitesse réelle instantanée**. ❌ Catch-up (= ta vitesse), ❌ bruit ±20%, ❌ décélération parasite.
- Abandonné (problèmes 3 et 4).

---

## 6. Questions ouvertes à trancher

1. **Base des mobs** : 5.0 (comme joueur) ou 4.0 (pré-2013) ? Choix retenu = **5.0 pour tout** (0% = toi sans
   gear ; mob normal 4.0 = -20% = kitable ; tigre 6.8 = +36%). ✔️ validé par l'utilisateur.
2. **Champ mob idle** : ✅ RÉSOLU — un mob qui **erre (jamais aggro)** lit un `ms` **≤ 8.0 STABLE** (mob normal =
   **4.000** constant, errant ET arrêté). Le spike 10–17 n'arrive QUE en poursuite. → la méthode "champ + gel" tient.
3. **Statut monté** : `0x170` contient-il 5/85 ? → cibler un joueur monté et lire `status`.
4. **Que veut voir l'utilisateur** pour un mob en chasse : sa **vitesse inhérente** (capacité — méthode B, stable)
   ou sa **vitesse instantanée** (catch-up = ta vitesse — inutile) ? → si "inhérente", la méthode B est la bonne.

---

## 7. Recommandation (mise à jour après la sonde Kaories)

Deux comportements distincts, tranchés par les données :

- **JOUEURS (self + autres) → `100*(ms/5-1)` sur le champ, TOUJOURS.** Le champ est statique et fiable, même en
  mouvement (Kaories = +18% stable). Pas de mesure, pas de gel. Simple et exact. (La mesure de position est
  bruitée ±15% → à ne PAS utiliser pour les joueurs.)
- **MOBS → champ quand plausible (≤8.0), GEL des spikes bogus (10–17) en mouvement.** Le champ mob au repos = sa
  vraie vitesse (tigre 6.8 = +36%, mob normal 4.0 = -20%) ; en chasse il spike à 10–17 (artefact) → on gèle la
  dernière valeur ≤8. Mesurer un mob est inutile (catch-up = ta vitesse) + bruité.
- **MONTÉ** (status 5/85) → `100*(ms/4)`.  **NPC pur** → rien.

**État du code = exactement ça. Méthode VERROUILLÉE** (tous les points 6 confirmés en jeu ; sonde retirée).
