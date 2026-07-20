# Audit complet — module Timers (2026-07-20)

Audit sur quatre axes : **sources & filtrage**, **tri & catégories**, **rendu & config**, **cycle de vie**.

## Statut de vérification — à lire en premier

Ce rapport mélange deux niveaux de preuve. La distinction est **essentielle** : la leçon la plus coûteuse de
ce projet est que raisonner depuis le code donne une réponse fausse, et que la mesure tranche du premier coup.

| Niveau | Sens |
|---|---|
| **VÉRIFIÉ** | j'ai lu le code moi-même et confirmé le mécanisme. Corrigé, ou prêt à l'être. |
| **RAPPORTÉ** | tracé par un agent d'audit avec `fichier:ligne`, **non revérifié par moi**. Crédible, pas prouvé. |
| **À MESURER** | hypothèse sur un bug observé. **Ne rien corriger avant capture.** |

Aucun correctif de ce rapport ne doit être appliqué sur la seule foi d'une ligne **RAPPORTÉE**.

---

## 1. Déjà corrigé pendant l'audit

### 1.1 `selfCasts_` — le correctif de la veille était mort après 24 incantations — **VÉRIFIÉ**

`party_state.cpp` — `selfCastHead_` est **monotone** (`++selfCastHead_`, remis à 0 seulement au changement de
perso). La sauvegarde l'écrivait brut ; la lecture exigeait `head < 24` et **rejetait tout l'anneau** au-delà.

Environ 4 rotations de chants suffisaient. Le correctif livré la veille pour « Minuet IV et V après un reload »
ne fonctionnait donc **que dans une session de test courte** — c'est-à-dire exactement la mienne. En jeu réel,
jamais. Le chemin de succès n'était pas instrumenté : le défaut était invisible.

`self_buff_spell_ranked` balaie les 24 cases et trie par `tick` — il **ne lit jamais** la tête. La borne
rejetait donc des données parfaitement utilisables.

**Corrigé** : `% 24` à l'écriture, tolérance de n'importe quelle valeur à la lecture.

### 1.2 Atlas d'icônes mort au-delà de 24,8 jours d'uptime — **VÉRIFIÉ**

`hud_timers.cpp` — `if ((int)(nowMs - buffAtlasNextMs_) >= 0)` avec `buffAtlasNextMs_ = 0` comme sentinelle
« jamais essayé ». Passé `GetTickCount() > 0x7FFFFFFF`, la différence signée devient négative : **la toute
première tentative ne part jamais**, le budget de 12 essais ne s'arme pas, et **toutes** les icônes de statut
de Timers et Debuffs sont absentes pour la session.

C'est la règle 10 qui rentre par l'arithmétique plutôt que par le budget — le correctif du budget était bon,
la comparaison le contournait.

**Corrigé** : `!buffAtlasNextMs_ ||` en tête, `| 1` pour ne jamais retomber sur la sentinelle, et **un log quand
le budget expire** (corollaire de la règle 10 : une sonde qui meurt en silence se lit comme un bug absent).

### 1.3 Curseur « Espacement lignes » — écriture du fichier de config à 60 Hz — **VÉRIFIÉ**

`tm_config.cpp` — `save_ui_config()` dans la branche vraie du curseur, donc **à chaque image du glissement**,
alors que `row_slider` persiste déjà au relâchement. Tous les autres curseurs du module font correctement
confiance à ce mécanisme. **Corrigé.**

---

## 2. Le bug Ulmia / Joachim — deux mécanismes, aucun encore prouvé

**À MESURER.** C'est le point dur du rapport. L'agent décrit deux mécanismes indépendants et cohérents avec le
symptôme, mais **la sonde actuelle est structurellement incapable de les voir** — donc rien n'est prouvé.

### 2.1 `buffCaster_` est indexé par statut, dernier écrivain gagne — **RAPPORTÉ**

`buffCaster_[1024]` : **un seul lanceur par statut**. Tu chantes Minuet V → `buffCaster_[198] = toi`. Ulmia
chante un Minuet → `buffCaster_[198] = Ulmia`. Le filtre de source voit alors « lancé par un autre » pour
**tous** les minuteurs du statut 198 — **y compris le tien** — et supprime ta ligne.

Elle ne revient qu'en rechantant. Ça bascule selon qui a chanté en dernier : « des conflits », littéralement.

Aggravant : `buffCaster_` n'est **jamais purgé** à l'expiration, et il est **persisté dans le cache** — une
attribution périmée survit à un reload.

N'affecte que les réglages de source **non par défaut** (`mine` / `mine + players`).

### 2.2 Repli AoE : deux lignes fusionnées en une — **RAPPORTÉ**

Actif **avec les réglages par défaut**. `selfCasts_` ne contient que **tes** incantations. Avec ton Minuet V et
celui d'Ulmia sur le même statut, les deux minuteurs résolvent vers **le même sort** — donc le repli AoE les
avale tous les deux dans un seul groupe. **Une ligne dessinée là où il devrait y en avoir deux**, et le décompte
affiché est celui que le paquet listait en premier — possiblement celui du trust.

C'est exactement la classe de panne que le commentaire du code documente déjà (« 4 chants en jeu, 3 lignes
dessinées ») — ce correctif supposait que tout minuteur de même statut a une entrée correspondante dans
l'anneau. **Un trust casse cette hypothèse.**

### 2.3 Pourquoi `//aio songlog` ne peut pas trancher

Deux raisons : la sonde ne se déclenche que sur **tes propres** incantations — l'événement déclencheur n'est
donc jamais capturé — et elle n'imprime ni le lanceur, ni l'anneau, ni la décision de repli. C'est-à-dire
précisément les trois quantités dont dépend le diagnostic.

**Prochaine étape : étendre la sonde, capturer avec Ulmia et Joachim en groupe, puis corriger.** Deux signatures
à chercher : `casterIsTrust=1` sur une ligne que tu as chantée (mécanisme 2.1), et deux `SONGFOLD` consécutifs
partageant le même groupe (mécanisme 2.2).

---

## 3. Tri et catégories — ta demande

### 3.1 Ce qui existe

Le tri est `(bande, temps restant)` — **dans cet ordre**. Bandes : tes buffs `0`, tes AoE `1`, alliés
`2 + position`. Les recasts sont tous en bande `0`, donc **triés purement par temps restant : les plus proches
d'être prêts en premier**. Ce point-là est déjà correct.

### 3.2 Trois défauts structurels — **RAPPORTÉ**

**La bande écrase l'urgence.** Ton Sanction à 30 min passe **au-dessus** de ta Victory March à 5 s. C'est la
décision qui produit à peu près toutes les plaintes d'ordre.

**« À moi » veut dire « sur moi », pas « lancé par moi ».** Le lanceur est déjà résolu — mais uniquement pour
filtrer, jamais pour trier. Le Protect d'un trust se classe comme ton Haste. **À une ligne près.**

**Tes chants quittent la bande « moi ».** Replié dans un groupe AoE → bande 1, sous la nourriture et le Sneak.
Structurellement faux pour un BRD.

**Les lignes alliées sont affamées par construction** : au-delà de `tmMax` (16), la coupe suit les bandes — un
Haste allié à 4 s saute pendant qu'un buff perso de 30 min survit. Et à l'intérieur de la bande alliée, l'ordre
est la position dans le groupe, sans aucun rapport avec l'urgence.

### 3.3 Les catégories : rien au rendu, mais tout est déjà là

Aucune catégorisation à l'affichage — une liste plate par colonne. **Mais la taxonomie complète existe déjà,
inutilisée** : `job_track_gen.h` porte 34 catégories (`TC_SONG`, `TC_ROLL`, `TC_GEO`, `TC_JA`, `TC_RUNE`…),
libellés FR/EN, indexées sur `status` **et** `recast` — les deux identifiants que les lignes possèdent déjà.

Le module ne l'inclut même pas. Manquent : un index inverse `status → catégorie`, et trois octets sur la ligne
pour porter catégorie/type/lanceur.

**Ton « par catégorie Song, Rolls » est donc atteignable sans nouveau reverse.** C'est du câblage.

---

## 4. Autres trouvailles notables — **RAPPORTÉ**

**Bouton « Réinitialiser » = colonne Durée vidée jusqu'au prochain changement de zone.** `timers_reset()` vide
`buffTimers_`, qui n'est rempli **que** par le rafraîchissement complet 0x063 — envoyé à la connexion et au
zonage, nulle part ailleurs. Pire : dans les 4 s, le cache écrase la bonne liste par une liste vide, donc même
un reload ne récupère rien. Règle 10, cas d'école, déclenchable par un bouton de config.

**La barrière de fraîcheur de 120 s jette des expirations absolues encore valides.** Un `//unload`, 3 minutes de
pause, `//load` → toute la colonne Durée vide, alors que les expirations sont des ticks absolus toujours bons.

**Pas de flush du cache au déchargement.** Fenêtre de perte de 4 s sur la boucle standard unload/load.

**Le nettoyage des buffs alliés ne tourne que si la boîte est affichée ET `tmMine` actif** — un mutateur du
modèle piloté par le code de dessin. Le tableau de 32 entrées se bouche, et les nouveaux buffs alliés sont
silencieusement perdus.

**Les buffs lancés sur des membres d'alliance sont structurellement indessinables** — enregistrés, jamais
affichés, jamais purgés, et ils évincent des entrées de groupe qui, elles, seraient affichables.

**Trois parcours de cibles 0x028 utilisent encore le pas fixe** `150 + i*123`, déjà déclaré cassé et réécrit
dans les deux blocs voisins.

**`on_set_update` n'a aucune garde de longueur de paquet** — le seul gestionnaire du module dans ce cas.

**Aucune troncature ni points de suspension dans Timers** — le commit « three dots everywhere » ne l'a pas
couvert. Une ligne alliée longue élargit la boîte hors écran.

**En mode icône, si l'icône manque, le nom est dessiné dans une colonne mesurée pour une icône** — le texte
déborde sur le décompte et hors du cadre. C'est le visage visible de toute panne d'atlas.

**L'atlas de l'aperçu Config est resté en abandon définitif** — le motif exact que la fonction voisine a été
réécrite pour supprimer, laissé intact deux fonctions plus bas.

**Le clignotement SP s'arrête pendant le maintien à 0:00** — l'alerte la plus forte du module se tait dans les
2 dernières secondes, précisément la fenêtre pour laquelle elle existe.

**`tmRecMode` est un réglage mort** : sauvegardé, relu, comparé — aucun contrôle ne l'écrit, la colonne Recast
est codée en dur. Piège pour la prochaine personne.

---

## 5. Ce qui est vérifié correct

L'arithmétique d'expiration (diff signée, sans bug de repli), le maintien à 0:00 avec `buffsOk`, les chemins de
cache **par personnage** (aucune fuite entre persos, fichiers disjoints entre clients), `on_character_changed`
(point de remise à zéro unique et bien documenté), la symétrie sauvegarde/lecture du cache, l'absence
d'allocation par image, l'absence de collision de ressort sur les `CTRL_ID`, et la gestion de la perte de
périphérique (oubli des poignées, pas de `Release`).

---

## 6. Ordre de traitement proposé

1. **Étendre `//aio songlog`, capturer avec les trusts, corriger le bug des chants.** C'est le symptôme réel.
   Mesurer avant de toucher à la logique.
2. **`timers_reset()`** — un bouton qui casse durablement la colonne.
3. **Barrière de fraîcheur + flush au déchargement** — la colonne Durée vide après un reload.
4. **Sortir le nettoyage du chemin de dessin.**
5. **Le tri** : lanceur en critère, chants dans la bande « moi », urgence avant position d'alliance.
6. **Les catégories** — câblage de `job_track_gen.h`, une fois le tri stabilisé.
