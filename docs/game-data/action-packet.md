# 0x028 — action packet (bit layout)

**Source : le parseur du client lui-même**, décompilé sous Ghidra le 2026-07-20. Ce n'est pas une reconstitution
depuis notre code ni depuis la documentation communautaire — c'est la vérité terrain, et elle a corrigé un de nos
champs au passage.

Adresses à la base d'image `0x05C60000` (`re/ffximain_dump.bin`) :

| Rôle | Adresse |
|---|---|
| Répartiteur | `0x05D5A6C5` — deux tables ; **0x028 est dans la table secondaire** |
| Gestionnaire 0x028 | `0x05CFE330` — appelle le parseur avec `packet+4` |
| **Parseur (référence)** | **`0x05DEC950`** |
| Lecteur de bits (LSB d'abord) | `0x05DEC480` |
| Copieur de résultat | `0x05DED0E0` |

> Correction d'une doc existante : `re-static-analysis.md` présente `0x44730` comme un RVA brut. C'est **faux** —
> c'est un déplacement relatif à un pointeur global.

## Pourquoi l'ancrage à 150 est exact

Le gestionnaire passe `packet+4`, le curseur est posé là, puis le lecteur **incrémente le pointeur d'octet avant
la première lecture**. La première lecture démarre donc à l'octet 5 = **bit absolu 40**.

En-tête = 40 + 32 + 6 + 4 + 4 + 32 + 32 = **150**. Mécaniquement prouvé, indépendant du contenu.

## En-tête (bits absolus, paquet en-tête compris)

| Bits | Largeur | Champ |
|---|---|---|
| 40 | 32 | id du lanceur |
| 72 | **6** | **nombre de cibles** |
| 78 | 4 | inconnu (champ distinct) |
| 82 | 4 | catégorie |
| 86 | 32 | param — les 16 bits bas = id du sort / de la capacité |
| 118 | 32 | recast / inconnu |
| **150** | | début de `target[0]` |

**Réserve honnête** : le parseur range les deux champs de 4 bits séparément sans les interpréter, donc *lequel* de
78/82 est la « catégorie » n'est pas prouvé par ces fonctions. Notre lecture au bit 82 est validée par le
comportement en jeu, pas par la décompilation.

## Par cible, par effet

Cible : `id` 32 bits, puis **nombre d'effets sur 4 bits**. Pas de structure 0x16C (364 o), confirmé indépendamment.

Effet (décalages relatifs au début de l'effet ; structure 0x2C = 44 o) :

| Décalage | Largeur | Champ |
|---|---|---|
| 0 | 3 | réaction |
| 3 | 2 | réaction (haut) |
| 5 | 12 | animation |
| 17 | 5 | effet |
| 22 | 5 | stagger |
| 27 | **17** | **param** |
| 44 | **10** | **message** |
| 54 | 31 | inconnu |
| 85 | 1 | **drapeau add-effect** |

**Bloc add-effect** (porte = bit 85) : 6 + 4 + 17 + 10 = **+37**. Nos `+37` et nos sous-décalages sont **corrects**.

**Bloc spike** (porte = bit suivant, +86) : 6 + 4 + **14** + 10 = 34 de corps → **+35 avec la porte, +1 sans**. Nos
`+35`/`+1` sont **corrects**. Noter que le troisième champ fait **14 bits, pas 17** — il diffère du bloc
add-effect. Sans effet aujourd'hui puisqu'on ne lit aucun champ spike, mais à savoir avant d'en lire un.

Effet minimal = 85 + 1 + 1 = **87 bits**. Cible minimale = 32 + 4 + 87 = **123 bits**. Notre 123 est **correct**.

Bornes dures du client : **≤ 64 cibles, ≤ 8 effets par cible**.

## Verdict sur le pas fixe `150 + i*123`

**Correct pour `i == 0`, sans condition.** L'en-tête de 150 est exact quel que soit le contenu, donc les blocs qui
ne lisent que `target[0]` (skillchain, popup de WS) sont sains.

**Pour `i >= 1`, correct uniquement si chaque cible `j < i` avait exactement un effet, sans add-effect ni spike.**
Un critique, un proc ou un spike sur une cible antérieure désynchronise tout ce qui suit.

Autrement dit : **le pas fixe échoue d'autant plus souvent que le groupe est actif**. Et il échoue précisément sur
le cas « le joueur était-il parmi les cibles de l'AoE ». Les sites concernés dans `party_state.cpp` — le parcours
de la liste de haine et les balayages `aoeSelf` / lignes de buff alliées — restent à convertir au pas variable.

### Bug corrigé grâce à cette passe

Un site lisait le nombre de cibles sur **10 bits** au lieu de 6, absorbant le champ inconnu des bits 78-81. Quand
ce champ était non nul, le compte gonflait par multiples de 64 — et le plafond à 16 masquait silencieusement le
problème. Les six autres sites du fichier utilisaient déjà 6. **Corrigé.**

## Le client ne stocke aucun lanceur par statut

Vérifié sur tous les sites de stockage :

- **`0x063` sous-type 9** (statuts self) → `DAT_060E5904`, exactement 192 octets = 32 × (timer 4 o + id 2 o).
  **Aucune place pour un lanceur.**
- **`0x076`** (buffs des membres) → `DAT_060E09A0`, 240 octets = 5 membres × 48. Aucun lanceur.
- **L'enregistrement de résultat d'action** fait 52 octets : id de cible + pointeur d'entité + l'effet de 44 o.
  **Le lanceur est stocké une fois par paquet, jamais par effet** — l'attribution est structurellement jetée dès
  que le paquet est consommé.

**Conséquence pour le bug des chants de trust.** Il n'existe aucun champ mémoire à lire : le paquet 0x028 est la
seule source possible, donc notre architecture est la bonne. Mais le client ne peut pas nous aider à départager :
la chanson d'un trust et la nôtre portent **le même id de statut**, donc une table indexée par statut seul ne peut
contenir qu'un lanceur, et le dernier écrivain gagne **par construction**.

Le correctif doit porter sur **la clé** — statut + quelque chose qui sépare les instances concurrentes, ou refus
d'écraser une attribution self par une attribution non-self — **pas** sur la recherche d'un champ lanceur qui
n'existe pas.
