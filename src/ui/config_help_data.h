// config_help_data.h -- the Help tab CONTENT, split out of config_page.cpp (PURE MOVE).
//
// 673 lines of pure `const` prose were the single biggest reason config_page.cpp regrew past its
// pre-split size. It is data, not logic : nothing here draws. Included ONCE, by config_page.cpp.
// NOTE: included from INSIDE `namespace aio` in config_page.cpp, so this file must NOT open the
// namespace itself (that would nest it as aio::aio::).
#pragma once

// ---- Help content. kind 0 = heading, 1 = paragraph, 2 = bullet. Each item carries EN + FR text. ----
struct HelpItem { int kind; const char* en; const char* fr; };

// GENERAL : everything global -- the window, language, profiles, the command list. (Module-specific
// help lives under its own page below, e.g. Party / Alliance.)
static const HelpItem HELP_GENERAL[] = {
    {0, "AioHUD", "AioHUD"},
    {1, "AioHUD is a from-scratch interface for FFXI, drawn live over the game. It replaces and adds to the native windows with a full set of modules : *Party & Alliance*, *Target*, *Player Hub*, *Minimap*, *Hate List*, *PointWatch*, *Timers*, *Skillchains*, *Treasure Pool* and *Zone Tracker*. Everything is set up from this window, and each module has its own page in this Help.",
        "AioHUD est une interface repensée de zéro pour FFXI, dessinée en direct par-dessus le jeu. Elle remplace et complète les fenêtres natives avec un ensemble complet de modules : *Party & Alliance*, *Cible*, *Hub Joueur*, *Minicarte*, *Liste d'aggro*, *PointWatch*, *Timers*, *Skillchains*, *Pool de trésor* et *Suivi de zone*. Tout se règle depuis cette fenêtre, et chaque module a sa propre page dans cette Aide."},

    {0, "The config window", "La fenêtre de configuration"},
    {1, "Type //aio config to open this window. Its tabs:",
        "Tape //aio config pour ouvrir cette fenêtre. Ses onglets :"},
    {2, "*Configuration* tunes how things look, with a live preview on the right.",
        "*Configuration* règle l'apparence, avec un aperçu en direct à droite."},
    {2, "*Profile* saves, loads and manages complete setups.",
        "*Profil* enregistre, charge et gère des configurations complètes."},
    {2, "*Help* is this reference. Pick a module in the left column to read about it.",
        "*Aide* est cette référence. Choisis un module dans la colonne de gauche pour le découvrir."},
    {2, "*Update* checks for and installs new versions, right in the game (see the Update page here).",
        "*Mise à jour* vérifie et installe les nouvelles versions, directement en jeu (voir la page Mise à jour ici)."},

    {0, "Language", "Langue"},
    {1, "The EN / FR button in the top-right corner switches the whole interface between English and French. It is on every tab and your choice is saved.",
        "Le bouton EN / FR en haut à droite bascule toute l'interface entre l'anglais et le français. Il est présent sur chaque onglet et ton choix est sauvegardé."},

    {0, "Profiles", "Profils"},
    {1, "A profile stores every setting under a name. Load one to switch your whole setup at once. Edit a loaded profile's name and press Save to rename it in place. They are handy for a per-character or per-job look.",
        "Un profil enregistre tous les réglages sous un nom. Charges-en un pour changer toute ta configuration d'un coup. Modifie le nom d'un profil chargé puis appuie sur Enregistrer pour le renommer. Pratique pour un rendu par personnage ou par job."},
    {1, "The last profile you load is remembered, so when the plugin restarts it automatically comes back on that same profile.",
        "Le dernier profil chargé est mémorisé : au redémarrage du plugin, il revient automatiquement sur ce même profil."},

    {0, "Commands", "Commandes"},
    {2, "*//aio config* opens this window.", "*//aio config* ouvre cette fenêtre."},
    {2, "*//aio edit* moves and resizes the boxes on screen.", "*//aio edit* déplace et redimensionne les cadres."},
    {2, "*//aio profile* save, load, delete or list a named setup.", "*//aio profile* save, load, delete ou list un profil nommé."},
    {2, "*//aio party N* previews N members, 0 to 18 (you are always #1).", "*//aio party N* prévisualise N membres, de 0 à 18 (tu es toujours le #1)."},
    {2, "7-18 auto-fills the alliances ; *//aio party off* returns to live.", "7-18 remplit les alliances automatiquement ; *//aio party off* revient au réel."},
};
static const int HELP_GENERAL_N = (int)(sizeof(HELP_GENERAL) / sizeof(HELP_GENERAL[0]));

// PARTY / ALLIANCE : only what concerns the party + alliance boxes.
static const HelpItem HELP_PA[] = {
    {0, "Party and Alliance", "Party et Alliance"},
    {1, "AioHUD replaces the game's *Party* and *Alliance* windows with up to three boxes, your *Party* plus *Alliance 1* and *Alliance 2*. Each one shows its members with live HP, MP and TP, their job, the leader marks, the distance, and for your own *Party* their status effects. It all moves in real time, and whoever you are targeting stays lit up.",
        "AioHUD remplace les fenêtres *Party* et *Alliance* du jeu par un maximum de trois cadres, ta *Party* plus l'*Alliance 1* et l'*Alliance 2*. Chacun montre ses membres avec leurs HP, MP et TP en direct, leur job, les marques de leader, la distance, et pour ta propre *Party* leurs effets de statut. Tout bouge en temps réel, et la personne que tu cibles reste allumée."},

    {0, "Reading a member", "Lire un membre"},
    {1, "A member's row reads from left to right, starting with the leader dots and the distance, then the job badge, the name, and finally the three gauges.",
        "La ligne d'un membre se lit de gauche à droite, en commençant par les pastilles de leader et la distance, puis le badge de job, le nom, et enfin les trois jauges."},
    {2, "The *name* sits in a clean off-white. It turns *red* when the member is KO, and greys out when they are in another zone.",
        "Le *nom* est en blanc cassé. Il passe au *rouge* quand le membre est KO, et grise quand il est dans une autre zone."},
    {2, "The *job badge* carries the main job on top and the sub below, WAR over NIN for example. It is tinted by the member's *role*, tank, healer, damage or support, so you read the make-up of the group at a glance.",
        "Le *badge de job* porte le job principal en haut et le sous-job en dessous, WAR sur NIN par exemple. Il est teinté selon le *rôle* du membre, tank, healer, damage ou support, pour lire la composition du groupe d'un coup d'oeil."},
    {2, "*HP* runs from green when it is high down through yellow and orange to red, and it blinks alarm-red once the member drops to a quarter or less.",
        "Les *HP* vont du vert quand elles sont hautes jusqu'au rouge en passant par le jaune et l'orange, et clignotent en rouge d'alarme dès que le membre tombe à un quart ou moins."},
    {12, "the HP colour, live, blinking at the critical quarter", "la couleur des HP en direct, clignotant au quart critique"},
    {2, "*MP* is blue. *TP* fills as it builds and lights up at 1000, which means a weapon skill is ready.",
        "Les *MP* sont bleus. Les *TP* se remplissent et s'illuminent à 1000, ce qui veut dire qu'une weapon skill est prête."},
    {13, "the TP gauge, live, glowing past 1000", "la jauge de TP en direct, brillant au-delà de 1000"},
    {2, "*Buff icons* for your own *Party*, on the left (up to 32, wrapping to two rows of 16). The game never sends *Alliance* buffs, so alliance members show none.",
        "Des *icônes de buffs* pour ta propre *Party*, à gauche (jusqu'à 32, sur deux lignes de 16). Le jeu n'envoie jamais les buffs d'*Alliance*, donc les membres d'alliance n'en ont pas."},

    {0, "Leader and Quartermaster dots", "Pastilles Party Leader, Alliance Leader et Quartermaster"},
    {1, "Three little dots sit at the top of the left column, each in its own fixed slot so they never shuffle around. A slot stays empty when the member does not hold that role, and the dots fade in and out as roles change.",
        "Trois petites pastilles en haut de la colonne de gauche, chacune dans un emplacement fixe pour ne jamais se mélanger. Un emplacement reste vide si le membre n'a pas ce rôle, et les pastilles apparaissent et disparaissent quand les rôles changent."},
    {11, "", ""},

    {0, "Distance", "Distance"},
    {1, "Under the dots, each member shows how far they are from you in yalms, written as 00.00. The colour tells you at a glance whether your spells can reach them.",
        "Sous les pastilles, chaque membre montre à quelle distance il est de toi en yalms, au format 00.00. La couleur te dit d'un coup d'oeil si tes sorts peuvent l'atteindre."},
    {10, "under 10 blue, up to 20.8 yellow, beyond that red", "moins de 10 bleu, jusqu'à 20.8 jaune, au-delà rouge"},
    {2, "*Blue* under 10 yalms, in range of everything, cures and the short spells like the AoE -ra line or Majesty.",
        "*Bleu* sous 10 yalms, à portée de tout, cures et sorts courts comme les -ra de zone ou Majesty."},
    {2, "*Yellow* from 10 up to 20.8, still in normal casting range but too far for those short spells.",
        "*Jaune* de 10 à 20.8, encore dans la portée de cast normale mais trop loin pour ces sorts courts."},
    {2, "*Red* at 20.8 and beyond, out of range. A normal cure or buff will fail, and the whole row dims.",
        "*Rouge* à 20.8 et au-delà, hors de portée. Une cure ou un buff normal échouera, et toute la ligne s'assombrit."},
    {1, "You, your trusts, and anyone in another zone show no distance.",
        "Toi, tes trusts et quiconque dans une autre zone n'affichent pas de distance."},

    {0, "Target cursor", "Curseur de cible"},
    {1, "When you target a *Party* or *Alliance* member, a hand appears at the left of their row pointing at them and a soft highlight slides onto it. The cursor follows your target from one member to the next, bobbing gently so it is always easy to spot.",
        "Quand tu cibles un membre de la *Party* ou de l'*Alliance*, une main apparaît à gauche de sa ligne en le pointant et une surbrillance douce glisse dessus. Le curseur suit ta cible de membre en membre, en oscillant légèrement pour rester bien visible."},
    {1, "When you lock onto your target, the hand and its highlight both turn *red* instead of gold, so a glance tells you the lock is on. Take the lock off and they go back to gold.",
        "Quand tu te verrouilles sur ta cible, la main et sa surbrillance passent au *rouge* au lieu de l'or, pour voir d'un coup d'oeil que le verrou est actif. Retire le verrou et elles redeviennent or."},
    {14, "", ""},
    {1, "The game's own party menu, like Quartermaster or Lottery, also lights up the member it points at, so the box always matches the menu on screen.",
        "Le menu de groupe du jeu, comme Quartermaster ou Lottery, allume aussi le membre qu'il pointe, donc le cadre correspond toujours au menu à l'écran."},

    {0, "Selection highlight", "Surbrillance de sélection"},
    {1, "The highlight is a faint tinted bar with a slow glass sheen sweeping across it, bright enough to frame the member but never enough to hide the gauges. The main target's bar is *gold*, and a sub-target's bar is ocean *blue* and sits on top.",
        "La surbrillance est une barre teintée discrète avec un reflet de verre qui la balaie lentement, assez visible pour encadrer le membre mais jamais au point de masquer les jauges. La barre de la cible principale est *or*, et celle d'une sous-cible est *bleu* océan et passe au-dessus."},

    {0, "Casting and out of range", "Incantation et hors de portée"},
    {1, "While a member casts, the spell name shows in *gold* just under their name, breathing softly. A member beyond casting range sits under a dark veil over their row, a clear can't-reach-them cue. A member who has moved to another zone shows that zone's name in place of the gauges.",
        "Quand un membre lance un sort, le nom du sort s'affiche en *or* juste sous son nom, en pulsant doucement. Un membre hors de portée de cast est recouvert d'un voile sombre sur sa ligne, un signal clair qu'on ne peut pas l'atteindre. Un membre parti dans une autre zone montre le nom de cette zone à la place des jauges."},

    {0, "Adaptive layout", "Disposition adaptative"},
    {1, "The *Party* box is anchored by its lower-right corner. It grows upward as members join and shrinks back as they leave, but the corner you placed never moves. Whatever the member count, it reaches up just far enough to keep the game's own party window hidden behind it, and that top edge is set by the *Party* zones covered further down.",
        "Le cadre *Party* est ancré par son coin bas-droit. Il grandit vers le haut quand des membres arrivent et se réduit quand ils partent, mais le coin que tu as placé ne bouge jamais. Quel que soit le nombre de membres, il remonte juste assez pour garder la fenêtre de groupe native du jeu cachée derrière lui, et ce bord haut est défini par les zones *Party* vues plus bas."},

    {0, "Cost and Next box", "Cadre Coût et Next"},
    {1, "When you open a spell, ability or weapon-skill menu, a small box appears just above the *Party*. It shows the action's name with, depending on the action, its MP cost for spells, its *Next* recast timer for spells and abilities, or your live TP for weapon skills. *Alliance 1* stacks flush on top of this box.",
        "Quand tu ouvres un menu de sort, d'aptitude ou de weapon skill, un petit cadre apparaît juste au-dessus de la *Party*. Il montre le nom de l'action avec, selon le cas, son coût en MP pour les sorts, son délai *Next* pour les sorts et aptitudes, ou tes TP en direct pour les weapon skills. L'*Alliance 1* se cale juste au-dessus de ce cadre."},

    {0, "Configuration", "Configuration"},
    {1, "The Configuration tab sets how the boxes look, with a live preview on the right that follows your changes.",
        "L'onglet Configuration règle l'apparence des cadres, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Box Theme* is the FFXI window skin used for every box.",
        "*Box Theme* est l'habillage de fenêtre FFXI utilisé pour tous les cadres."},
    {2, "*Font* is the text face for names, jobs and numbers.",
        "*Font* est la police des noms, jobs et chiffres."},
    {2, "*Party Size* scales the *Party* box from 100 to 200 percent. It never drops below 100 so it always covers the native window. *Ally 1* and *Ally 2* scale on their own, 50 to 200.",
        "*Party Size* met le cadre *Party* à l'échelle de 100 à 200 pour cent. Il ne descend jamais sous 100 pour toujours couvrir la fenêtre native. *Ally 1* et *Ally 2* s'échelonnent seuls, de 50 à 200."},
    {2, "*Buff Size* sets how big the buff icons are, as a share of the row height.",
        "*Buff Size* règle la taille des icônes de buffs, en proportion de la hauteur de ligne."},
    {2, "*Bar Height* and *Bar Width* size the HP, MP and TP gauges. The box grows to fit.",
        "*Bar Height* et *Bar Width* dimensionnent les jauges HP, MP et TP. Le cadre s'agrandit en conséquence."},
    {2, "*Job Badge* is Off, Main job only, Main plus Sub, or Icons.",
        "*Job Badge* est Aucun, Job principal seul, Principal plus Sub, ou Icônes."},
    {2, "*Casts* shows the casting line, toggled apart for *Party* and *Alliance*. *Distance* shows the yalms number, toggled per box.",
        "*Casts* affiche la ligne d'incantation, activable à part pour la *Party* et l'*Alliance*. *Distance* affiche le nombre de yalms, activable par cadre."},
    {2, "*Borders* turns the window frame off per box, *Party*, *Cost*, *Alliance 1* and *Alliance 2*. The background stays, only the frame goes.",
        "*Borders* enlève le cadre de fenêtre par boîte, *Party*, *Cost*, *Alliance 1* et *Alliance 2*. Le fond reste, seule la bordure part."},
    {1, "Whenever a box is resized its lower-right corner stays put, so it grows up and to the left from where you placed it and nothing drifts.",
        "Quand un cadre est redimensionné son coin bas-droit reste en place, il grandit vers le haut et la gauche depuis où tu l'as posé et rien ne dérive."},

    {0, "Move and resize", "Déplacer et redimensionner"},
    {1, "Type //aio edit, or click Edit Layout, to arrange the boxes right on the game screen. Drag a box to move it, and roll the wheel over it to resize, the *Party* from 100 to 200 and the alliances from 50 to 200. Boxes snap to each other's edges as you drag. Default resets every position and size, and Done saves and leaves edit mode.",
        "Tape //aio edit, ou clique Éditer dispo, pour disposer les cadres directement sur l'écran. Glisse un cadre pour le déplacer, et utilise la molette dessus pour redimensionner, la *Party* de 100 à 200 et les alliances de 50 à 200. Les cadres s'aimantent à leurs bords pendant le glissement. Défaut réinitialise toutes les positions et tailles, et Terminé sauvegarde et quitte."},

    {0, "Zones", "Zones"},
    {1, "In edit mode the *Rules* button opens the zone editor. The HUD hides so only your zones and the toolbar show. Zones are rectangles you draw over the screen to say where each box may sit, to line the boxes up on the game's native windows and keep them off spots like the chat log.",
        "En mode édition le bouton *Rules* ouvre l'éditeur de zones. Le HUD se cache pour ne montrer que tes zones et la barre d'outils. Les zones sont des rectangles que tu dessines sur l'écran pour dire où chaque cadre peut se poser, pour aligner les cadres sur les fenêtres natives du jeu et les tenir à l'écart d'endroits comme le journal de chat."},
    {2, "*Draw* a zone by dragging on an empty spot, or click + Zone. Move it by its body, resize it by its corners. A plain click only selects it.",
        "*Dessine* une zone en glissant sur un endroit vide, ou clique + Zone. Déplace-la par son corps, redimensionne-la par ses coins. Un simple clic la sélectionne seulement."},
    {2, "*Name and permissions*, pick a zone then rename it in the panel and choose which boxes may sit on it, *Party*, *Alliance* or *Hub*. A zone that allows nothing is a keep-out, a box dragged onto it is pushed back out.",
        "*Nom et permissions*, choisis une zone puis renomme-la dans le panneau et choisis quels cadres peuvent s'y poser, *Party*, *Alliance* ou *Hub*. Une zone qui n'autorise rien est interdite, un cadre glissé dessus en est repoussé."},
    {2, "*+ Party* creates the six party zones, 1p to 6p. The *Party* box grows up to the zone that matches your member count. *+ Ally* creates the two alliance zones. Drag and resize them onto the native windows.",
        "*+ Party* crée les six zones party, 1p à 6p. Le cadre *Party* remonte jusqu'à la zone qui correspond à ton nombre de membres. *+ Ally* crée les deux zones d'alliance. Glisse-les et redimensionne-les sur les fenêtres natives."},
    {1, "Turn *Rules* off to go back to placing the boxes. Your zones stay active and keep the boxes out of the forbidden spots. The panel is draggable and its place is remembered. Everything is stored as a share of the screen, so a saved layout scales to any resolution, you may just re-align the zones to your own native windows.",
        "Désactive *Rules* pour revenir au placement des cadres. Tes zones restent actives et tiennent les cadres hors des endroits interdits. Le panneau est déplaçable et sa place est mémorisée. Tout est stocké en proportion de l'écran, donc une disposition sauvée s'adapte à toute résolution, il te suffira peut-être de ré-aligner les zones sur tes propres fenêtres natives."},

    {0, "Preview and demo", "Aperçu et démo"},
    {1, "Use //aio party N to fill the boxes with N fake members, 0 to 18 - you are always member #1. 1 to 6 is a single *Party*; 7 to 18 auto-creates the alliances needed (7-12 adds alliance 1, 13-18 adds alliance 2), so no separate command. In demo a target cursor cycles through the members so you watch the highlight move. //aio party off returns to live data. Demo mirrors the real layout, so you can tune spacing and placement with no live party.",
        "Utilise //aio party N pour remplir les cadres avec N faux membres, de 0 à 18 - tu es toujours le membre #1. De 1 à 6 c'est une seule *Party*; de 7 à 18 les alliances nécessaires sont créées automatiquement (7-12 ajoute l'alliance 1, 13-18 l'alliance 2), sans commande séparée. En démo un curseur de cible défile sur les membres pour voir bouger la surbrillance. //aio party off revient aux données réelles. La démo reproduit la vraie disposition, pour régler l'espacement et le placement sans groupe réel."},
};
static const int HELP_PA_N = (int)(sizeof(HELP_PA) / sizeof(HELP_PA[0]));

// TARGET : everything the Target box shows (name, HP + damage trail, distance/range, debuffs, speed, TH)
// and how to configure it. Text-only (kinds 0/1/2), same style as HELP_PA.
static const HelpItem HELP_TARGET[] = {
    {0, "Target", "Cible"},
    {1, "The *Target* box is a compact panel that appears whenever you have something targeted, a player, an ally, a monster or an NPC. It shows the target's name, its HP as a bar and a percentage, how far it is on a colour-coded range bar, its status effects with timers, and for a living foe its movement speed and Treasure Hunter. It fades in when you target and slips away when you drop the target.",
        "Le cadre *Cible* est un panneau compact qui apparaît dès que tu as quelque chose de ciblé, un joueur, un allié, un monstre ou un PNJ. Il montre le nom de la cible, ses HP en barre et en pourcentage, sa distance sur une barre de portée colorée, ses effets de statut avec leurs minuteurs, et pour un ennemi vivant sa vitesse de déplacement et son Treasure Hunter. Il apparaît en fondu quand tu cibles et s'efface quand tu relâches la cible."},

    {0, "Reading the target", "Lire la cible"},
    {2, "The *name* sits at the top and stays readable over any box theme.",
        "Le *nom* est en haut et reste lisible sur n'importe quel thème de cadre."},
    {2, "The *HP %* number tells you exactly how healthy the target is, even when the bar is almost full or almost empty.",
        "Le *pourcentage de HP* te dit exactement l'état de la cible, même quand la barre est presque pleine ou presque vide."},
    {2, "The *HP bar* fills with a glass sheen and shifts from green through yellow and orange to red as the target loses health, the same liquid look as the party gauges.",
        "La *barre de HP* se remplit avec un reflet de verre et passe du vert au rouge en passant par le jaune et l'orange à mesure que la cible perd ses HP, le même rendu liquide que les jauges de groupe."},

    {0, "The damage trail", "La traînée de dégâts"},
    {1, "When the target takes a hit, the coloured bar snaps down at once while a brighter *orange* slice holds for a moment behind it, then drains to catch up. That lingering orange is the health you just took off, so a big hit reads as a satisfying chunk, the way a fighting-game health bar does.",
        "Quand la cible encaisse un coup, la barre colorée chute d'un coup tandis qu'une tranche plus vive, *orange*, reste un instant derrière, puis se vide pour rattraper. Cet orange qui s'attarde, c'est les HP que tu viens d'enlever, si bien qu'un gros coup se lit comme une belle entaille, à la manière d'une barre de vie de jeu de combat."},
    {20, "a hit drops the bar, the orange damage lingers then drains", "un coup fait chuter la barre, les dégâts orange s'attardent puis se vident"},

    {0, "Distance and range", "Distance et portée"},
    {1, "Below the HP, a range bar shows how far the target is and which of your actions can reach it. A cursor marks the target's exact distance along the bar, and the number in the centre reads it out in yalms. It works for allies and NPCs just like for monsters.",
        "Sous les HP, une barre de portée montre à quelle distance est la cible et quelles actions peuvent l'atteindre. Un curseur marque la distance exacte de la cible le long de la barre, et le nombre au centre l'indique en yalms. Elle fonctionne pour les alliés et les PNJ comme pour les monstres."},
    {2, "Against a *monster*, the zones are your combat ranges, *Melee*, *WS*, *Magic*, *Ranged* and *Enmity*, scaled to the creature's size, so you see at a glance whether you are close enough to swing, weapon-skill or cast.",
        "Contre un *monstre*, les zones sont tes portées de combat, *Melee*, *WS*, *Magic*, *Ranged* et *Enmity*, mises à l'échelle de la taille de la créature, pour voir d'un coup d'oeil si tu es assez près pour frapper, faire une weapon skill ou lancer un sort."},
    {21, "vs a monster : Melee, WS, Magic, Ranged, Enmity", "contre un monstre : Melee, WS, Magic, Ranged, Enmity"},
    {2, "On a *player* or an *NPC*, the zones are the support ranges, *Trade*, *AoE* and *Cast*, so you know when you can trade, land an area buff or cure them.",
        "Sur un *joueur* ou un *PNJ*, les zones sont les portées de soutien, *Trade*, *AoE* et *Cast*, pour savoir quand tu peux échanger, poser un buff de zone ou le soigner."},
    {22, "vs a player or NPC : Trade, AoE, Cast", "contre un joueur ou un PNJ : Trade, AoE, Cast"},
    {2, "Past the last zone the cursor moves into *Out*, the target is beyond reach.",
        "Au-delà de la dernière zone le curseur passe dans *Out*, la cible est hors de portée."},

    {0, "Movement speed", "Vitesse de déplacement"},
    {1, "For a player or a monster the box shows how fast the target moves compared to normal, written +18% or -12%. The colour reads it for you, and it means the opposite thing for a friend and a foe.",
        "Pour un joueur ou un monstre le cadre montre à quelle vitesse la cible se déplace par rapport à la normale, écrit +18% ou -12%. La couleur l'interprète pour toi, et elle veut dire l'inverse pour un allié et un ennemi."},
    {2, "On a *player*, *green* is faster than normal (feet, haste, good) and *red* is slower (bind, weight, bad).",
        "Sur un *joueur*, le *vert* est plus rapide que la normale (feet, haste, c'est bon) et le *rouge* plus lent (bind, poids, c'est mauvais)."},
    {2, "On a *monster* it flips, *red* is faster (a hasted or fleeing mob, watch out) and *green* is slower (it is snared).",
        "Sur un *monstre* ça s'inverse, le *rouge* est plus rapide (un mob hâté ou qui fuit, attention) et le *vert* plus lent (il est ralenti)."},
    {23, "same +%, green on a player, red on a monster", "le même +%, vert sur un joueur, rouge sur un monstre"},
    {1, "A plain NPC that is neither a player nor a monster shows no speed.",
        "Un PNJ neutre qui n'est ni joueur ni monstre n'affiche pas de vitesse."},

    {0, "Treasure Hunter", "Treasure Hunter"},
    {1, "Once you have hit a monster, a small treasure-chest mark carries a number, its current *Treasure Hunter* tier, so you know how far your TH has climbed before the kill. It only appears on a foe you are fighting.",
        "Une fois que tu as frappé un monstre, une petite marque de coffre porte un chiffre, son niveau de *Treasure Hunter* actuel, pour savoir jusqu'où ton TH a grimpé avant le kill. Elle n'apparaît que sur un ennemi que tu combats."},
    {24, "the coffer, cycling its Treasure Hunter tier", "le coffre, avec son niveau de Treasure Hunter"},

    {0, "Status effects", "Effets de statut"},
    {1, "The target's debuffs show as icons, each with its own countdown timer, so you track your Slow, Paralyze, Dia or Poison at a glance. You choose how many appear and where they sit around the box.",
        "Les debuffs de la cible s'affichent en icônes, chacune avec son propre minuteur décompté, pour suivre tes Slow, Paralyze, Dia ou Poison d'un coup d'oeil. Tu choisis combien apparaissent et où ils se placent autour du cadre."},
    {25, "live debuff icons with their timers", "les icônes de debuff en direct avec leurs minuteurs"},

    {0, "Configuration", "Configuration"},
    {1, "The Configuration tab holds the Target settings in five sections, with a live preview on the right that follows your changes.",
        "L'onglet Configuration regroupe les réglages Cible en cinq sections, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Box / Frame* turns the window frame on or off and sets its theme. Keep it *Same as Party*, or give the target its own *Style*, *Theme*, *Luminosity* and *Transparency*. *Target Size* scales the whole box.",
        "*Box / Cadre* active ou non le cadre de fenêtre et règle son thème. Garde-le *Comme la Party*, ou donne à la cible son propre *Style*, *Thème*, *Luminosité* et *Transparence*. *Taille de la cible* met tout le cadre à l'échelle."},
    {2, "*Bars* sets the *Bar Height* and *Bar Width* of the HP bar.",
        "*Barres* règle la *Hauteur* et la *Largeur* de la barre de HP."},
    {2, "*Detail* covers the *Distance* bar (with its own height), *Speed* and *TH*, each shown as text or as an icon, with a shared *Speed/TH Icon Size*.",
        "*Détail* couvre la barre de *Distance* (avec sa propre hauteur), la *Vitesse* et le *TH*, chacun en texte ou en icône, avec une *Taille d'icône Vitesse/TH* commune."},
    {2, "*Debuffs* sets the *Buff position* (Inside, Below, Above, Left or Right), the *Max Debuffs* to show, the *Buff Size*, and whether the *Debuff Timers* are drawn. Left and Right split the icons evenly on both sides.",
        "*Debuffs* règle la *Position des buffs* (Inside, Below, Above, Left ou Right), le *Nombre max de debuffs*, la *Taille des buffs*, et l'affichage des *Minuteurs*. Left et Right répartissent les icônes également des deux côtés."},
    {2, "*Text* styles each piece of text on its own, Name, HP %, Timer, Speed, TH and Distance, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise chaque texte séparément, Nom, HP %, Minuteur, Vitesse, TH et Distance, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Target module and target anything, or lean on the live preview in the corner, to tune the box against a real sample while you change the settings.",
        "Ouvre //aio config sur le module Cible et cible n'importe quoi, ou appuie-toi sur l'aperçu en direct dans le coin, pour régler le cadre sur un vrai échantillon pendant que tu changes les options."},
};
static const int HELP_TARGET_N = (int)(sizeof(HELP_TARGET) / sizeof(HELP_TARGET[0]));

// PLAYER : the Player Hub -- your own box (identity + HP/MP/TP vitals + buffs).
static const HelpItem HELP_PLAYER[] = {
    {0, "Player Hub", "Hub Joueur"},
    {1, "The *Player Hub* is your own box, the counterpart to the Target box but always on you : who you are, your vitals, your buffs, your gil, your movement speed and your gear, all at a glance. Every part turns on or off on its own, so you build exactly the panel you want.",
        "Le *Hub Joueur* est ta propre box, le pendant de la box Cible mais toujours sur toi : qui tu es, tes jauges, tes buffs, ton gil, ta vitesse de déplacement et ton équipement, le tout d'un coup d'oeil. Chaque partie s'active ou se coupe séparément, pour construire exactement le panneau que tu veux."},
    {46, "", ""},

    {0, "Identity", "Identité"},
    {1, "The top row is who you are : your *job emblem*, tinted by your role (tank, healer, damage, support), then your *name*, and your *main and sub jobs* with their levels, PLD 99 / WAR 54 for example.",
        "La ligne du haut, c'est qui tu es : ton *emblème de job*, teinté par ton rôle (tank, healer, damage, support), puis ton *nom*, et tes *jobs principal et secondaire* avec leurs niveaux, PLD 99 / WAR 54 par exemple."},

    {0, "Vitals", "Jauges"},
    {1, "Your *HP*, *MP* and *TP* as the same living fioles used across the HUD, with a glass sheen and the liquid look.",
        "Tes *HP*, *MP* et *TP* en fioles vivantes, les mêmes que partout dans le HUD, avec le reflet de verre et le rendu liquide."},
    {2, "*HP* runs green through yellow and orange to red as it drops. *MP* is blue. *TP* fills as it builds and lights up at 1000, ready for a weapon skill.",
        "Les *HP* vont du vert au rouge en passant par le jaune et l'orange à mesure qu'ils baissent. Les *MP* sont bleus. Les *TP* se remplissent et s'illuminent à 1000, prêts pour une weapon skill."},
    {1, "While you cast, the spell name and a progress bar breathe just under the vitals, so you follow your own cast without the game's bar.",
        "Quand tu incantes, le nom du sort et une barre de progression pulsent juste sous les jauges, pour suivre ta propre incantation sans la barre du jeu."},

    {0, "Buffs, gil and speed", "Buffs, gil et vitesse"},
    {2, "Your *status icons* wrap into rows, capped by *Max Buffs* so the box stays tidy.",
        "Tes *icônes de statut* se répartissent en lignes, limitées par *Max Buffs* pour garder la box propre."},
    {2, "Your *gil* and your *movement speed* (a +/- percent, green faster / red slower) can sit in the box too.",
        "Ton *gil* et ta *vitesse de déplacement* (un pourcentage +/-, vert plus rapide / rouge plus lent) peuvent aussi figurer dans la box."},

    {0, "Equipment", "Équipement"},
    {1, "An optional *equipment viewer* shows your 16 equipped items in a 4x4 grid. Place it inside the box, or dock it to a side (left, right, top or bottom) so it hugs the hub without crowding it.",
        "Un *visualiseur d'équipement* optionnel montre tes 16 objets équipés dans une grille 4x4. Place-le dans la box, ou accroche-le sur un côté (gauche, droite, haut ou bas) pour qu'il colle au hub sans l'encombrer."},

    {0, "Configuration", "Configuration"},
    {1, "The Player module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Joueur dans //aio config règle la box, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "Turn each section on or off : *Emblem*, *Name*, *Level*, *HP / MP / TP*, *Buffs*, *Gil*, *Speed* and the *Equipment* viewer.",
        "Active ou coupe chaque section : *Emblème*, *Nom*, *Niveau*, *HP / MP / TP*, *Buffs*, *Gil*, *Vitesse* et le visualiseur d'*Équipement*."},
    {2, "*Size* scales the box, *Bar Height / Width* size the fioles, *Max Buffs* caps the buff count, and *Equipment placement* sets the grid inside or docked. *Box / Frame* sets the theme and can turn the frame off. *Text* styles each piece on its own.",
        "*Taille* met la box à l'échelle, *Hauteur / Largeur de barre* dimensionnent les fioles, *Max Buffs* limite les buffs, et *Placement équipement* met la grille dedans ou accrochée. *Box / Cadre* règle le thème et peut couper le cadre. *Texte* stylise chaque élément séparément."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Player module to tune the whole hub against a live sample of yourself. Drag it anywhere in //aio edit, and roll the wheel over it to resize.",
        "Ouvre //aio config sur le module Joueur pour régler tout le hub sur un aperçu vivant de toi-même. Déplace-le où tu veux avec //aio edit, et utilise la molette dessus pour redimensionner."},
};
static const int HELP_PLAYER_N = (int)(sizeof(HELP_PLAYER) / sizeof(HELP_PLAYER[0]));

// MINIMAP : the round/square map of your zone + its Vana'diel clock box. Live samples (kinds 30-33) draw the
// REAL elements -- the map disc, the marker legend, the moon and the elemental day -- via minimap_help_*.
static const HelpItem HELP_MINIMAP[] = {
    {0, "Minimap", "Minicarte"},
    {1, "The *Minimap* draws a live map of the zone you are in, with you always at its centre and everything around you, party, players, NPCs and monsters, shown as coloured markers that move in real time. Above the map sits an optional *Vana'diel clock* with the time, the elemental day, and the moon. It is drawn straight from the game's own maps, so it always matches where you actually are.",
        "La *Minicarte* dessine en direct la carte de la zone où tu te trouves, avec toi toujours en son centre et tout ce qui t'entoure, party, joueurs, PNJ et monstres, montré par des marqueurs colorés qui bougent en temps réel. Au-dessus de la carte se pose une *horloge Vana'diel* optionnelle avec l'heure, le jour élémentaire et la lune. Elle est tirée des cartes du jeu, donc elle correspond toujours à l'endroit où tu es vraiment."},

    {0, "What the map shows", "Ce que montre la carte"},
    {1, "You are the pin at the very centre, and the map scrolls under you as you walk. It can be a *round* lens or a *square* panel, framed by your chosen box theme, and it is *north-up*, so the top of the map is always north. Roll the mouse wheel over it to zoom in and out, from the whole zone down to a close look around you.",
        "Tu es le pin au centre exact, et la carte défile sous toi quand tu marches. Elle peut être une lentille *ronde* ou un panneau *carré*, encadrée par le thème de box que tu as choisi, et elle est *nord en haut*, donc le haut de la carte est toujours le nord. Utilise la molette dessus pour zoomer et dézoomer, de la zone entière jusqu'à une vue rapprochée autour de toi."},
    {30, "", ""},
    {1, "If the current zone has no map on file, the box shows just the clock header, so the Vana'diel time and moon stay with you everywhere.",
        "Si la zone actuelle n'a pas de carte disponible, le cadre n'affiche que l'en-tête d'horloge, pour garder l'heure Vana'diel et la lune partout avec toi."},

    {0, "The markers", "Les marqueurs"},
    {1, "Every entity around you is a marker, and its colour tells you at a glance what it is and, for a monster, whether it is yours to fight. You point up-arrows for monsters, small dots for players and NPCs, and the pin for yourself.",
        "Chaque entité autour de toi est un marqueur, et sa couleur te dit d'un coup d'oeil ce que c'est et, pour un monstre, s'il est à toi. Des flèches pour les monstres, de petites pastilles pour les joueurs et les PNJ, et le pin pour toi-même."},
    {31, "", ""},
    {1, "A monster you or your party have claimed, or that is fighting you, turns *red*, an unclaimed one stays *gold*, and one another player has claimed shows *magenta*, so you never swing at someone else's mob by mistake.",
        "Un monstre que toi ou ta party avez réclamé, ou qui te combat, passe au *rouge*, un monstre non réclamé reste *or*, et celui qu'un autre joueur a réclamé apparaît en *magenta*, pour ne jamais frapper le mob de quelqu'un d'autre par erreur."},

    {0, "The Vana'diel clock", "L'horloge Vana'diel"},
    {1, "The clock box on top gathers everything time-related in Vana'diel. It carries the current *time*, the *elemental day* flowing into the next one, the *moon* with its phase, and, if you like, your own *real* and *GMT* time to keep an eye on the clock behind the game.",
        "Le cadre d'horloge en haut rassemble tout ce qui touche au temps dans Vana'diel. Il porte l'*heure* actuelle, le *jour élémentaire* qui s'enchaîne vers le suivant, la *lune* avec sa phase, et, si tu veux, ta propre heure *réelle* et *GMT* pour garder un oeil sur l'horloge derrière le jeu."},

    {0, "The moon", "La lune"},
    {1, "The moon is drawn for real, lit from the correct side and shaded by its phase, and it counts down the days to the next *New* and *Full* moon, handy for anything in Vana'diel that leans on the lunar phase.",
        "La lune est dessinée pour de vrai, éclairée du bon côté et ombrée selon sa phase, et elle décompte les jours jusqu'aux prochaines *Nouvelle* et *Pleine* lunes, pratique pour tout ce qui dépend de la phase lunaire dans Vana'diel."},
    {32, "live, sweeping New to Full", "en direct, de la Nouvelle à la Pleine lune"},

    {0, "The elemental day", "Le jour élémentaire"},
    {1, "Each Vana'diel day belongs to one of the eight elements, Fire, Earth, Water, Wind, Ice, Lightning, Light and Dark, shown by its icon and tint. The clock names the current day and points to the next, so you always know which element is in the air.",
        "Chaque jour de Vana'diel appartient à l'un des huit éléments, Feu, Terre, Eau, Vent, Glace, Foudre, Lumière et Ténèbres, montré par son icône et sa teinte. L'horloge nomme le jour en cours et pointe vers le suivant, pour toujours savoir quel élément est dans l'air."},
    {33, "the eight elemental days", "les huit jours élémentaires"},

    {0, "Configuration", "Configuration"},
    {1, "The Minimap module in //aio config groups its settings into five sections.",
        "Le module Minicarte dans //aio config regroupe ses réglages en cinq sections."},
    {2, "*Display* turns the map on with *Show*, sets the *Size* and the *Map size* on their own, the *Zoom* level, and the *Map background* opacity, from a solid backdrop down to fully transparent so the game shows through.",
        "*Affichage* active la carte avec *Afficher*, règle la *Taille* et la *Taille carte* séparément, le niveau de *Zoom*, et l'opacité du *Fond de carte*, d'un fond plein jusqu'à totalement transparent pour laisser voir le jeu."},
    {2, "*Clock* toggles the whole *Clock header* and then each row apart, *Vana'diel time*, *Elemental day*, *Moon phase* and *Real / GMT time*, so you keep only the ones you want.",
        "*Horloge* active tout l'*En-tête horloge* puis chaque ligne à part, *Heure Vana'diel*, *Jour élémentaire*, *Phase de lune* et *Heure réelle / GMT*, pour ne garder que celles que tu veux."},
    {2, "*Text* styles each clock element, *Time*, *Day*, *Moon* and *Real / GMT*, on its own, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise chaque élément d'horloge, *Heure*, *Jour*, *Lune* et *Réel / GMT*, séparément, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},
    {2, "*Shape & Frame* picks the *Shape*, Square or Round, and the *Frame*, None, your Box Theme, or a Custom colour of your own.",
        "*Forme & Cadre* choisit la *Forme*, Carré ou Rond, et le *Cadre*, Aucun, ton Thème box, ou une couleur Perso à toi."},
    {2, "*Markers* sets the *Marker Size* and turns each type on or off, *Players (PC)*, *NPCs* and *Monsters*, so the map stays as busy or as clean as you want.",
        "*Marqueurs* règle la *Taille marqueurs* et active ou non chaque type, *Joueurs (PC)*, *PNJ* et *Monstres*, pour une carte aussi chargée ou aussi épurée que tu veux."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Minimap module while you play to tune the box, the map and the clock against the real thing. Drag it anywhere in //aio edit, and roll the wheel over it in game to set your zoom.",
        "Ouvre //aio config sur le module Minicarte pendant que tu joues pour régler le cadre, la carte et l'horloge sur le vrai rendu. Déplace-la où tu veux avec //aio edit, et utilise la molette dessus en jeu pour régler ton zoom."},
};
static const int HELP_MINIMAP_N = (int)(sizeof(HELP_MINIMAP) / sizeof(HELP_MINIMAP[0]));

// HATE LIST : the mobs that have aggro on you / your party, one HP fiole per mob, lowest-HP on top. Live sample
// (kind 40) draws the REAL box in preview mode via hatelist_help_box, so it matches your config exactly.
static const HelpItem HELP_HATELIST[] = {
    {0, "Hate List", "Liste d'aggro"},
    {1, "The *Hate List* shows every monster that has aggro on you or your party, one slim HP fiole per mob, sorted so the closest to death sits on top. It is your kill order at a glance in a big pull, so you finish the low ones first and never lose track of what is beating on the group.",
        "La *Liste d'aggro* montre chaque monstre qui a de la haine sur toi ou ta party, une fine fiole de HP par mob, tri\xC3\xA9""e pour que le plus proche de la mort soit tout en haut. C'est ton ordre de kill d'un coup d'oeil dans un gros pull, pour finir les bas d'abord et ne jamais perdre de vue ce qui tape sur le groupe."},
    {40, "", ""},

    {0, "Reading a row", "Lire une ligne"},
    {1, "Each row is one mob : its distance on the left, its name and remaining HP on the fiole, the HP percentage, and on the right the party member it is fixed on.",
        "Chaque ligne est un mob : sa distance \xC3\xA0 gauche, son nom et ses HP restants sur la fiole, le pourcentage de HP, et \xC3\xA0 droite le membre de la party sur qui il est fix\xC3\xA9."},
    {2, "The *HP fiole* fills from *green* when the mob is healthy down through yellow to *red* as it dies, its own continuous ramp so you read the health from the colour alone.",
        "La *fiole de HP* se remplit du *vert* quand le mob est en forme jusqu'au *rouge* quand il meurt, sa propre rampe continue pour lire la vie rien qu'\xC3\xA0 la couleur."},
    {2, "The *name* sits over the bar, trimmed with .. when it is too long, and the *HP %* reads out on the right.",
        "Le *nom* est pos\xC3\xA9 sur la barre, coup\xC3\xA9 avec .. quand il est trop long, et le *HP %* s'affiche \xC3\xA0 droite."},
    {2, "The *distance* on the left is in yalms, so you know how far each aggroed mob is.",
        "La *distance* \xC3\xA0 gauche est en yalms, pour savoir \xC3\xA0 quelle distance est chaque mob aggro."},

    {0, "Order and claim", "Ordre et claim"},
    {1, "Rows are sorted by HP, lowest on top, so your next kill is always at the top of the box. The order stays stable even in a hundred-mob pull, it will not flicker as the mobs trade blows.",
        "Les lignes sont tri\xC3\xA9""es par HP, le plus bas en haut, pour que ton prochain kill soit toujours en haut du cadre. L'ordre reste stable m\xC3\xAAme dans un pull de cent mobs, il ne clignote pas quand les mobs \xC3\xA9""changent des coups."},
    {2, "The mob you are *targeting* is framed, *gold* normally and *red* when it is claimed by someone outside your party, so you spot your current target in the list at a glance.",
        "Le mob que tu *cibles* est encadr\xC3\xA9, *or* normalement et *rouge* quand il est claim par quelqu'un hors de ta party, pour rep\xC3\xA9rer ta cible actuelle dans la liste d'un coup d'oeil."},
    {2, "The *>> name* on the right is the party member the mob is fixed on, so you know who is tanking or getting hit.",
        "Le *>> nom* \xC3\xA0 droite est le membre de la party sur qui le mob est fix\xC3\xA9, pour savoir qui tank ou se fait taper."},

    {0, "Configuration", "Configuration"},
    {1, "The Hate List module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Liste d'aggro dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Box / Frame* sets the window theme and can turn the frame off (the fioles then float bare). *Size* scales the whole box.",
        "*Box / Cadre* r\xC3\xA8gle le th\xC3\xA8me de fen\xC3\xAAtre et peut couper le cadre (les fioles flottent alors nues). *Taille* met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle."},
    {2, "*Max mobs* caps how many mobs show at once, so you keep only the lowest-HP few. *Distance* and *Target* toggle those two side columns.",
        "*Mobs max* limite combien de mobs s'affichent, pour ne garder que les quelques plus bas en HP. *Distance* et *Cible* activent ces deux colonnes lat\xC3\xA9rales."},
    {2, "*Text* styles each piece, Distance, Name, HP% and Target, on its own, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise chaque \xC3\xA9l\xC3\xA9ment, Distance, Nom, PV% et Cible, s\xC3\xA9par\xC3\xA9ment, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Hate List module to tune it against a sample list, or pull a pack of mobs and watch it fill live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Liste d'aggro pour le r\xC3\xA9gler sur une liste d'exemple, ou pull un paquet de mobs et regarde-la se remplir en direct. D\xC3\xA9place-la o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_HATELIST_N = (int)(sizeof(HELP_HATELIST) / sizeof(HELP_HATELIST[0]));

// POINTWATCH : your progression (XP / CP / ML) + Merits, each with a live per-hour rate. Live sample (kind 41)
// draws the REAL box in preview mode via pointwatch_help_box, config-aware (layout / display / text / rate).
static const HelpItem HELP_POINTWATCH[] = {
    {0, "PointWatch", "PointWatch"},
    {1, "*PointWatch* tracks your progression at a glance : your current stage -- XP under level 99, Capacity Points at 99, or Master-Level Exemplar Points once you are mastered -- plus your Merits, each shown with how far along you are and how fast you are earning per hour.",
        "*PointWatch* suit ta progression d'un coup d'oeil : ton stade actuel -- XP sous le niveau 99, Capacity Points à 99, ou Exemplar Points de Master Level une fois master -- plus tes Merits, chacun avec ta progression et ta vitesse de gain par heure."},
    {41, "", ""},

    {0, "The progression row", "La ligne de progression"},
    {1, "The first row follows the stage that fits your character and switches on its own as you level.",
        "La première ligne suit le stade qui correspond à ton personnage et bascule seule quand tu montes."},
    {2, "*XP* while under level 99, current over the amount required for the next level.",
        "*XP* tant que tu es sous le niveau 99, actuel sur le montant requis pour le niveau suivant."},
    {2, "*CP* (Capacity Points) at 99, with your *Job Points* count beside it.",
        "*CP* (Capacity Points) à 99, avec ton nombre de *Job Points* à côté."},
    {2, "*ML* (Master Level) once you are mastered, your Exemplar Points toward the next master level.",
        "*ML* (Master Level) une fois master, tes Exemplar Points vers le prochain master level."},
    {2, "The *X/h* on the right is your live earn rate, built from your recent gains, so you know your pace and can estimate the time left.",
        "Le *X/h* à droite est ta vitesse de gain en direct, calculée sur tes gains récents, pour connaître ton rythme et estimer le temps restant."},

    {0, "The Merits row", "La ligne Merits"},
    {1, "The second row is always your *Merits* : your Limit Points toward the next merit, and your held merits over the maximum you can carry -- when you are capped both counts simply show the max (e.g. 10k / 10k, 75 / 75).",
        "La deuxième ligne est toujours tes *Merits* : tes Limit Points vers le prochain merit, et tes merits détenus sur le maximum que tu peux porter -- une fois cappé, les deux compteurs affichent simplement le max (ex. 10k / 10k, 75 / 75)."},

    {0, "Configuration", "Configuration"},
    {1, "The PointWatch module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module PointWatch dans //aio config règle le cadre, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Progression* is Auto (by level / ML) or forced to XP, CP or ML. *Layout* stacks the two stats vertically or sets them side by side. *Show* displays the text, the bar, or both.",
        "*Progression* est Auto (par niveau / ML) ou forcé sur XP, CP ou ML. *Disposition* empile les deux stats verticalement ou les met côte à côte. *Afficher* montre le texte, la barre, ou les deux."},
    {2, "*Show rate* toggles the X/h readout. *Box / Frame* sets the window theme and can turn the frame off. *Size* scales the whole box.",
        "*Afficher le taux* active le X/h. *Box / Cadre* règle le thème de fenêtre et peut couper le cadre. *Taille* met tout le cadre à l'échelle."},
    {2, "*Text* styles the Label, Value and Rate on their own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise le Libellé, la Valeur et le Taux séparément, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the PointWatch module to tune it against a sample, then kill a few mobs to watch the value and the rate move live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module PointWatch pour le régler sur un exemple, puis tue quelques mobs pour voir la valeur et le taux bouger en direct. Déplace-le où tu veux avec //aio edit."},
};
static const int HELP_POINTWATCH_N = (int)(sizeof(HELP_POINTWATCH) / sizeof(HELP_POINTWATCH[0]));

// TIMERS : your active buff durations (exact server timers) + your recasts, on one or two boxes. Live sample
// (kind 42) draws the REAL box in preview mode via timers_help_box, config-aware (fused/separate, icon/name, colours).
static const HelpItem HELP_TIMERS[] = {
    {0, "Timers", "Timers"},
    {1, "*Timers* counts down your active *buffs* and your *recasts* on one clean panel, each row an icon and a name with the time left, coloured brighter as it runs out. Buff durations are the game's *exact* server timers, not estimates, so a countdown ends the moment the effect really wears.",
        "*Timers* décompte tes *buffs* actifs et tes *recasts* sur un panneau épuré, chaque ligne une icône et un nom avec le temps restant, coloré plus vif à mesure que ça s'épuise. Les durées de buffs sont les minuteurs *exacts* du serveur, pas des estimations, donc un décompte se termine pile quand l'effet s'estompe vraiment."},
    {42, "", ""},

    {0, "The two columns", "Les deux colonnes"},
    {2, "*Duration* lists your active buffs with the time left on each, so you know when to refresh your food, songs, rolls or self-buffs. It also carries what *other people* put on you — each of those rows names its owner in parentheses.",
        "*Duration* liste tes buffs actifs avec le temps restant sur chacun, pour savoir quand refresh ta bouffe, tes chants, tes rolls ou tes self-buffs. Elle porte aussi ce que les *autres* t'ont posé — chacune de ces lignes nomme son propriétaire entre parenthèses."},
    {2, "*Recast* lists your spells and abilities on cooldown, counting down to when you can use them again.",
        "*Recast* liste tes sorts et aptitudes en récupération, en décomptant jusqu'à ce que tu puisses les relancer."},

    {0, "Reading a timer", "Lire un minuteur"},
    {2, "A *buff* timer is *white* while it has time, turns *orange* under 30 seconds, and flashes *red* in the last 10 seconds, so an expiring effect grabs your eye.",
        "Un minuteur de *buff* est *blanc* tant qu'il a du temps, passe *orange* sous 30 secondes, et clignote en *rouge* dans les 10 dernières secondes, pour qu'un effet qui expire attire l'oeil."},
    {2, "A *recast* is the opposite : *red* just after you use it (a long wait), turning *orange* then *green* as it nears ready, so a glance tells you what is coming back up.",
        "Un *recast* est l'inverse : *rouge* juste après l'avoir utilisé (longue attente), passant *orange* puis *vert* à mesure qu'il approche d'être prêt, pour voir d'un coup d'oeil ce qui revient bientôt."},
    {2, "A Duration row can show the *icon*, the *name*, or *both*. Recast rows are always the *name* (no icon set exists for recasts).",
        "Une ligne Duration peut montrer l'*icône*, le *nom*, ou les *deux*. Les lignes Recast sont toujours le *nom* (pas de jeu d'icônes pour les recasts)."},

    {0, "Fused or separate", "Fusionné ou séparé"},
    {1, "Keep the two columns *fused* in one box, or split them into two boxes you place independently. An empty column simply disappears until something fills it.",
        "Garde les deux colonnes *fusionnées* dans un cadre, ou sépare-les en deux cadres que tu places indépendamment. Une colonne vide disparaît simplement jusqu'à ce que quelque chose la remplisse."},

    {0, "Configuration", "Configuration"},
    {1, "The Timers module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Timers dans //aio config règle le cadre, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Layout* is Fused or Separate. *Duration: show* picks icon, name or both for the Duration column. *Box / Frame* sets the theme and can turn the frame off. *Size* scales the whole box.",
        "*Disposition* est Fusionné ou Séparé. *Duration : afficher* choisit icône, nom ou les deux pour la colonne Duration. *Box / Cadre* règle le thème et peut couper le cadre. *Taille* met tout le cadre à l'échelle."},
    {2, "*Text* styles the Header, Name and Timer on their own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise l'En-tête, le Nom et le Minuteur séparément, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Buff filter", "Filtre de buffs"},
    {1, "A full spellbook is noise. The *Buff filter* section lets you pick, buff by buff, exactly what the Duration column follows. Buffs are grouped by *magic family* — Protect / Shell, Haste, Barspells, Songs, Rolls, Ninjutsu, Food, Aftermath, Signet-type and Crafting — that you fold or unfold, each family header carrying a group toggle that sets the *whole* family at once. It is *job-agnostic* : one setting per buff, stored on the profile and shared across every job — hide Shell once and it stays hidden whatever you play.",
        "Un grimoire complet, c'est du bruit. La section *Filtre de buffs* te laisse choisir, buff par buff, ce que la colonne Duration suit exactement. Les buffs sont groupés par *famille de magie* — Protect / Shell, Haste, Barspells, Chants, Rolls, Ninjutsu, Nourriture, Aftermath, Signet et Artisanat — que tu déplies ou replies, l'en-tête de chaque famille portant un bouton de groupe qui règle *toute* la famille d'un coup. C'est *indépendant du job* : un réglage par buff, stocké sur le profil et partagé entre tous les jobs — masque Shell une fois et il reste masqué quel que soit ce que tu joues."},
    {2, "*Recasts are always shown* — the filter only ever touches buffs, never your cooldowns.",
        "*Les recasts sont toujours affichés* — le filtre ne touche que les buffs, jamais tes récupérations."},

    {0, "The four states", "Les quatre états"},
    {1, "Each buff carries a *dot*. Click it to cycle through four behaviours — the legend just above the list shows each one. The cycle is *Show → Show + alert → Hide → Hide + alert* and back.",
        "Chaque buff porte une *pastille*. Clique dessus pour parcourir quatre comportements — la légende juste au-dessus de la liste les montre. Le cycle est *Afficher → Afficher + alerte → Masquer → Masquer + alerte* puis retour."},
    {2, "*Show* : shown normally in the panel, no alert.",
        "*Afficher* : affiché normalement dans le panneau, aucune alerte."},
    {2, "*Show + alert* : shown, and once it drops under the *Alert under* threshold its name and timer *blink* (recast soon) ; a red \"OUT\" line then replaces it the moment it falls or is dispelled, until you re-cast.",
        "*Afficher + alerte* : affiché, et dès qu'il passe sous le seuil *Alerter sous* son nom et son minuteur *clignotent* (recast bientôt) ; une ligne rouge \"OUT\" le remplace ensuite dès qu'il tombe ou est dispel, jusqu'à ce que tu le relances."},
    {2, "*Hide* : never shown. Silence.",
        "*Masquer* : jamais affiché. Silence."},
    {2, "*Hide + alert* : hidden while healthy, but *appears and blinks* once it drops under the *Alert under* threshold, or shows a red \"OUT\" when it falls / is dispelled. The alert holds for the *Alert duration* then clears itself if you don't re-cast. Both are sliders in the *Alerts* section. Zero clutter, zero misses.",
        "*Masquer + alerte* : caché tant que tout va bien, mais *apparaît et clignote* dès qu'il passe sous le seuil *Alerter sous*, ou affiche un \"OUT\" rouge quand il tombe / est dispel. L'alerte tient le temps réglé par *Durée de l'alerte* puis s'efface si tu ne relances pas. Les deux sont des curseurs dans la section *Alertes*. Zéro encombrement, zéro oubli."},

    {0, "Job Abilities by job", "Aptitudes par job"},
    {1, "Job Abilities don't fit a magic family, so they get their own list *grouped by job* — your *current job* first, in *gold* and expanded, every other job under its own collapsible header. The setting is still global per buff : set an ability once and it applies wherever it shows up, whatever job you are on.",
        "Les aptitudes de job n'entrent pas dans une famille de magie, alors elles ont leur propre liste *groupée par job* — ton *job actuel* en premier, en *doré* et déplié, chaque autre job sous son propre en-tête repliable. Le réglage reste global par buff : règle une aptitude une fois et elle s'applique partout où elle apparaît, quel que soit ton job."},

    {0, "Focus alerts, no false alarms", "Alertes focus, sans faux positif"},
    {2, "An alert fires *only* for a buff the HUD actually saw active then lost, and it obeys the same *Buff source* filter as the rows — so a buff the column hides never alerts either.",
        "Une alerte se déclenche *uniquement* pour un buff que le HUD a réellement vu actif puis perdu, et elle suit le même filtre *Source des buffs* que les lignes — un buff que la colonne masque ne déclenche donc jamais d'alerte."},
    {2, "Change job (you or an ally) and the timers reset cleanly — except SP abilities, whose recast is shared across all jobs.",
        "Change de job (toi ou un allié) et les timers se réinitialisent proprement — sauf les aptitudes SP, dont le recast est partagé entre tous les jobs."},
    {2, "An ally leaving the party, or a zone change, purges their focus alerts automatically.",
        "Un allié qui quitte la party, ou un changement de zone, purge ses alertes focus automatiquement."},

    {0, "Grouping ally buffs", "Grouper les buffs alliés"},
    {1, "Single-target buffs (Haste, Protect) you spread over several allies show *one row per person* by default ; flip *Grouped* to fold them into a single \"(AoE N)\" line. A real AoE — Protectra, a spell under SCH Accession, a COR roll — always groups either way. Rows are ordered *what you cast first, then buffs with no known caster, then other players, then trusts*, and finally AoE groups and each ally. Within every block, the soonest to expire comes first.",
        "Les buffs monocibles (Haste, Protect) que tu répartis sur plusieurs alliés s'affichent *une ligne par personne* par défaut ; bascule *Groupés* pour les replier en une seule ligne \"(AoE N)\". Un vrai AoE — Protectra, un sort sous SCH Accession, un roll COR — se groupe toujours. Les lignes sont ordonnées *ce que tu as lancé d'abord, puis les buffs sans lanceur connu, puis les autres joueurs, puis les trusts*, et enfin les groupes AoE et chaque allié. Dans chaque bloc, le plus proche d'expirer passe en premier."},
    {2, "An area ENHANCING buff you cast (Protect / Regen under SCH Accession, PLD Majesty) keeps its group and countdown across a *zone change* ; songs and rolls drop on a zone, as in game.",
        "Un buff d'AMÉLIORATION de zone que tu lances (Protect / Regen sous Accession SCH, Majesty PLD) garde son groupe et son décompte à travers un *changement de zone* ; les chants et rolls disparaissent au zone, comme en jeu."},
    {2, "The game never sends buff durations for *other* players — only your own — so a timer on an ally is an *estimate* : exact when the buff also lands on you (an AoE), and within a second or two otherwise.",
        "Le jeu n'envoie jamais les durées de buff des *autres* joueurs — seulement les tiennes — donc un minuteur sur un allié est un *estimé* : exact quand le buff te touche aussi (un AoE), et à une ou deux secondes près sinon."},

    {0, "Buff source", "Source des buffs"},
    {1, "The *Buff source* filter picks whose buffs the Duration column keeps : *Mine* only, *Mine + players*, *Mine + trusts*, or *All* (the default). Your own are always kept ; it cleanly drops a trust's Haste or a stat boost you didn't cast. Anything you did not cast carries its owner's name in parentheses.",
        "Le filtre *Source des buffs* choisit quels buffs la colonne Duration garde : *Moi* seul, *Moi + joueurs*, *Moi + trusts*, ou *Tout* (par défaut). Les tiens sont toujours gardés ; il retire proprement le Haste d'un trust ou un stat boost que tu n'as pas lancé. Tout ce que tu n'as pas lancé porte le nom de son propriétaire entre parenthèses."},
    {2, "Buffs you carry yourself with no caster — food, the conquest Signet family, Aftermath, crafting Imagery — are never dropped by *Mine only* ; their own Buff-filter toggle controls them, and very long ones (a 13-hour Signet) still show.",
        "Les buffs que tu portes toi-même sans lanceur — nourriture, la famille Signet de conquête, Aftermath, l'Imagery d'artisanat — ne sont jamais retirés par *Moi seul* ; c'est leur propre interrupteur dans le Filtre de buffs qui les contrôle, et les très longs (un Signet de 13 heures) s'affichent quand même."},

    {0, "Who cast what", "Qui a lancé quoi"},
    {1, "Several spells can share one internal effect id — every march is *Enhances Attack*, every minuet *Enhances Attack Speed*. The panel resolves each row to the *real* spell and the *real* caster, so your Honor March and a Trust's Victory March are two distinct rows with the right names and the right owners, even when they expire in the same second.",
        "Plusieurs sorts peuvent partager un même identifiant d'effet interne — toutes les marches, tous les menuets. Le panneau résout chaque ligne vers le *vrai* sort et le *vrai* lanceur, donc ton Honor March et la Victory March d'un trust sont deux lignes distinctes, avec les bons noms et les bons propriétaires, même si elles expirent à la même seconde."},
    {2, "Song modifier tags (*SV*, *NT*, *TR*, *M*) reflect the *singer's* job abilities at cast time — on your own songs and on another player's or a trust's alike.",
        "Les marqueurs de chants (*SV*, *NT*, *TR*, *M*) reflètent les aptitudes du *chanteur* au moment de l'incantation — sur tes propres chants comme sur ceux d'un autre joueur ou d'un trust."},
    {2, "When a buff cannot be traced to anyone — food, gear, an effect the game never announced — the row stays, simply without an owner. The panel never guesses a caster, because a wrong guess could hide a buff you actually have.",
        "Quand un buff ne peut être rattaché à personne — nourriture, équipement, un effet que le jeu n'annonce pas — la ligne reste, simplement sans propriétaire. Le panneau ne devine jamais un lanceur : une erreur pourrait masquer un buff que tu as réellement."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Timers module to tune it against a sample, or buff up and blow a few recasts to watch it live. Drag each box in //aio edit.",
        "Ouvre //aio config sur le module Timers pour le régler sur un exemple, ou buff-toi et lance quelques sorts pour le voir en direct. Déplace chaque cadre avec //aio edit."},
};
static const int HELP_TIMERS_N = (int)(sizeof(HELP_TIMERS) / sizeof(HELP_TIMERS[0]));

// SKILLCHAINS : the live resonance on your target -- property, window timer, step + continuation weapon skills.
// Live sample (kind 43) draws the REAL box in preview mode via skillchains_help_box, config-aware (lines / theme).
static const HelpItem HELP_SKILLCHAINS[] = {
    {0, "Skillchains", "Skillchains"},
    {1, "*Skillchains* reads the resonance on your target and shows you, live, the open skillchain : the element to magic burst, a countdown window, the weapon skills that would continue it, and whether your TP is up. When you or your party open a chain, the box lights up so you know exactly what to feed it and when.",
        "*Skillchains* lit la résonance sur ta cible et t'affiche, en direct, le skillchain ouvert : l'élément à magic burst, une fenêtre de décompte, les weapon skills qui le prolongeraient, et si tes TP sont prêts. Quand toi ou ta party ouvrez un chain, le cadre s'allume pour savoir exactement quoi enchaîner et quand."},
    {43, "", ""},

    {0, "Reading the box", "Lire le cadre"},
    {2, "The *title* names the box. The *timer* line reads *Wait* (red), then *Go!* (green) — it flips a touch early so a weapon skill you start still lands with normal server lag. A closing *Burst* counts down precisely, fading green through yellow to red as it ends.",
        "Le *titre* nomme le cadre. La ligne *timer* affiche *Wait* (rouge), puis *Go!* (vert) — elle bascule un peu en avance pour qu'une weapon skill lancée à ce moment atterrisse malgré la latence serveur normale. Un *Burst* de fermeture décompte précisément, virant du vert au jaune puis au rouge en finissant."},
    {2, "The *Step* line shows the chain length and the move that opened it, *Step: 2 -> Savage Blade* for example.",
        "La ligne *Step* montre la longueur du chain et le coup qui l'a ouvert, *Step: 2 -> Savage Blade* par exemple."},
    {2, "Once the chain forms, the *Nuke:* line shows the burst *elements* it opens — each a colour *pip* plus its name — so a nuker reads the element to magic burst at a glance. Before it forms, it shows the active property in its element colour instead.",
        "Une fois le chain formé, la ligne *Nuke:* montre les *éléments* de burst qu'il ouvre — chacun une *pastille* de couleur et son nom — un nukeur lit l'élément à magic burst d'un coup d'oeil. Avant qu'il se forme, elle montre la propriété active dans sa couleur d'élément."},
    {2, "The *continuation list* lines up the weapon skills that would extend the chain, each with its level, the property it would make, and colour *pips* for the burst elements that property opens — so you pick the next move, and its element, at a glance.",
        "La *liste de continuation* aligne les weapon skills qui prolongeraient le chain, chacune avec son niveau, la propriété qu'elle ferait, et des *pastilles* de couleur pour les éléments de burst que cette propriété ouvre — pour choisir le prochain coup, et son élément, d'un coup d'oeil."},
    {2, "A *TP gauge* at the bottom fills toward 1000 and turns *green* the moment you can weapon skill, so you know the listed moves are actually doable now.",
        "Une *jauge de TP* en bas se remplit vers 1000 et passe au *vert* dès que tu peux weapon skill, pour savoir si les coups listés sont réellement faisables maintenant."},
    {2, "Detection covers *SCH Immanence* elemental spells — they open and continue chains — and *Aeonic* weapons : while you wield one with the matching Aftermath its Radiance / Umbra close is offered, and a look-alike non-Aeonic never is.",
        "La détection couvre les sorts élémentaires sous *Immanence* (SCH) — ils ouvrent et prolongent les chains — et les armes *Aeonic* : tant que tu en portes une avec l'Aftermath correspondant sa fermeture Radiance / Umbra est proposée, et une arme non-Aeonic qui y ressemble ne l'est jamais."},

    {0, "The window", "La fenêtre"},
    {1, "After a skill opens the chain there is a short *Wait*, then the *Go! / Burst* window during which the next weapon skill, or a magic burst, lands on the resonance. Miss the window and it fades and the box clears.",
        "Après qu'un coup ouvre le chain il y a un court *Wait*, puis la fenêtre *Go! / Burst* pendant laquelle la weapon skill suivante, ou un magic burst, tombe sur la résonance. Rate la fenêtre et elle s'estompe, le cadre se vide."},

    {0, "Configuration", "Configuration"},
    {1, "The Skillchains module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Skillchains dans //aio config règle le cadre, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Elements* toggles each row on its own : the *Title*, the *Timer*, the *Step*, the *Properties* (the Nuke line), the continuation *List* and the *TP indicator*.",
        "*Éléments* active chaque rangée séparément : le *Titre*, le *Timer*, le *Step*, les *Propriétés* (la ligne Nuke), la *Liste* de continuation et l'*indicateur TP*."},
    {2, "*Show party/nearby chains* also shows a party member's chain on your battle target or a nearby mob, not only your cursor target, so a supporter still sees the chain to burst.",
        "*Afficher SC du groupe/proches* montre aussi le chain d'un membre de party sur ta cible de combat ou un mob proche, pas seulement ta cible curseur, pour qu'un support voie quand même le chain à burst."},
    {2, "*Box / Frame* sets the theme and can turn the frame off. *Size* scales the box. *Text* styles the Title, Timer, Step, Properties and List each on its own.",
        "*Box / Cadre* règle le thème et peut couper le cadre. *Taille* met le cadre à l'échelle. *Texte* stylise le Titre, le Timer, le Step, les Propriétés et la Liste chacun séparément."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Skillchains module to tune it against a looping sample, or open a real chain on your target to see it live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Skillchains pour le régler sur un exemple qui boucle, ou ouvre un vrai chain sur ta cible pour le voir en direct. Déplace-le où tu veux avec //aio edit."},
};
static const int HELP_SKILLCHAINS_N = (int)(sizeof(HELP_SKILLCHAINS) / sizeof(HELP_SKILLCHAINS[0]));

// TREASURE POOL : the party lottery pool -- coffer icon + one row per item (index / name / expiry timer / lotter).
// Live sample (kind 44) draws the REAL box in preview mode via treasure_help_box, config-aware (icon / theme / text).
static const HelpItem HELP_TREASURE[] = {
    {0, "Treasure Pool", "Pool de trésor"},
    {1, "*Treasure Pool* shows the party's lottery pool at a glance : every item waiting to be lotted, the time left before it drops, and who is winning it right now. No more opening the game menu to see what is up for grabs.",
        "*Pool de trésor* montre le pool de loterie de la party d'un coup d'oeil : chaque objet en attente de lot, le temps restant avant qu'il tombe, et qui le remporte à l'instant. Fini d'ouvrir le menu du jeu pour voir ce qui est à gagner."},
    {44, "", ""},

    {0, "Reading a row", "Lire une ligne"},
    {1, "Each row is one item in the pool, read left to right.",
        "Chaque ligne est un objet du pool, lu de gauche à droite."},
    {2, "The *index* numbers the rows shown, from 1 upward, in order of time left -- it is a display counter, not the game's own pool slot.",
        "L'*index* numérote les lignes affichées, à partir de 1, dans l'ordre du temps restant -- c'est un compteur d'affichage, pas la place réelle de l'objet dans le pool du jeu."},
    {2, "The *name* is the item itself.",
        "Le *nom* est l'objet lui-même."},
    {2, "The *timer* counts down the roughly five minutes before the item drops out of the pool, going *green* (over 3 min) to *orange* (1-3 min) to *red* (under a minute) as it runs out.",
        "Le *timer* décompte les cinq minutes environ avant que l'objet quitte le pool, passant du *vert* (plus de 3 min) à l'*orange* (1-3 min) puis au *rouge* (moins d'une minute) à mesure que le temps s'épuise."},
    {2, "The *winner* on the right is whoever currently holds the highest lot, with their score, so you know whether to lot or pass.",
        "Le *gagnant* à droite est celui qui a le plus haut lot pour l'instant, avec son score, pour savoir s'il faut loter ou passer."},

    {0, "The coffer", "Le coffre"},
    {1, "An optional treasure-chest icon sits on the left of the box, a clear cue that the pool has something in it. Turn it off to keep the box compact.",
        "Une icône de coffre optionnelle est à gauche du cadre, un repère clair que le pool contient quelque chose. Coupe-la pour garder le cadre compact."},

    {0, "Configuration", "Configuration"},
    {1, "The Treasure Pool module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Pool de trésor dans //aio config règle le cadre, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Coffer icon* shows or hides the chest. *Box / Frame* sets the theme and can turn the frame off. *Size* scales the whole box.",
        "*Icône coffre* affiche ou masque le coffre. *Box / Cadre* règle le thème et peut couper le cadre. *Taille* met tout le cadre à l'échelle."},
    {2, "*Text* styles the Index, Name, Timer and Winner each on its own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise l'Index, le Nom, le Timer et le Gagnant chacun séparément, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Treasure Pool module to tune it against a sample pool, or fight for drops to see it fill live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Pool de trésor pour le régler sur un pool d'exemple, ou combats pour des drops pour le voir se remplir en direct. Déplace-le où tu veux avec //aio edit."},
};
static const int HELP_TREASURE_N = (int)(sizeof(HELP_TREASURE) / sizeof(HELP_TREASURE[0]));

// ZONE TRACKER : a chameleon box that follows the special content you are in (Dynamis / Abyssea / Omen / Nyzul /
// Odyssey). Live sample (kind 45) draws the REAL box in preview mode via zonetracker_help_box (your chosen variant).
static const HelpItem HELP_ZONETRACKER[] = {
    {0, "Zone Tracker", "Suivi de zone"},
    {1, "*Zone Tracker* is a chameleon box that follows the special content you are in and shows exactly what matters there. It appears on its own when you enter that content and reshapes to fit, then folds away when you leave.",
        "*Suivi de zone* est un cadre caméléon qui suit le contenu spécial où tu es et montre exactement ce qui compte là. Il apparaît seul quand tu entres dans ce contenu et se reforme pour s'adapter, puis se range quand tu pars."},
    {45, "", ""},

    {0, "What it tracks", "Ce qu'il suit"},
    {2, "*Dynamis* : your time-extension granules and a run timer counting your time in the zone.",
        "*Dynamis* : tes granules d'extension de temps et un timer de run qui décompte ton temps dans la zone."},
    {2, "*Abyssea* : the seven lights (Pearl, Azure, Ruby, Amber, Gold, Silver, Ebon) and your visitant time left.",
        "*Abyssea* : les sept lumières (Pearl, Azure, Ruby, Amber, Gold, Silver, Ebon) et ton temps de visitant restant."},
    {2, "*Omen* : your bonus objectives and their progress, with the omen count and the bonus timer.",
        "*Omen* : tes objectifs bonus et leur progression, avec le compteur d'omens et le timer de bonus."},
    {2, "*Nyzul Isle* : the current floor, the floor timer, your objective and restriction, floors cleared, and a live token estimate.",
        "*Nyzul Isle* : l'étage actuel, le timer d'étage, ton objectif et ta restriction, les étages faits, et une estimation de tokens en direct."},
    {2, "*Sheol* (Odyssey) : the Mog Segments you have banked this run, plus resistances, the NM family and any Cruel Joke.",
        "*Sheol* (Odyssey) : les Mog Segments accumulés durant ce run, plus les résistances, la famille du NM et l'éventuel Cruel Joke."},
    {2, "*Limbus* : the area and its level, the floor with its progress gauge, your Temenos / Apollyon units, what this run has banked and how many data collections are left this week -- plus one dot per quadrant coffer (dim = not opened, red = 3k, green = 5k).",
        "*Limbus* : la zone et son niveau, l'étage et sa jauge de progression, tes units Temenos / Apollyon, ce que ce run a rapporté et le nombre de collectes restantes cette semaine -- plus une pastille par coffre de quadrant (éteinte = pas ouvert, rouge = 3k, verte = 5k)."},

    {0, "Configuration", "Configuration"},
    {1, "The Zone Tracker module in //aio config tunes the box, with a live preview on the right.",
        "Le module Suivi de zone dans //aio config règle le cadre, avec un aperçu en direct à droite."},
    {2, "*Content* picks which zone to work on : it drives the preview AND which options the panel shows, so you can style and place the box even when you are not in that zone.",
        "*Contenu* choisit la zone sur laquelle tu travailles : il pilote l'aperçu ET les options affichées dans le panneau, pour styliser et placer le cadre même quand tu n'es pas dans cette zone."},
    {2, "*Box / Frame* sets the theme and can turn the frame off. *Size* scales the whole box, and *Show title* hides the title row.",
        "*Box / Cadre* règle le thème et peut couper le cadre. *Taille* met tout le cadre à l'échelle, et *Afficher le titre* masque la ligne de titre."},
    {2, "*Every zone* is configurable row by row : pick the preview variant and the panel shows only that zone's switches. Dynamis (run timer, key items), Abyssea (visitant timer, lights), Omen (floor objective, omens + bonus, objective rows), Nyzul (floor, floor timer, objective, restriction, floors cleared, reward rate, tokens), Sheol (segments, resistances, family name, Cruel Joke) and Limbus (name row, floor on the gauge, currencies, run total, coffer dots).",
        "*Chaque zone* se règle ligne par ligne : choisis la variante d'aperçu et le panneau n'affiche que les interrupteurs de cette zone. Dynamis (timer de run, objets clés), Abyssea (timer visitant, lumières), Omen (objectif d'étage, omens + bonus, lignes d'objectifs), Nyzul (étage, timer d'étage, objectif, restriction, étages faits, taux de récompense, tokens), Sheol (segments, résistances, nom de famille, Cruel Joke) et Limbus (ligne du nom, étage sur la jauge, monnaies, total du run, pastilles de coffres)."},
    {2, "*Text* styles each of those rows on its own : the element list follows the preview variant, so every zone exposes exactly its own text elements (font, size, outline, bold / italic / caps and colour).",
        "*Texte* stylise chacune de ces lignes séparément : la liste d'éléments suit la variante d'aperçu, donc chaque zone expose exactement ses propres éléments de texte (police, taille, contour, gras / italique / capitales et couleur)."},
    {2, "*Bars and gauges are dimensionable* : the Dynamis / Abyssea timer bar width and height, the Dynamis key-item dot size, the Abyssea light-bar width and height, the Sheol weapon-icon and element-dot sizes, and the Limbus gauge width and height.",
        "*Les barres et jauges sont dimensionnables* : largeur et hauteur de la barre de timer Dynamis / Abyssea, taille des pastilles d'objets clés Dynamis, largeur et hauteur des barres de lumières Abyssea, taille des icônes d'armes et des pastilles d'éléments Sheol, et largeur / hauteur de la jauge Limbus."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the Zone Tracker module and pick a preview variant to tune it, or just enter the content to see it live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Suivi de zone et choisis une variante d'aperçu pour le régler, ou entre simplement dans le contenu pour le voir en direct. Déplace-le où tu veux avec //aio edit."},
};
static const int HELP_ZONETRACKER_N = (int)(sizeof(HELP_ZONETRACKER) / sizeof(HELP_ZONETRACKER[0]));

// EMPYPOP : the pop items + key items needed to spawn an Abyssea empyrean NM. Live sample (kind 47) draws the
// REAL stack in preview mode via empypop_help_box (the built-in chloris chain, so it renders for any player).
static const HelpItem HELP_EMPYPOP[] = {
    {0, "EmpyPop", "EmpyPop"},
    {1, "*EmpyPop* lists everything you need to pop one Abyssea empyrean NM : the pop items and key items, the placeholders that drop them and where, and, when the NM has one, its collectable progress. Pick the NM you are farming and the box lays out the whole spawn chain so you always know the next step.",
        "*EmpyPop* liste tout ce qu'il faut pour faire appara\xC3\xAetre un NM emp\xC3\xA9r\xC3\xA9""en d'Abyssea : les items de pop et key items, les placeholders qui les d\xC3\xA9posent et o\xC3\xB9, et, quand le NM en a un, sa progression de collectable. Choisis le NM que tu farm et le cadre d\xC3\xA9roule toute la cha\xC3\xAene de spawn pour toujours conna\xC3\xAetre l'\xC3\xA9tape suivante."},
    {47, "", ""},

    {0, "Reading the box", "Lire le cadre"},
    {2, "The *title* names the tracked NM, and turns to *READY!* once every pop item is in hand.",
        "Le *titre* nomme le NM suivi, et passe \xC3\xA0 *READY!* d\xC3\xA8s que tous les items de pop sont en main."},
    {2, "Each *pop line* is an item or key item you need, with a *from* line under it : the placeholder mob or NPC and its map position.",
        "Chaque *ligne de pop* est un item ou key item requis, avec une ligne *from* en dessous : le mob placeholder ou PNJ et sa position sur la carte."},
    {2, "The *collectable* row, when the NM uses one, counts your progress toward its target.",
        "La ligne *collectable*, quand le NM en utilise une, compte ta progression vers son objectif."},

    {0, "Configuration", "Configuration"},
    {1, "The EmpyPop module in //aio config tunes the box, with a live preview on the right that follows your changes. It is off by default, since it only helps while farming one specific NM.",
        "Le module EmpyPop dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements. Il est d\xC3\xA9sactiv\xC3\xA9 par d\xC3\xA9""faut, car il ne sert que pour farm un NM pr\xC3\xA9""cis."},
    {2, "*Track NM* picks which empyrean NM the box follows, out of the full Abyssea list.",
        "*Suivre NM* choisit quel NM emp\xC3\xA9r\xC3\xA9""en le cadre suit, dans la liste compl\xC3\xA8te d'Abyssea."},
    {2, "*Collectable* shows or hides the collectable progress row. *Box / Frame* sets the theme and can turn the frame off. *Size* scales the whole box.",
        "*Collectable* affiche ou masque la ligne de progression. *Box / Cadre* r\xC3\xA8gle le th\xC3\xA8me et peut couper le cadre. *Taille* met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle."},
    {2, "*Text* styles the Title, Pop, From and Collectable each on its own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise le Titre, le Pop, le From et le Collectable chacun s\xC3\xA9par\xC3\xA9ment, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aperçu"},
    {1, "Open //aio config on the EmpyPop module to tune it against a sample chain, then turn Show on to track your NM live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module EmpyPop pour le r\xC3\xA9gler sur une cha\xC3\xAene d'exemple, puis active Afficher pour suivre ton NM en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_EMPYPOP_N = (int)(sizeof(HELP_EMPYPOP) / sizeof(HELP_EMPYPOP[0]));

// UPDATE : how to keep AioHud current from the Update tab (user-facing, no dev jargon).
static const HelpItem HELP_UPDATE[] = {
    {0, "Updating AioHud", "Mettre à jour AioHud"},
    {1, "AioHud updates itself, right in the game. Open the *Update* tab at the top of this window to see your installed version and whether a newer one is available.",
        "AioHud se met à jour tout seul, directement en jeu. Ouvre l'onglet *Mise à jour* en haut de cette fenêtre pour voir ta version installée et si une plus récente existe."},
    {1, "It checks automatically each time the plugin loads, so the Update tab is already up to date when you open it.",
        "Il vérifie automatiquement à chaque chargement du plugin, donc l'onglet est déjà à jour quand tu l'ouvres."},

    {0, "One click", "En un clic"},
    {1, "When an update is available, press *Update now*. AioHud downloads it and reloads itself : the HUD blinks off for a second or two, then comes back on the new version. No window, nothing to download by hand.",
        "Quand une mise à jour est dispo, appuie sur *Mettre à jour*. AioHud la télécharge et se recharge : le HUD disparaît une seconde ou deux, puis revient sur la nouvelle version. Aucune fenêtre, rien à télécharger à la main."},
    {2, "Your settings and profiles are always kept — an update never touches them.",
        "Tes réglages et profils sont toujours conservés — une mise à jour n'y touche jamais."},
    {2, "The //aioupdate command does exactly the same thing.",
        "La commande //aioupdate fait exactement la même chose."},

    {0, "Dual-boxing", "Dual-box"},
    {1, "If you run two clients, unload AioHud on the OTHER client first (//unload AioHud), then update on this one. Every client has to release the plugin before it can be swapped.",
        "Si tu lances deux clients, décharge AioHud sur l'AUTRE client d'abord (//unload AioHud), puis mets à jour sur celui-ci. Chaque client doit libérer le plugin avant qu'il puisse être remplacé."},
};
static const int HELP_UPDATE_N = (int)(sizeof(HELP_UPDATE) / sizeof(HELP_UPDATE[0]));

// One help page per module (the Help tab's left menu lists these ; add a module = add a row here).
struct HelpModule { const char* en; const char* fr; const HelpItem* items; int count; };
static const HelpModule HELP_MODULES[] = {
    { "General",          "Général",            HELP_GENERAL, HELP_GENERAL_N },
    { "Party / Alliance", "Party / Alliance",    HELP_PA,      HELP_PA_N },
    { "Target",           "Cible",              HELP_TARGET,  HELP_TARGET_N },
    { "Player",           "Joueur",             HELP_PLAYER,  HELP_PLAYER_N },
    { "Minimap",          "Minicarte",          HELP_MINIMAP, HELP_MINIMAP_N },
    { "Hate List",        "Liste d'aggro",      HELP_HATELIST, HELP_HATELIST_N },
    { "PointWatch",       "PointWatch",         HELP_POINTWATCH, HELP_POINTWATCH_N },
    { "Timers",           "Timers",             HELP_TIMERS,  HELP_TIMERS_N },
    { "Skillchains",      "Skillchains",        HELP_SKILLCHAINS, HELP_SKILLCHAINS_N },
    { "Treasure Pool",    "Pool de trésor",     HELP_TREASURE, HELP_TREASURE_N },
    { "Zone Tracker",     "Suivi de zone",      HELP_ZONETRACKER, HELP_ZONETRACKER_N },
    { "EmpyPop",          "EmpyPop",            HELP_EMPYPOP, HELP_EMPYPOP_N },
    { "Update",           "Mise à jour",        HELP_UPDATE,  HELP_UPDATE_N },
};
static const int HELP_MODULE_N = (int)(sizeof(HELP_MODULES) / sizeof(HELP_MODULES[0]));
