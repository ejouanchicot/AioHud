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
    {1, "AioHUD is a from-scratch interface for FFXI, drawn live over the game. It replaces and adds to the native windows with a full set of modules : *Party & Alliance*, *Target*, *Player*, *Pet*, *Timers*, *Skillchains*, *Hate List*, *Absorb-TP*, *Grimoire (SCH)*, *Arcade WS*, *Minimap*, *Zone Tracker*, *Treasure Pool*, *PointWatch*, *EmpyPop*, *Chat* and *Game UI*. Everything is set up from this window, and each module has its own page in this Help.",
        "AioHUD est une interface repens\xC3\xA9""e de z\xC3\xA9ro pour FFXI, dessin\xC3\xA9""e en direct par-dessus le jeu. Elle remplace et compl\xC3\xA8te les fen\xC3\xAAtres natives avec un ensemble complet de modules : *Groupe / Alliance*, *Cible*, *Joueur*, *Pet*, *Timers*, *Skillchains*, *Liste de haine*, *Absorb-TP*, *Grimoire (SCH)*, *Arcade WS*, *Minicarte*, *Zone Tracker*, *Tr\xC3\xA9sor*, *PointWatch*, *EmpyPop*, *Chat* et *Interface du jeu*. Tout se r\xC3\xA8gle depuis cette fen\xC3\xAAtre, et chaque module a sa propre page dans cette Aide."},

    {0, "The config window", "La fen\xC3\xAAtre de configuration"},
    {1, "Type //aio config to open this window. Its tabs:",
        "Tape //aio config pour ouvrir cette fen\xC3\xAAtre. Ses onglets :"},
    {2, "*Configuration* tunes how things look, with a live preview on the right.",
        "*Configuration* r\xC3\xA8gle l'apparence, avec un aper\xC3\xA7u en direct \xC3\xA0 droite."},
    {2, "*Profile* saves, loads and manages complete setups.",
        "*Profil* enregistre, charge et g\xC3\xA8re des configurations compl\xC3\xA8tes."},
    {2, "*Edit Layout* moves and resizes the boxes on screen, the same as //aio edit.",
        "*\xC3\x89""diter dispo* d\xC3\xA9place et redimensionne les cadres \xC3\xA0 l'\xC3\xA9""cran, comme //aio edit."},
    {2, "*Help* is this reference. Pick a module in the left column to read about it.",
        "*Aide* est cette r\xC3\xA9""f\xC3\xA9rence. Choisis un module dans la colonne de gauche pour le d\xC3\xA9""couvrir."},
    {2, "*Update* checks for and installs new versions, right in the game (see the Update page here).",
        "*Mise \xC3\xA0 jour* v\xC3\xA9rifie et installe les nouvelles versions, directement en jeu (voir la page Mise \xC3\xA0 jour ici)."},
    {2, "*Debug* lists the known bugs and the planned work.",
        "*Debug* liste les bugs connus et le travail pr\xC3\xA9vu."},

    {0, "Simple or Advanced", "Simple ou Avanc\xC3\xA9"},
    {1, "The *Simple / Advanced* switch, at the top right (also on the *Interface* page), sets how deep every page goes. Simple shows each module's essential settings, always open. Advanced adds the frame, the text styles and the fine sizes, and lays each page out the same way : *Display*, *Frame*, one section per part of the box, *Behaviour* and *Text*. Where a box has several faces (Party / Alliance, the pet jobs, the zones, the two chat boxes), a switch at the top picks the one you set. Nothing is lost when you switch : a setting hidden in Simple keeps its value.",
        "Le bouton *Simple / Avanc\xC3\xA9*, en haut \xC3\xA0 droite (aussi sur la page *Interface*), r\xC3\xA8gle la profondeur de chaque page. Simple montre les r\xC3\xA9glages essentiels de chaque module, toujours ouverts. Avanc\xC3\xA9 ajoute le cadre, les styles de texte et les tailles fines, et range chaque page de la m\xC3\xAAme fa\xC3\xA7on : *Affichage*, *Cadre*, une section par partie du cadre, *Comportement* et *Texte*. Quand un cadre a plusieurs visages (Groupe / Alliance, les jobs \xC3\xA0 familier, les zones, les deux box de chat), un s\xC3\xA9lecteur en haut choisit celui que tu r\xC3\xA8gles. Rien n'est perdu quand tu bascules : un r\xC3\xA9glage cach\xC3\xA9 en Simple garde sa valeur."},

    {0, "Language", "Langue"},
    {1, "The *EN / FR* button at the top right (also on the *Interface* page) switches the whole interface between English and French. Your choice is saved.",
        "Le bouton *EN / FR* en haut \xC3\xA0 droite (aussi sur la page *Interface*) bascule toute l'interface entre l'anglais et le fran\xC3\xA7""ais. Ton choix est sauvegard\xC3\xA9."},

    {0, "Profiles", "Profils"},
    {1, "A profile stores every setting under a name. Load one to switch your whole setup at once. They are handy for a per-character or per-job look.",
        "Un profil enregistre tous les r\xC3\xA9glages sous un nom. Charges-en un pour changer toute ta configuration d'un coup. Pratique pour un rendu par personnage ou par job."},
    {2, "In the *Profile* tab, type a new name and press *Create*, or type an existing name to *Overwrite* that profile. Every saved profile is a row of the list, with its kind and the date it was saved, and its keys : *Load*, *Duplicate* (a copy named \"(copy)\"), *Export* (copies it to the clipboard, to share it) and *Delete*, which asks for a second click (*Sure ?*) before it removes one.",
        "Dans l'onglet *Profil*, tape un nouveau nom et appuie sur *Cr\xC3\xA9""er*, ou tape un nom existant pour *Remplacer* ce profil. Chaque profil enregistr\xC3\xA9 est une ligne de la liste, avec son type et sa date d'enregistrement, et ses touches : *Charger*, *Dupliquer* (une copie nomm\xC3\xA9""e \"(copie)\"), *Exporter* (le copie dans le presse-papiers, pour le partager) et *Supprimer*, qui demande un second clic (*S\xC3\xBBr ?*) avant d'en effacer un."},
    {2, "*Import* takes a profile someone shared : copy their text, then press *Import* (it uses the name you typed, or the profile's own). With unsaved changes, *Discard the changes* goes back to the profile as saved.",
        "*Importer* reprend un profil partag\xC3\xA9 : copie son texte, puis appuie sur *Importer* (il prend le nom que tu as tap\xC3\xA9, ou celui du profil). Avec des modifs non enregistr\xC3\xA9""es, *Annuler les modifications* revient au profil tel qu'il est enregistr\xC3\xA9."},
    {2, "The profile bar in the header : the arrows switch profiles, and *Save changes* saves the one that is loaded. With unsaved changes, the first arrow click only warns you ; click again within 4 seconds to switch anyway.",
        "La barre de profil de l'en-t\xC3\xAAte : les fl\xC3\xA8""ches changent de profil, et *Enregistrer* sauvegarde celui qui est charg\xC3\xA9. Avec des modifs non enregistr\xC3\xA9""es, le premier clic sur une fl\xC3\xA8""che ne fait que pr\xC3\xA9venir ; reclique dans les 4 secondes pour changer quand m\xC3\xAAme."},
    {2, "Each character remembers the last profile it loaded or saved, and comes back to it when the plugin restarts. *Save for <name>* binds a profile to the current character, and *Save as Default* saves the *Default* profile, in one click.",
        "Chaque personnage m\xC3\xA9morise le dernier profil qu'il a charg\xC3\xA9 ou enregistr\xC3\xA9, et y revient au red\xC3\xA9marrage du plugin. *Sauver pour <nom>* lie un profil au personnage actuel, et *Sauver comme D\xC3\xA9""faut* enregistre le profil *D\xC3\xA9""faut*, en un clic."},

    {0, "Commands", "Commandes"},
    {2, "*//aio config* opens this window.", "*//aio config* ouvre cette fen\xC3\xAAtre."},
    {2, "*//aio edit* moves and resizes the boxes on screen.", "*//aio edit* d\xC3\xA9place et redimensionne les cadres."},
    {2, "*//aio profile* save, load, delete or list a named setup.", "*//aio profile* save, load, delete ou list un profil nomm\xC3\xA9."},
    {2, "*//aio party N* previews N members, 0 to 18 (you are always #1).", "*//aio party N* pr\xC3\xA9visualise N membres, de 0 \xC3\xA0 18 (tu es toujours le #1)."},
    {2, "7-18 auto-fills the alliances ; *//aio party off* returns to live.", "7-18 remplit les alliances automatiquement ; *//aio party off* revient au r\xC3\xA9""el."},
    {2, "*//aio out <name> <spell>* removes a watched buff line from Timers (e.g. //aio out Aeryn Haste). *//aio out list* prints each line with its number, for *//aio out <number>* -- or turn on *Tracking numbers (//aio out)* in Timers > Alerts to see them on the rows.",
        "*//aio out <nom> <sort>* retire une ligne de buff suivie dans Timers (ex. //aio out Aeryn Haste). *//aio out list* affiche chaque ligne avec son num\xC3\xA9ro, pour *//aio out <num\xC3\xA9ro>* -- ou active *Num\xC3\xA9ros de suivi (//aio out)* dans Timers > Alertes pour les voir sur les lignes."},
    {2, "*//aio out alerts* removes only the rows in red (the buffs that just ran out), leaving the live timers alone.",
        "*//aio out alertes* retire seulement les lignes en rouge (les buffs qui viennent d'expirer), sans toucher aux minuteurs en cours."},
    {2, "*//aio in* puts back every line you removed that way (a name or a number puts back just one).",
        "*//aio in* remet toutes les lignes retir\xC3\xA9""es de cette fa\xC3\xA7on (un nom ou un num\xC3\xA9ro n'en remet qu'une)."},
};
static const int HELP_GENERAL_N = (int)(sizeof(HELP_GENERAL) / sizeof(HELP_GENERAL[0]));

// INTERFACE : the config menu's own look + a few global HUD settings (config_page.cpp draw_interface_category).
static const HelpItem HELP_INTERFACE[] = {
    {0, "Interface", "Interface"},
    {1, "The *Interface* page holds the settings that belong to no single box : the font, the status icons and the key that hides the HUD, under *HUD* ; and this window's own preferences, under *Config window*.",
        "La page *Interface* regroupe les r\xC3\xA9glages qui n'appartiennent \xC3\xA0 aucun cadre en particulier : la police, les ic\xC3\xB4nes de statut et la touche qui masque le HUD, sous *HUD* ; et les pr\xC3\xA9""f\xC3\xA9rences de cette fen\xC3\xAAtre, sous *Fen\xC3\xAAtre de config*."},

    {0, "HUD", "HUD"},
    {2, "*Font (config + HUD)* is the font of this window, and also the HUD's default face. A text element given its own font in a module's *Text* section keeps it.",
        "*Police (config + HUD)* est la police de cette fen\xC3\xAAtre, et aussi la police par d\xC3\xA9""faut du HUD. Un \xC3\xA9l\xC3\xA9ment de texte qui a sa propre police dans la section *Texte* d'un module la garde."},
    {2, "*Status icons* picks the icon pack every buff and debuff icon is drawn from, in the party, player, target, Timers and debuff boxes. *Auto* is the default and picks the best pack found.",
        "*Ic\xC3\xB4nes de statut* choisit le pack d'ic\xC3\xB4nes d'o\xC3\xB9 viennent toutes les ic\xC3\xB4nes de buff et de debuff, dans les cadres groupe, joueur, cible, Timers et debuffs. *Auto* est le d\xC3\xA9""faut et prend le meilleur pack trouv\xC3\xA9."},
    {2, "*Hide key (End)* hides the whole HUD : *Toggle* hides it with one press and shows it with the next, *Hold* hides it only while the key is held.",
        "*Touche masquer (Fin)* cache tout le HUD : *Bascule* le cache d'un appui et le remontre au suivant, *Maintien* le cache seulement tant que la touche est enfonc\xC3\xA9""e."},

    {0, "Config window", "Fen\xC3\xAAtre de config"},
    {1, "These are this window's own preferences. They apply and are saved at once, for every profile : no *Save changes* needed, and loading a profile keeps them.",
        "Ce sont les pr\xC3\xA9""f\xC3\xA9rences de cette fen\xC3\xAAtre. Elles s'appliquent et s'enregistrent tout de suite, pour tous les profils : pas besoin d'*Enregistrer*, et charger un profil les garde."},
    {2, "*Size* scales this window, from 80 to 140 percent.",
        "*Taille* met cette fen\xC3\xAAtre \xC3\xA0 l'\xC3\xA9""chelle, de 80 \xC3\xA0 140 pour cent."},
    {2, "*Language* and *Settings shown* (Simple or Advanced) are the same as the *EN / FR* and *Simple / Advanced* buttons at the top right.",
        "*Langue* et *R\xC3\xA9glages affich\xC3\xA9s* (Simple ou Avanc\xC3\xA9) sont les m\xC3\xAAmes que les boutons *EN / FR* et *Simple / Avanc\xC3\xA9* en haut \xC3\xA0 droite."},
    {2, "*Background* is *Opaque*, *Dark* or *See-through*, to keep an eye on the game behind the window.",
        "*Fond* est *Opaque*, *Sombre* ou *Translucide*, pour garder un oeil sur le jeu derri\xC3\xA8re la fen\xC3\xAAtre."},
    {2, "*Reopen on the last page* opens the window where you left it, instead of on its first page.",
        "*Rouvrir sur la derni\xC3\xA8re page* rouvre la fen\xC3\xAAtre l\xC3\xA0 o\xC3\xB9 tu l'as laiss\xC3\xA9""e, au lieu de sa premi\xC3\xA8re page."},
};
static const int HELP_INTERFACE_N = (int)(sizeof(HELP_INTERFACE) / sizeof(HELP_INTERFACE[0]));

// PARTY / ALLIANCE : only what concerns the party + alliance boxes.
static const HelpItem HELP_PA[] = {
    {0, "Party and Alliance", "Groupe et Alliance"},
    {1, "AioHUD replaces the game's *Party* and *Alliance* windows with up to three boxes, your *Party* plus *Alliance 1* and *Alliance 2*. Each one shows its members with live HP, MP and TP, their job, the leader marks, the distance, and for your own *Party* their status effects. It all moves in real time, and whoever you are targeting stays lit up.",
        "AioHUD remplace les fen\xC3\xAAtres *Party* et *Alliance* du jeu par un maximum de trois cadres, ta *Party* plus l'*Alliance 1* et l'*Alliance 2*. Chacun montre ses membres avec leurs HP, MP et TP en direct, leur job, les marques de leader, la distance, et pour ta propre *Party* leurs effets de statut. Tout bouge en temps r\xC3\xA9""el, et la personne que tu cibles reste allum\xC3\xA9""e."},

    {0, "Reading a member", "Lire un membre"},
    {1, "A member's row reads from left to right, starting with the leader dots and the distance, then the job badge, the name, and finally the three gauges.",
        "La ligne d'un membre se lit de gauche \xC3\xA0 droite, en commen\xC3\xA7""ant par les pastilles de leader et la distance, puis le badge de job, le nom, et enfin les trois jauges."},
    {2, "The *name* sits in a clean off-white. It turns *red* when the member is KO, and greys out when they are in another zone.",
        "Le *nom* est en blanc cass\xC3\xA9. Il passe au *rouge* quand le membre est KO, et grise quand il est dans une autre zone."},
    {2, "The *job badge* carries the main job on top and the sub below, WAR over NIN for example. It is tinted by the member's *role*, tank, healer, damage or support, so you read the make-up of the group at a glance.",
        "Le *badge de job* porte le job principal en haut et le sous-job en dessous, WAR sur NIN par exemple. Il est teint\xC3\xA9 selon le *r\xC3\xB4le* du membre, tank, healer, damage ou support, pour lire la composition du groupe d'un coup d'oeil."},
    {2, "*HP* runs from green when it is high down through yellow and orange to red, and it blinks alarm-red once the member drops to a quarter or less (turn *Low HP blink* off to stop the blink).",
        "Les *HP* vont du vert quand elles sont hautes jusqu'au rouge en passant par le jaune et l'orange, et clignotent en rouge d'alarme d\xC3\xA8s que le membre tombe \xC3\xA0 un quart ou moins (coupe *Clignote si PV bas* pour arr\xC3\xAAter le clignotement)."},
    {12, "the HP colour, live, blinking at the critical quarter", "la couleur des HP en direct, clignotant au quart critique"},
    {2, "*MP* is blue. *TP* fills as it builds and lights up at 1000, which means a weapon skill is ready (*TP ready pulse* turns that glow off).",
        "Les *MP* sont bleus. Les *TP* se remplissent et s'illuminent \xC3\xA0 1000, ce qui veut dire qu'une weapon skill est pr\xC3\xAAte (*Pulse quand TP pr\xC3\xAAt* coupe cette lueur)."},
    {13, "the TP gauge, live, glowing past 1000", "la jauge de TP en direct, brillant au-del\xC3\xA0 de 1000"},
    {2, "*Buff icons* for your own *Party*, on the left (up to 32, wrapping to two rows of 16). The game never sends *Alliance* buffs, so alliance members show none.",
        "Des *ic\xC3\xB4nes de buffs* pour ta propre *Party*, \xC3\xA0 gauche (jusqu'\xC3\xA0 32, sur deux lignes de 16). Le jeu n'envoie jamais les buffs d'*Alliance*, donc les membres d'alliance n'en ont pas."},

    {0, "Leader and Quartermaster dots", "Pastilles Party Leader, Alliance Leader et Quartermaster"},
    {1, "Three little dots sit at the top of the left column, each in its own fixed slot so they never shuffle around. A slot stays empty when the member does not hold that role, and the dots fade in and out as roles change.",
        "Trois petites pastilles en haut de la colonne de gauche, chacune dans un emplacement fixe pour ne jamais se m\xC3\xA9langer. Un emplacement reste vide si le membre n'a pas ce r\xC3\xB4le, et les pastilles apparaissent et disparaissent quand les r\xC3\xB4les changent."},
    {11, "", ""},

    {0, "Distance", "Distance"},
    {1, "Under the dots, each member shows how far they are from you in yalms, written as 00.00. The colour tells you at a glance whether your spells can reach them.",
        "Sous les pastilles, chaque membre montre \xC3\xA0 quelle distance il est de toi en yalms, au format 00.00. La couleur te dit d'un coup d'oeil si tes sorts peuvent l'atteindre."},
    {10, "under 10 blue, up to 20.8 yellow, beyond that red", "moins de 10 bleu, jusqu'\xC3\xA0 20.8 jaune, au-del\xC3\xA0 rouge"},
    {2, "*Blue* under 10 yalms, in range of everything, cures and the short spells like the AoE -ra line or Majesty.",
        "*Bleu* sous 10 yalms, \xC3\xA0 port\xC3\xA9""e de tout, cures et sorts courts comme les -ra de zone ou Majesty."},
    {2, "*Yellow* from 10 up to 20.8, still in normal casting range but too far for those short spells.",
        "*Jaune* de 10 \xC3\xA0 20.8, encore dans la port\xC3\xA9""e de cast normale mais trop loin pour ces sorts courts."},
    {2, "*Red* at 20.8 and beyond, out of range. A normal cure or buff will fail, and the whole row dims.",
        "*Rouge* \xC3\xA0 20.8 et au-del\xC3\xA0, hors de port\xC3\xA9""e. Une cure ou un buff normal \xC3\xA9""chouera, et toute la ligne s'assombrit."},
    {1, "You, your trusts, and anyone in another zone show no distance.",
        "Toi, tes trusts et quiconque dans une autre zone n'affichent pas de distance."},

    {0, "Target cursor", "Curseur de cible"},
    {1, "When you target a *Party* or *Alliance* member, a hand appears at the left of their row pointing at them and a soft highlight slides onto it. The cursor follows your target from one member to the next, bobbing gently so it is always easy to spot.",
        "Quand tu cibles un membre de la *Party* ou de l'*Alliance*, une main appara\xC3\xAEt \xC3\xA0 gauche de sa ligne en le pointant et une surbrillance douce glisse dessus. Le curseur suit ta cible de membre en membre, en oscillant l\xC3\xA9g\xC3\xA8rement pour rester bien visible."},
    {1, "When you lock onto your target, the hand and its highlight both turn *red* instead of gold, so a glance tells you the lock is on. Take the lock off and they go back to gold.",
        "Quand tu te verrouilles sur ta cible, la main et sa surbrillance passent au *rouge* au lieu de l'or, pour voir d'un coup d'oeil que le verrou est actif. Retire le verrou et elles redeviennent or."},
    {14, "", ""},
    {1, "The game's own party menu, like Quartermaster or Lottery, also lights up the member it points at, so the box always matches the menu on screen.",
        "Le menu de groupe du jeu, comme Quartermaster ou Lottery, allume aussi le membre qu'il pointe, donc le cadre correspond toujours au menu \xC3\xA0 l'\xC3\xA9""cran."},

    {0, "Selection highlight", "Surbrillance de s\xC3\xA9lection"},
    {1, "The highlight is a faint tinted bar with a slow glass sheen sweeping across it, bright enough to frame the member but never enough to hide the gauges. The main target's bar is *gold*, and a sub-target's bar is ocean *blue* and sits on top.",
        "La surbrillance est une barre teint\xC3\xA9""e discr\xC3\xA8te avec un reflet de verre qui la balaie lentement, assez visible pour encadrer le membre mais jamais au point de masquer les jauges. La barre de la cible principale est *or*, et celle d'une sous-cible est *bleu* oc\xC3\xA9""an et passe au-dessus."},

    {0, "Casting and out of range", "Incantation et hors de port\xC3\xA9""e"},
    {1, "While a member casts, the spell name shows in *gold* just under their name, breathing softly. A member beyond casting range sits under a dark veil over their row, a clear can't-reach-them cue. A member who has moved to another zone shows that zone's name in place of the gauges.",
        "Quand un membre lance un sort, le nom du sort s'affiche en *or* juste sous son nom, en pulsant doucement. Un membre hors de port\xC3\xA9""e de cast est recouvert d'un voile sombre sur sa ligne, un signal clair qu'on ne peut pas l'atteindre. Un membre parti dans une autre zone montre le nom de cette zone \xC3\xA0 la place des jauges."},

    {0, "Adaptive layout", "Disposition adaptative"},
    {1, "The *Party* box is anchored by its lower-right corner. It grows upward as members join and shrinks back as they leave, but the corner you placed never moves. The game's own party and alliance windows are hidden while AioHUD's boxes are shown, so nothing of them shows behind.",
        "Le cadre *Party* est ancr\xC3\xA9 par son coin bas-droit. Il grandit vers le haut quand des membres arrivent et se r\xC3\xA9""duit quand ils partent, mais le coin que tu as plac\xC3\xA9 ne bouge jamais. Les fen\xC3\xAAtres de groupe et d'alliance du jeu sont cach\xC3\xA9""es tant que les cadres d'AioHUD sont affich\xC3\xA9s, donc rien d'elles ne d\xC3\xA9passe derri\xC3\xA8re."},

    {0, "Cost and Next box", "Cadre Co\xC3\xBBt et Next"},
    {1, "When you open a spell, ability or weapon-skill menu, a small box appears just above the *Party*. It shows the action's name with, depending on the action, its MP cost for spells, its *Next* recast timer for spells and abilities, or your live TP for weapon skills. *Alliance 1* stacks flush on top of this box.",
        "Quand tu ouvres un menu de sort, d'aptitude ou de weapon skill, un petit cadre appara\xC3\xAEt juste au-dessus de la *Party*. Il montre le nom de l'action avec, selon le cas, son co\xC3\xBBt en MP pour les sorts, son d\xC3\xA9lai *Next* pour les sorts et aptitudes, ou tes TP en direct pour les weapon skills. L'*Alliance 1* se cale juste au-dessus de ce cadre."},

    {0, "Configuration", "Configuration"},
    {1, "The Configuration tab sets how the boxes look, with a live preview on the right that follows your changes.",
        "L'onglet Configuration r\xC3\xA8gle l'apparence des cadres, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "In Simple the page has three cards. *Party* : *Show*, *Size*, *Gauge style*, *Low HP blink*, *TP ready pulse*, *Bar height* and *Bar width*. *Alliance* : *Show* and *Size* (50 to 200) for the two alliance boxes. *Buffs* : the *Buffs side* and the order of the buff groups.",
        "En Simple la page a trois cartes. *Groupe* : *Afficher*, *Taille*, *Style de jauge*, *Clignote si PV bas*, *Pulse quand TP pr\xC3\xAAt*, *Hauteur des barres* et *Largeur des barres*. *Alliance* : *Afficher* et *Taille* (de 50 \xC3\xA0 200) pour les deux cadres d'alliance. *Buffs* : le *C\xC3\xB4t\xC3\xA9 des buffs* et l'ordre des groupes de buffs."},
    {2, "In Advanced, the *Party | Alliance* switch at the top picks the box you set : the sections below follow it, so no option is on the page twice. *Display* : *Show*, *Size* (50 to 200 percent), *Gauge style* (*Vial*, *Bars*, *Segments*, *Minimal*, *Sphere*, *Ring*, *Crystal* or *Text*), *Bar height* (not for the round gauges) and *Bar width*. The box grows to fit.",
        "En Avanc\xC3\xA9, le s\xC3\xA9lecteur *Groupe | Alliance* en haut choisit le cadre que tu r\xC3\xA8gles : les sections en dessous le suivent, aucune option n'est deux fois sur la page. *Affichage* : *Afficher*, *Taille* (de 50 \xC3\xA0 200 pour cent), *Style de jauge* (*Fiole*, *Barres*, *Segments*, *Minimal*, *Sph\xC3\xA8re*, *Anneau*, *Cristal* ou *Texte*), *Hauteur des barres* (pas pour les jauges rondes) et *Largeur des barres*. Le cadre s'agrandit en cons\xC3\xA9quence."},
    {2, "*Frame* : on *Party*, the *Border*, the *Box transparency*, the *Box theme* with its *Tint* or *Custom colour*, the *Luminosity* and the *Cost box border*. The background can stay while the border goes. On *Alliance*, one *Same as Party* follows the Party's frame (transparency and theme) ; turn it off to give the alliances their own.",
        "*Cadre* : en *Groupe*, la *Bordure*, la *Transparence du cadre*, le *Th\xC3\xA8me de cadre* avec sa *Teinte* ou sa *Couleur perso*, la *Luminosit\xC3\xA9* et la *Bordure de la bo\xC3\xAEte co\xC3\xBBt*. Le fond peut rester quand la bordure part. En *Alliance*, un seul *Comme le Groupe* suit le cadre du Groupe (transparence et th\xC3\xA8me) ; coupe-le pour donner aux alliances le leur."},
    {2, "*Rows* : the *Job badge* (Off, Main job, Main + Sub or Icons) and its *Badge size*, *Casts on the row* and *Distance on the row*.",
        "*Lignes* : le *Badge de job* (Aucun, Job principal, Principal + Sub ou Ic\xC3\xB4nes) et sa *Taille du badge*, *Sorts sur la ligne* et *Distance sur la ligne*."},
    {2, "*Buffs* (Party only) : *Buff size* (as a share of the row height), *Buffs side* (*Auto* puts them on the side where there is room), *Max buffs* (None, 16, 20, 24 or 32) and *Buff lines* (1 or 2 lines).",
        "*Buffs* (Groupe seulement) : *Taille des buffs* (en proportion de la hauteur de ligne), *C\xC3\xB4t\xC3\xA9 des buffs* (*Auto* les met du c\xC3\xB4t\xC3\xA9 o\xC3\xB9 il y a la place), *Buffs max* (Aucun, 16, 20, 24 ou 32) et *Lignes de buffs* (1 ou 2 lignes)."},
    {2, "The *buff strip editor* under *Buffs* orders the groups and turns them off. Drag a block to move it -- the rightmost block sits against the member row -- and double-click a group to open it and order its buffs. A group can be *Shown* or *Hidden* ; a hidden one costs no width, so hiding a group really does buy room for the rest.",
        "L'*\xC3\xA9""diteur de bandeau de buffs* sous *Buffs* ordonne les groupes et les d\xC3\xA9sactive. Glisse un bloc pour le d\xC3\xA9placer -- le bloc le plus \xC3\xA0 droite est contre la ligne du membre -- et double-clique sur un groupe pour l'ouvrir et ordonner ses buffs. Un groupe peut \xC3\xAAtre *Affich\xC3\xA9* ou *Masqu\xC3\xA9* ; un groupe masqu\xC3\xA9 ne prend aucune largeur, donc le masquer lib\xC3\xA8re vraiment de la place pour les autres."},
    {2, "*Text* (Advanced) styles each text element of the box the switch picks, Name, Job badge, Cast, Distance and, on Party, Cost box, with its font, size, outline, bold / italic / caps and colour. On Alliance, *Copy the Party's text* starts it from the Party's in one click.",
        "*Texte* (Avanc\xC3\xA9) stylise chaque \xC3\xA9l\xC3\xA9ment de texte du cadre choisi par le s\xC3\xA9lecteur, Nom, Badge de job, Sort, Distance et, en Groupe, Bo\xC3\xAEte co\xC3\xBBt, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur. En Alliance, *Copier le texte du Groupe* le reprend de celui du Groupe en un clic."},
    {2, "*Behaviour (both boxes)* (Advanced) is the same for Party and Alliance, whatever the switch : *Low HP blink*, *TP ready pulse*, *Cursor size*, *Cursor style*, and the *Distance colours* : three plates, Close, Normal and Far ; click one to open its colour picker.",
        "*Comportement (les deux bo\xC3\xAEtes)* (Avanc\xC3\xA9) vaut pour le Groupe et l'Alliance, quel que soit le s\xC3\xA9lecteur : *Clignote si PV bas*, *Pulse quand TP pr\xC3\xAAt*, *Taille du curseur*, *Style du curseur*, et les *Couleurs de distance* : trois plaques, Proche, Normal et Loin ; clique sur l'une pour ouvrir son choix de couleur."},
    {1, "Whenever a box is resized its lower-right corner stays put, so it grows up and to the left from where you placed it and nothing drifts.",
        "Quand un cadre est redimensionn\xC3\xA9 son coin bas-droit reste en place, il grandit vers le haut et la gauche depuis o\xC3\xB9 tu l'as pos\xC3\xA9 et rien ne d\xC3\xA9rive."},

    {0, "Move and resize", "D\xC3\xA9placer et redimensionner"},
    {1, "Type //aio edit, or click *Edit Layout*, to arrange the boxes right on the game screen. Every box shows its outline ; hover one to read its name, click it to select it. Drag a box to move it, and roll the wheel over it to resize, the *Party* from 100 to 200 and the alliances from 50 to 200. Boxes snap to each other's edges as you drag, and a brass guide shows the edge they line up on.",
        "Tape //aio edit, ou clique *\xC3\x89""diter dispo*, pour disposer les cadres directement sur l'\xC3\xA9""cran. Chaque cadre montre son contour ; survole-en un pour lire son nom, clique dessus pour le s\xC3\xA9lectionner. Glisse un cadre pour le d\xC3\xA9placer, et utilise la molette dessus pour redimensionner, la *Party* de 100 \xC3\xA0 200 et les alliances de 50 \xC3\xA0 200. Les cadres s'aimantent \xC3\xA0 leurs bords pendant le glissement, et un guide dor\xC3\xA9 montre le bord sur lequel ils s'alignent."},
    {2, "The toolbar at the top shows the selected box, its *x* and *y* in steppers (1 pixel a click, 10 with Shift ; click the number to type it) and its size. The *Party* and *Alliance* boxes move with the mouse only.",
        "La barre d'outils en haut montre le cadre s\xC3\xA9lectionn\xC3\xA9, son *x* et son *y* en compteurs (1 pixel par clic, 10 avec Maj ; clique sur le nombre pour le taper) et sa taille. Les cadres *Party* et *Alliance* se d\xC3\xA9placent \xC3\xA0 la souris seulement."},
    {2, "*Undo* and *Redo* step back and forth through your moves (Ctrl+Z / Ctrl+Y). *Default* resets every position and size, after a confirmation, and *Done* saves and leaves edit mode.",
        "*Annuler* et *R\xC3\xA9tablir* reviennent en arri\xC3\xA8re ou en avant dans tes d\xC3\xA9placements (Ctrl+Z / Ctrl+Y). *D\xC3\xA9""faut* r\xC3\xA9initialise toutes les positions et tailles, apr\xC3\xA8s confirmation, et *Termin\xC3\xA9* sauvegarde et quitte."},
    {2, "While you drag, Shift keeps the move horizontal, Ctrl vertical, and Alt moves freely without snapping. Delete puts the selected box back to its default place, Escape deselects it.",
        "Pendant le glissement, Maj garde le d\xC3\xA9placement horizontal, Ctrl vertical, et Alt d\xC3\xA9place librement sans aimant. Suppr remet le cadre s\xC3\xA9lectionn\xC3\xA9 \xC3\xA0 sa place par d\xC3\xA9""faut, \xC3\x89""chap le d\xC3\xA9s\xC3\xA9lectionne."},

    {0, "Zones", "Zones"},
    {1, "Zones are rectangles over the screen that say where each box may sit. The ones you already have stay active : in edit mode they show faintly in colour, and a box dragged onto a zone that forbids it is pushed back out. The new toolbar has no zone editor. Everything is stored as a share of the screen, so a saved layout scales to any resolution.",
        "Les zones sont des rectangles sur l'\xC3\xA9""cran qui disent o\xC3\xB9 chaque cadre peut se poser. Celles que tu as d\xC3\xA9j\xC3\xA0 restent actives : en mode \xC3\xA9""dition elles s'affichent en couleur, discr\xC3\xA8tement, et un cadre gliss\xC3\xA9 sur une zone qui l'interdit en est repouss\xC3\xA9. La nouvelle barre d'outils n'a pas d'\xC3\xA9""diteur de zones. Tout est stock\xC3\xA9 en proportion de l'\xC3\xA9""cran, donc une disposition sauv\xC3\xA9""e s'adapte \xC3\xA0 toute r\xC3\xA9solution."},

    {0, "Preview and demo", "Aper\xC3\xA7u et d\xC3\xA9mo"},
    {1, "Use //aio party N to fill the boxes with N fake members, 0 to 18 - you are always member #1. 1 to 6 is a single *Party*; 7 to 18 auto-creates the alliances needed (7-12 adds alliance 1, 13-18 adds alliance 2), so no separate command. In demo a target cursor cycles through the members so you watch the highlight move. //aio party off returns to live data. Demo mirrors the real layout, so you can tune spacing and placement with no live party.",
        "Utilise //aio party N pour remplir les cadres avec N faux membres, de 0 \xC3\xA0 18 - tu es toujours le membre #1. De 1 \xC3\xA0 6 c'est une seule *Party*; de 7 \xC3\xA0 18 les alliances n\xC3\xA9""cessaires sont cr\xC3\xA9\xC3\xA9""es automatiquement (7-12 ajoute l'alliance 1, 13-18 l'alliance 2), sans commande s\xC3\xA9par\xC3\xA9""e. En d\xC3\xA9mo un curseur de cible d\xC3\xA9""file sur les membres pour voir bouger la surbrillance. //aio party off revient aux donn\xC3\xA9""es r\xC3\xA9""elles. La d\xC3\xA9mo reproduit la vraie disposition, pour r\xC3\xA9gler l'espacement et le placement sans groupe r\xC3\xA9""el."},
};
static const int HELP_PA_N = (int)(sizeof(HELP_PA) / sizeof(HELP_PA[0]));

// TARGET : everything the Target box shows (name, HP + damage trail, distance/range, debuffs, speed, TH)
// and how to configure it. Text-only (kinds 0/1/2), same style as HELP_PA.
static const HelpItem HELP_TARGET[] = {
    {0, "Target", "Cible"},
    {1, "The *Target* box is a compact panel that appears whenever you have something targeted, a player, an ally, a monster or an NPC. It shows the target's name, its HP as a bar and a percentage, how far it is on a colour-coded range bar, its status effects with timers, and for a living foe its movement speed and Treasure Hunter. It fades in when you target and slips away when you drop the target.",
        "Le cadre *Cible* est un panneau compact qui appara\xC3\xAEt d\xC3\xA8s que tu as quelque chose de cibl\xC3\xA9, un joueur, un alli\xC3\xA9, un monstre ou un PNJ. Il montre le nom de la cible, ses HP en barre et en pourcentage, sa distance sur une barre de port\xC3\xA9""e color\xC3\xA9""e, ses effets de statut avec leurs minuteurs, et pour un ennemi vivant sa vitesse de d\xC3\xA9placement et son Treasure Hunter. Il appara\xC3\xAEt en fondu quand tu cibles et s'efface quand tu rel\xC3\xA2""ches la cible."},

    {0, "Reading the target", "Lire la cible"},
    {2, "The *name* sits at the top and stays readable over any box theme.",
        "Le *nom* est en haut et reste lisible sur n'importe quel th\xC3\xA8me de cadre."},
    {2, "The *HP %* number tells you exactly how healthy the target is, even when the bar is almost full or almost empty.",
        "Le *pourcentage de HP* te dit exactement l'\xC3\xA9tat de la cible, m\xC3\xAAme quand la barre est presque pleine ou presque vide."},
    {2, "The *HP bar* fills with a glass sheen and shifts from green through yellow and orange to red as the target loses health, the same liquid look as the party gauges.",
        "La *barre de HP* se remplit avec un reflet de verre et passe du vert au rouge en passant par le jaune et l'orange \xC3\xA0 mesure que la cible perd ses HP, le m\xC3\xAAme rendu liquide que les jauges de groupe."},

    {0, "The damage trail", "La tra\xC3\xAEn\xC3\xA9""e de d\xC3\xA9g\xC3\xA2ts"},
    {1, "When the target takes a hit, the coloured bar snaps down at once while a brighter *orange* slice holds for a moment behind it, then drains to catch up. That lingering orange is the health you just took off, so a big hit reads as a satisfying chunk, the way a fighting-game health bar does.",
        "Quand la cible encaisse un coup, la barre color\xC3\xA9""e chute d'un coup tandis qu'une tranche plus vive, *orange*, reste un instant derri\xC3\xA8re, puis se vide pour rattraper. Cet orange qui s'attarde, c'est les HP que tu viens d'enlever, si bien qu'un gros coup se lit comme une belle entaille, \xC3\xA0 la mani\xC3\xA8re d'une barre de vie de jeu de combat."},
    {20, "a hit drops the bar, the orange damage lingers then drains", "un coup fait chuter la barre, les d\xC3\xA9g\xC3\xA2ts orange s'attardent puis se vident"},

    {0, "Distance and range", "Distance et port\xC3\xA9""e"},
    {1, "Below the HP, a range bar shows how far the target is and which of your actions can reach it. A cursor marks the target's exact distance along the bar, and the number in the centre reads it out in yalms. It works for allies and NPCs just like for monsters.",
        "Sous les HP, une barre de port\xC3\xA9""e montre \xC3\xA0 quelle distance est la cible et quelles actions peuvent l'atteindre. Un curseur marque la distance exacte de la cible le long de la barre, et le nombre au centre l'indique en yalms. Elle fonctionne pour les alli\xC3\xA9s et les PNJ comme pour les monstres."},
    {2, "Against a *monster*, the zones are your combat ranges, *Melee*, *WS*, *Magic*, *Ranged* and *Enmity*, scaled to the creature's size, so you see at a glance whether you are close enough to swing, weapon-skill or cast.",
        "Contre un *monstre*, les zones sont tes port\xC3\xA9""es de combat, *Melee*, *WS*, *Magic*, *Ranged* et *Enmity*, mises \xC3\xA0 l'\xC3\xA9""chelle de la taille de la cr\xC3\xA9""ature, pour voir d'un coup d'oeil si tu es assez pr\xC3\xA8s pour frapper, faire une weapon skill ou lancer un sort."},
    {21, "vs a monster : Melee, WS, Magic, Ranged, Enmity", "contre un monstre : Melee, WS, Magic, Ranged, Enmity"},
    {2, "On a *player* or an *NPC*, the zones are the support ranges, *Trade*, *AoE* and *Cast*, so you know when you can trade, land an area buff or cure them.",
        "Sur un *joueur* ou un *PNJ*, les zones sont les port\xC3\xA9""es de soutien, *Trade*, *AoE* et *Cast*, pour savoir quand tu peux \xC3\xA9""changer, poser un buff de zone ou le soigner."},
    {22, "vs a player or NPC : Trade, AoE, Cast", "contre un joueur ou un PNJ : Trade, AoE, Cast"},
    {2, "Past the last zone the cursor moves into *Out*, the target is beyond reach.",
        "Au-del\xC3\xA0 de la derni\xC3\xA8re zone le curseur passe dans *Out*, la cible est hors de port\xC3\xA9""e."},

    {0, "Movement speed", "Vitesse de d\xC3\xA9placement"},
    {1, "For a player or a monster the box shows how fast the target moves compared to normal, written +18% or -12%. The colour reads it for you, and it means the opposite thing for a friend and a foe.",
        "Pour un joueur ou un monstre le cadre montre \xC3\xA0 quelle vitesse la cible se d\xC3\xA9place par rapport \xC3\xA0 la normale, \xC3\xA9""crit +18% ou -12%. La couleur l'interpr\xC3\xA8te pour toi, et elle veut dire l'inverse pour un alli\xC3\xA9 et un ennemi."},
    {2, "On a *player*, *green* is faster than normal (feet, haste, good) and *red* is slower (bind, weight, bad).",
        "Sur un *joueur*, le *vert* est plus rapide que la normale (feet, haste, c'est bon) et le *rouge* plus lent (bind, poids, c'est mauvais)."},
    {2, "On a *monster* it flips, *red* is faster (a hasted or fleeing mob, watch out) and *green* is slower (it is snared).",
        "Sur un *monstre* \xC3\xA7""a s'inverse, le *rouge* est plus rapide (un mob h\xC3\xA2t\xC3\xA9 ou qui fuit, attention) et le *vert* plus lent (il est ralenti)."},
    {23, "same +%, green on a player, red on a monster", "le m\xC3\xAAme +%, vert sur un joueur, rouge sur un monstre"},
    {1, "A plain NPC that is neither a player nor a monster shows no speed.",
        "Un PNJ neutre qui n'est ni joueur ni monstre n'affiche pas de vitesse."},

    {0, "Treasure Hunter", "Treasure Hunter"},
    {1, "Once you have hit a monster, a small treasure-chest mark carries a number, its current *Treasure Hunter* tier, so you know how far your TH has climbed before the kill. It only appears on a foe you are fighting.",
        "Une fois que tu as frapp\xC3\xA9 un monstre, une petite marque de coffre porte un chiffre, son niveau de *Treasure Hunter* actuel, pour savoir jusqu'o\xC3\xB9 ton TH a grimp\xC3\xA9 avant le kill. Elle n'appara\xC3\xAEt que sur un ennemi que tu combats."},
    {24, "the coffer, cycling its Treasure Hunter tier", "le coffre, avec son niveau de Treasure Hunter"},

    {0, "Status effects", "Effets de statut"},
    {1, "The target's debuffs show as icons, each with its own countdown timer, so you track your Slow, Paralyze, Dia or Poison at a glance. You choose how many appear and where they sit around the box.",
        "Les debuffs de la cible s'affichent en ic\xC3\xB4nes, chacune avec son propre minuteur d\xC3\xA9""compt\xC3\xA9, pour suivre tes Slow, Paralyze, Dia ou Poison d'un coup d'oeil. Tu choisis combien apparaissent et o\xC3\xB9 ils se placent autour du cadre."},
    {25, "live debuff icons with their timers", "les ic\xC3\xB4nes de debuff en direct avec leurs minuteurs"},

    {0, "Configuration", "Configuration"},
    {1, "In Simple the Target page shows two cards, *Display* (*Show*, *Size*) and *What it shows* (*Distance to the target*, *The spell it casts*, *Its debuffs* and *Debuffs shown* -- in the target box or in their own box --, *Its speed (slowed / hasted)*, *Treasure Hunter*). Advanced splits it into *Display*, *Frame*, *Bars*, *Markers*, *Spell and sub-target*, *Debuffs* and *Text*, with a live preview on the right that follows your changes.",
        "En Simple la page Cible montre deux cartes, *Affichage* (*Afficher*, *Taille*) et *Ce qu'elle affiche* (*Distance de la cible*, *Le sort qu'elle lance*, *Ses debuffs* et *Debuffs affich\xC3\xA9s* -- dans la box cible ou dans leur propre box --, *Sa vitesse (ralentie / acc\xC3\xA9l\xC3\xA9r\xC3\xA9""e)*, *Treasure Hunter*). Avanc\xC3\xA9 la d\xC3\xA9""coupe en *Affichage*, *Cadre*, *Barres*, *Indicateurs*, *Sort et sous-cible*, *Debuffs* et *Texte*, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* (Advanced) adds the *Name colour* (By hostility or Fixed). *Frame* : *Box*, *Border*, *Transparency* and *Theme* (each *Same as Party* or *Custom*), the *Box transparency*, the *Box theme* with its *Tint* or *Custom colour*, and the *Luminosity*.",
        "*Affichage* (Avanc\xC3\xA9) ajoute la *Couleur du nom* (Par hostilit\xC3\xA9 ou Fixe). *Cadre* : *Cadre*, *Bordure*, *Transparence* et *Th\xC3\xA8me* (chacun *Comme Party* ou *Perso*), la *Transparence du cadre*, le *Th\xC3\xA8me de cadre* avec sa *Teinte* ou sa *Couleur perso*, et la *Luminosit\xC3\xA9*."},
    {2, "*Bars* sets the *Bar height* and *Bar width* of the HP bar.",
        "*Barres* r\xC3\xA8gle la *Hauteur de la barre* et la *Largeur de la barre* de HP."},
    {2, "*Markers* : one choice each for *Distance* (Off, Gauge or Minimal, with the *Distance gauge height*), *Speed (slowed / hasted)* and *Treasure Hunter* (Off, Text or Icon), and a shared *Speed / TH icon size*.",
        "*Indicateurs* : un seul choix chacun pour la *Distance* (D\xC3\xA9sactiv\xC3\xA9, Jauge ou Minimal, avec la *Hauteur de la jauge distance*), la *Vitesse (ralentie / acc\xC3\xA9l\xC3\xA9r\xC3\xA9""e)* et le *Treasure Hunter* (D\xC3\xA9sactiv\xC3\xA9, Texte ou Ic\xC3\xB4ne), et une *Taille des ic\xC3\xB4nes vitesse / TH* commune."},
    {2, "*Spell and sub-target* : *The spell it casts*, a *Sample spell (to place it)*, and the *Sub-target box (<st>)* : Off, or where it sits (Above-Right, Above-Left, Below-Right, Below-Left, Right or Left).",
        "*Sort et sous-cible* : *Le sort qu'elle lance*, un *Sort d'exemple (pour le placer)*, et la *Box de sous-cible (<st>)* : D\xC3\xA9sactiv\xC3\xA9, ou sa place (Haut-Droite, Haut-Gauche, Bas-Droite, Bas-Gauche, Droite ou Gauche)."},
    {2, "*Debuffs* : *Placement* is Off, *In the target box* or *In their own box*. In the target box : *Max debuffs*, the *Debuffs position* (Inside, Below, Above, Left or Right ; Left and Right split the icons on both sides), the *Debuff icon size* and the *Time left under each*. In their own box : *Size*, *Grows toward*, *Max rows*, the *Header (mob name)*, what the *Rows show* (Icon, Name or Icon + name), the *Icon size* and the *Row spacing* ; that box then gets its own *Debuff box frame* section.",
        "*Debuffs* : l'*Emplacement* est D\xC3\xA9sactiv\xC3\xA9, *Dans la cible* ou *Bo\xC3\xAEte s\xC3\xA9par\xC3\xA9""e*. Dans la cible : *Debuffs max*, la *Position des debuffs* (Dans la box, Dessous, Dessus, Gauche ou Droite ; Gauche et Droite r\xC3\xA9partissent les ic\xC3\xB4nes des deux c\xC3\xB4t\xC3\xA9s), la *Taille des ic\xC3\xB4nes* et le *Temps restant sous chacun*. En bo\xC3\xAEte s\xC3\xA9par\xC3\xA9""e : *Taille*, *Grandit vers*, *Lignes max*, l'*En-t\xC3\xAAte (nom du mob)*, ce que *Les lignes montrent* (Ic\xC3\xB4ne, Nom ou Ic\xC3\xB4ne + nom), la *Taille des ic\xC3\xB4nes* et l'*Espacement des lignes* ; cette bo\xC3\xAEte a alors sa propre section *Cadre de la bo\xC3\xAEte des debuffs*."},
    {2, "*Text* styles each piece of text on its own, the list following what is shown : Name, HP%, Speed, Treasure Hunter, Distance, the debuff timer, and the debuff box header, name and timer, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* stylise chaque texte s\xC3\xA9par\xC3\xA9ment, la liste suivant ce qui est affich\xC3\xA9 : Nom, PV%, Vitesse, Treasure Hunter, Distance, le minuteur des debuffs, et l'en-t\xC3\xAAte, le nom et le minuteur de la bo\xC3\xAEte des debuffs, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Target module and target anything, or lean on the live preview in the corner, to tune the box against a real sample while you change the settings.",
        "Ouvre //aio config sur le module Cible et cible n'importe quoi, ou appuie-toi sur l'aper\xC3\xA7u en direct dans le coin, pour r\xC3\xA9gler le cadre sur un vrai \xC3\xA9""chantillon pendant que tu changes les options."},
};
static const int HELP_TARGET_N = (int)(sizeof(HELP_TARGET) / sizeof(HELP_TARGET[0]));

// PLAYER : the Player Hub -- your own box (identity + HP/MP/TP vitals + buffs).
static const HelpItem HELP_PLAYER[] = {
    {0, "Player", "Joueur"},
    {1, "The *Player* box is your own box, the counterpart to the Target box but always on you : who you are, your vitals, your buffs, your gil, your movement speed and your gear, all at a glance. Every part turns on or off on its own, so you build exactly the panel you want.",
        "La box *Joueur* est ta propre box, le pendant de la box Cible mais toujours sur toi : qui tu es, tes jauges, tes buffs, ton gil, ta vitesse de d\xC3\xA9placement et ton \xC3\xA9quipement, le tout d'un coup d'oeil. Chaque partie s'active ou se coupe s\xC3\xA9par\xC3\xA9ment, pour construire exactement le panneau que tu veux."},
    {46, "", ""},

    {0, "Identity", "Identit\xC3\xA9"},
    {1, "The top row is who you are : your *job emblem*, tinted by your role (tank, healer, damage, support), then your *name*, and your *main and sub jobs* with their levels, PLD 99 / WAR 54 for example.",
        "La ligne du haut, c'est qui tu es : ton *embl\xC3\xA8me de job*, teint\xC3\xA9 par ton r\xC3\xB4le (tank, healer, damage, support), puis ton *nom*, et tes *jobs principal et secondaire* avec leurs niveaux, PLD 99 / WAR 54 par exemple."},

    {0, "Vitals", "Jauges"},
    {1, "Your *HP*, *MP* and *TP* as the same living fioles used across the HUD, with a glass sheen and the liquid look.",
        "Tes *HP*, *MP* et *TP* en fioles vivantes, les m\xC3\xAAmes que partout dans le HUD, avec le reflet de verre et le rendu liquide."},
    {2, "*HP* runs green through yellow and orange to red as it drops. *MP* is blue. *TP* fills as it builds and lights up at 1000, ready for a weapon skill.",
        "Les *HP* vont du vert au rouge en passant par le jaune et l'orange \xC3\xA0 mesure qu'ils baissent. Les *MP* sont bleus. Les *TP* se remplissent et s'illuminent \xC3\xA0 1000, pr\xC3\xAAts pour une weapon skill."},
    {1, "While you cast, the spell name and a progress bar breathe just under the vitals, so you follow your own cast without the game's bar.",
        "Quand tu incantes, le nom du sort et une barre de progression pulsent juste sous les jauges, pour suivre ta propre incantation sans la barre du jeu."},

    {0, "Buffs, gil and speed", "Buffs, gil et vitesse"},
    {2, "Your *status icons* wrap into rows, capped by *Max buffs* so the box stays tidy.",
        "Tes *ic\xC3\xB4nes de statut* se r\xC3\xA9partissent en lignes, limit\xC3\xA9""es par *Buffs max* pour garder la box propre."},
    {2, "Your *gil* and your *movement speed* (a +/- percent, green faster / red slower) can sit in the box too.",
        "Ton *gil* et ta *vitesse de d\xC3\xA9placement* (un pourcentage +/-, vert plus rapide / rouge plus lent) peuvent aussi figurer dans la box."},

    {0, "Equipment", "\xC3\x89quipement"},
    {1, "An optional *equipment viewer* shows your 16 equipped items in a 4x4 grid. Place it inside the box, or dock it to a side (left, right, top or bottom) so it hugs the hub without crowding it.",
        "Un *visualiseur d'\xC3\xA9quipement* optionnel montre tes 16 objets \xC3\xA9quip\xC3\xA9s dans une grille 4x4. Place-le dans la box, ou accroche-le sur un c\xC3\xB4t\xC3\xA9 (gauche, droite, haut ou bas) pour qu'il colle au hub sans l'encombrer."},

    {0, "Configuration", "Configuration"},
    {1, "The Player module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Joueur dans //aio config r\xC3\xA8gle la box, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "In Simple, *Display* holds *Show* and *Size*, and *Content* turns each part on or off : *Emblem*, *Name*, *Level*, *HP*, *MP* and *TP* (each on its own), *Speed*, *The spell you cast*, *Buffs*, *Equipment* and *Gil*.",
        "En Simple, *Affichage* contient *Afficher* et *Taille*, et *Contenu* active ou coupe chaque partie : *Embl\xC3\xA8me*, *Nom*, *Niveau*, *PV*, *PM* et *TP* (chacun s\xC3\xA9par\xC3\xA9ment), *Vitesse*, *Le sort que tu lances*, *Buffs*, *\xC3\x89quipement* et *Gil*."},
    {2, "Advanced puts each on / off with the settings it governs : *Identity* (*Emblem* and its *Emblem size*, *Name*, *Level*, *Movement speed*), *Bars* (*HP*, *MP*, *TP*, then *Bar height*, *Bar width* and *Bar spacing* for the fioles), *Spell* (*The spell you cast*, and *Sample spell (to place it)* to show a fake cast while you place the cast line) and *Buffs* (*Buffs*, *Max buffs*, *Buff size*).",
        "Avanc\xC3\xA9 range chaque on / off avec les r\xC3\xA9glages qu'il commande : *Identit\xC3\xA9* (*Embl\xC3\xA8me* et sa *Taille de l'embl\xC3\xA8me*, *Nom*, *Niveau*, *Vitesse de d\xC3\xA9placement*), *Barres* (*PV*, *PM*, *TP*, puis *Hauteur des barres*, *Largeur des barres* et *Espacement des barres* pour les fioles), *Sort* (*Le sort que tu lances*, et *Sort d'exemple (pour le placer)* pour afficher un faux cast le temps de placer la ligne d'incantation) et *Buffs* (*Buffs*, *Buffs max*, *Taille des buffs*)."},
    {2, "*Equipment and gil* (Advanced) : *Equipment* is Off, *In the Player box* (then its *Placement*) or *Standalone* (its own box, placed in Edit Layout, with its *Size* and its own *Equipment frame* section) ; then the *Cell size*, the *Cell border* (*Custom* or *The box theme*) and the *Cell background*. *Gil* is on or off, and beside a standalone grid it also picks its side (Below, Above, Left or Right).",
        "*\xC3\x89quipement et gil* (Avanc\xC3\xA9) : l'*\xC3\x89quipement* est D\xC3\xA9sactiv\xC3\xA9, *Dans la box Joueur* (puis son *Placement*) ou *Autonome* (sa propre box, plac\xC3\xA9""e dans \xC3\x89""diter dispo, avec sa *Taille* et sa propre section *Cadre de l'\xC3\xA9quipement*) ; puis la *Taille des cases*, la *Bordure des cases* (*Perso* ou *Le th\xC3\xA8me de la box*) et le *Fond des cases*. Le *Gil* s'active ou se coupe, et \xC3\xA0 c\xC3\xB4t\xC3\xA9 d'une grille autonome il choisit aussi son c\xC3\xB4t\xC3\xA9 (Dessous, Dessus, Gauche ou Droite)."},
    {2, "The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*. *Text* (Advanced) styles each piece on its own, the list following what is shown.",
        "La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*. *Texte* (Avanc\xC3\xA9) stylise chaque \xC3\xA9l\xC3\xA9ment s\xC3\xA9par\xC3\xA9ment, la liste suivant ce qui est affich\xC3\xA9."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Player module to tune the whole hub against a live sample of yourself. Drag it anywhere in //aio edit, and roll the wheel over it to resize.",
        "Ouvre //aio config sur le module Joueur pour r\xC3\xA9gler tout le hub sur un aper\xC3\xA7u vivant de toi-m\xC3\xAAme. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit, et utilise la molette dessus pour redimensionner."},
};
static const int HELP_PLAYER_N = (int)(sizeof(HELP_PLAYER) / sizeof(HELP_PLAYER[0]));

// PET : one box for the pet jobs -- BST (Ready dial + jug pet), GEO (luopan + auras), SMN (avatar + pacts).
static const HelpItem HELP_PET[] = {
    {0, "Pet", "Pet"},
    {1, "The *Pet* box follows your pet job. On *BST* it shows your pet and your Ready charges, on *GEO* your luopan and your auras, on *SMN* your avatar or spirit and its pacts. It is one box : its place, size and frame are the same whatever job fills it, and every part has its own on / off.",
        "Le cadre *Pet* suit ton job \xC3\xA0 familier. En *BST* il montre ton pet et tes charges Ready, en *GEO* ton luopan et tes auras, en *SMN* ton avatar ou ton esprit et ses pacts. C'est un seul cadre : sa place, sa taille et son cadre sont les m\xC3\xAAmes quel que soit le job qui le remplit, et chaque partie a son propre on / off."},

    {0, "Reading the box", "Lire le cadre"},
    {2, "*BST* : the Ready dial with your charges and the seconds to the next one, the pet's image, name, HP, TP and distance, its correlation, its Ready moves with what each still waits for, and your job abilities.",
        "*BST* : le cadran Ready avec tes charges et les secondes avant la suivante, l'image du pet, son nom, ses HP, TP et sa distance, sa corr\xC3\xA9lation, ses Ready moves avec ce que chacun attend encore, et tes job abilities."},
    {2, "*GEO* : the luopan drawn as its bubble in the element of its Geo- spell, its HP as a ring, the estimated time it has left, the distance to it, the Indi- aura you carry, the one you entrusted, your MP, your job abilities, and the *Cardinal Chant* compass around it.",
        "*GEO* : le luopan dessin\xC3\xA9 en bulle dans l'\xC3\xA9l\xC3\xA9ment de son sort Geo-, ses HP en anneau, le temps qu'il lui reste estim\xC3\xA9, sa distance, l'aura Indi- que tu portes, celle que tu as confi\xC3\xA9""e, tes MP, tes job abilities, et la boussole *Cardinal Chant* autour."},
    {2, "*SMN* : the avatar or spirit in a disc, its HP as a ring and its TP as an inner arc, its name in its element's colour, the distance to it, the *Blood Pact Rage / Ward* recasts, your job abilities, your MP, and the pacts of that avatar you know with their MP cost (red when you are short).",
        "*SMN* : l'avatar ou l'esprit dans un disque, ses HP en anneau et ses TP en arc int\xC3\xA9rieur, son nom dans la couleur de son \xC3\xA9l\xC3\xA9ment, sa distance, les recasts *Blood Pact Rage / Ward*, tes job abilities, tes MP, et les pacts de cet avatar que tu connais avec leur co\xC3\xBBt en MP (en rouge quand il t'en manque)."},

    {0, "Configuration", "Configuration"},
    {2, "In Simple, *Display* holds the box's *Show*, *Size* and *Grows toward*, then *Job* : BST, GEO or SMN. The rows below it, and the preview, follow that job : its *Show on BST / GEO / SMN*, its *Layout* (*Side by side* or the pet, compass or portrait on top) and its *Only with a pet out* (luopan, avatar). *Content* turns each part of that job's box on or off, the ones listed above.",
        "En Simple, *Affichage* contient *Afficher*, *Taille* et *Grandit vers* du cadre, puis *Job* : BST, GEO ou SMN. Les lignes en dessous, et l'aper\xC3\xA7u, suivent ce job : son *Afficher en BST / GEO / SMN*, sa *Disposition* (*C\xC3\xB4te \xC3\xA0 c\xC3\xB4te* ou le pet, la boussole ou le portrait au-dessus) et son *Seulement avec un pet sorti* (luopan, avatar). *Contenu* active ou coupe chaque partie du cadre de ce job, celles list\xC3\xA9""es plus haut."},
    {2, "In Advanced, *Display (every job)* and *Frame (every job)* are the box's : *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*. Then a *BST | GEO | SMN* switch picks the job you set (the preview follows it), and that job's sections, each \"show + how\" being one choice (Off, then its modes).",
        "En Avanc\xC3\xA9, *Affichage (tous les jobs)* et *Cadre (tous les jobs)* sont ceux du cadre : *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*. Puis un s\xC3\xA9lecteur *BST | GEO | SMN* choisit le job que tu r\xC3\xA8gles (l'aper\xC3\xA7u le suit), et les sections de ce job, chaque \"afficher + comment\" \xC3\xA9tant un seul choix (D\xC3\xA9sactiv\xC3\xA9, puis ses modes)."},
    {2, "*BST* : *When and layout* (*Shown on BST* : Off, Always or Only with a pet out ; *Layout*), *Pet* (*Preview pet*, *Pet picture* : Off, Image, Correlation or Image + correlation, with their sizes, *Pet name*, *HP*, *TP*, *Distance*) and *Ready and abilities* (*Ready charges* : Off, Paws, Ring or Bars ; *Ready size*, *Ready moves*, *Moves' skillchain*, *Move type icons*, *Job abilities*).",
        "*BST* : *Quand et disposition* (*Affich\xC3\xA9""e en BST* : D\xC3\xA9sactiv\xC3\xA9, Toujours ou Seulement avec un pet sorti ; *Disposition*), *Pet* (*Pet (aper\xC3\xA7u)*, *Image du pet* : D\xC3\xA9sactiv\xC3\xA9, Image, Corr\xC3\xA9lation ou Image + corr\xC3\xA9lation, avec leurs tailles, *Nom du pet*, *HP*, *TP*, *Distance*) et *Ready et abilities* (*Charges Ready* : D\xC3\xA9sactiv\xC3\xA9, Pattes, Cercle ou Barres ; *Taille Ready*, *Ready moves*, *Skillchain des moves*, *Ic\xC3\xB4nes de type*, *Job abilities*)."},
    {2, "*GEO* : *When and layout* (*Shown on GEO*, *Layout*), *Luopan* (*Icon / luopan size*, *Bubble name*, *Luopan HP*, *Estimated time left*, *Distance to the luopan*, and the *Cardinal Chant compass* : Off, Compass only or Compass + bonus) and *Auras, MP and abilities* (*Aura (Indi-)*, *Entrust*, *MP*, *Job abilities* : Off, Icons, Icons + names or Names).",
        "*GEO* : *Quand et disposition* (*Affich\xC3\xA9""e en GEO*, *Disposition*), *Luopan* (*Taille ic\xC3\xB4nes / luopan*, *Nom de la bulle*, *HP du luopan*, *Temps estim\xC3\xA9 du luopan*, *Distance au luopan*, et la *Boussole Cardinal Chant* : D\xC3\xA9sactiv\xC3\xA9, Boussole seule ou Boussole + bonus) et *Auras, MP et abilities* (*Aura (Indi-)*, *Entrust*, *MP*, *Job abilities* : D\xC3\xA9sactiv\xC3\xA9, Ic\xC3\xB4nes, Ic\xC3\xB4nes + noms ou Noms)."},
    {2, "*SMN* : *When and layout* (*Shown on SMN*, *Layout*), *Avatar* (*Preview avatar*, *Portrait / icon size*, *Avatar portrait*, *Avatar name*, *Avatar HP*, *Avatar TP*, *Distance to the avatar*) and *Pacts, abilities and MP*. *Text* styles that job's text elements.",
        "*SMN* : *Quand et disposition* (*Affich\xC3\xA9""e en SMN*, *Disposition*), *Avatar* (*Avatar (aper\xC3\xA7u)*, *Taille portrait / ic\xC3\xB4nes*, *Portrait de l'avatar*, *Nom de l'avatar*, *HP de l'avatar*, *TP de l'avatar*, *Distance \xC3\xA0 l'avatar*) et *Pacts, abilities et MP*. *Texte* stylise les \xC3\xA9l\xC3\xA9ments de texte de ce job."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Pet module and pick a job (*Job* in Simple, the *BST | GEO | SMN* switch in Advanced) to tune its box against a sample, or call your pet to see it live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Pet et choisis un job (*Job* en Simple, le s\xC3\xA9lecteur *BST | GEO | SMN* en Avanc\xC3\xA9) pour r\xC3\xA9gler son cadre sur un exemple, ou appelle ton pet pour le voir en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_PET_N = (int)(sizeof(HELP_PET) / sizeof(HELP_PET[0]));

// MINIMAP : the round/square map of your zone + its Vana'diel clock box. Live samples (kinds 30-33) draw the
// REAL elements -- the map disc, the marker legend, the moon and the elemental day -- via minimap_help_*.
static const HelpItem HELP_MINIMAP[] = {
    {0, "Minimap", "Minicarte"},
    {1, "The *Minimap* draws a live map of the zone you are in, with you always at its centre and everything around you, party, players, NPCs and monsters, shown as coloured markers that move in real time. Above the map sits an optional *Vana'diel clock* with the time, the elemental day, and the moon. It is drawn straight from the game's own maps, so it always matches where you actually are.",
        "La *Minicarte* dessine en direct la carte de la zone o\xC3\xB9 tu te trouves, avec toi toujours en son centre et tout ce qui t'entoure, party, joueurs, PNJ et monstres, montr\xC3\xA9 par des marqueurs color\xC3\xA9s qui bougent en temps r\xC3\xA9""el. Au-dessus de la carte se pose une *horloge Vana'diel* optionnelle avec l'heure, le jour \xC3\xA9l\xC3\xA9mentaire et la lune. Elle est tir\xC3\xA9""e des cartes du jeu, donc elle correspond toujours \xC3\xA0 l'endroit o\xC3\xB9 tu es vraiment."},

    {0, "What the map shows", "Ce que montre la carte"},
    {1, "You are the arrow at the very centre, pointing where you face, and the map scrolls under you as you walk. It can be a *round* lens, ringed by a brass *Bezel ring* if you like, or a *square* panel, framed by your chosen box theme, and it is *north-up*, so the top of the map is always north. Roll the mouse wheel over it to zoom in and out, from the whole zone down to a close look around you.",
        "Tu es la fl\xC3\xA8""che au centre exact, point\xC3\xA9""e l\xC3\xA0 o\xC3\xB9 tu regardes, et la carte d\xC3\xA9""file sous toi quand tu marches. Elle peut \xC3\xAAtre une lentille *ronde*, cercl\xC3\xA9""e d'un *Anneau bordure* en laiton si tu veux, ou un panneau *carr\xC3\xA9*, encadr\xC3\xA9""e par le th\xC3\xA8me de box que tu as choisi, et elle est *nord en haut*, donc le haut de la carte est toujours le nord. Utilise la molette dessus pour zoomer et d\xC3\xA9zoomer, de la zone enti\xC3\xA8re jusqu'\xC3\xA0 une vue rapproch\xC3\xA9""e autour de toi."},
    {30, "", ""},
    {1, "If the current zone has no map on file, the box shows just the clock header, so the Vana'diel time and moon stay with you everywhere.",
        "Si la zone actuelle n'a pas de carte disponible, le cadre n'affiche que l'en-t\xC3\xAAte d'horloge, pour garder l'heure Vana'diel et la lune partout avec toi."},

    {0, "The markers", "Les marqueurs"},
    {1, "Every entity around you is a marker, and its shape and colour tell you at a glance what it is. Players are dots, like NPCs, each kind its own colour : your party blue, the alliance lavender, trusts pale blue, your pet teal, everyone else grey, and your own people wear a white rim. A monster is an arrow that points where it faces. The places you travel by, the *Home Point*, the *Waypoint*, the *Survival Guide*, the *Moogle* and a *???* to examine, keep their own large icon so you spot them from afar.",
        "Chaque entit\xC3\xA9 autour de toi est un marqueur, et sa forme et sa couleur te disent d'un coup d'oeil ce que c'est. Les joueurs sont des points, comme les PNJ, chacun de sa couleur : ta party en bleu, l'alliance en lavande, les trusts en bleu p\xC3\xA2le, ton familier en turquoise, tous les autres en gris, et les tiens portent un liser\xC3\xA9 blanc. Un monstre est une fl\xC3\xA8""che point\xC3\xA9""e l\xC3\xA0 o\xC3\xB9 il regarde. Les lieux qui te servent \xC3\xA0 voyager, le *Home Point*, le *Waypoint*, le *Survival Guide*, le *Moogle* et un *???* \xC3\xA0 examiner, gardent leur grande ic\xC3\xB4ne pour les rep\xC3\xA9rer de loin."},
    {31, "", ""},
    {1, "A monster you or your party have claimed, or that is fighting you, turns *red*, an unclaimed one is *yellow*, and one another player has claimed shows *magenta*, so you never swing at someone else's mob by mistake. A notorious monster is a bigger arrow with a bright gold frame.",
        "Un monstre que toi ou ta party avez r\xC3\xA9""clam\xC3\xA9, ou qui te combat, passe au *rouge*, un monstre libre est *jaune*, et celui qu'un autre joueur a r\xC3\xA9""clam\xC3\xA9 appara\xC3\xAEt en *magenta*, pour ne jamais frapper le mob de quelqu'un d'autre par erreur. Un monstre notable est une fl\xC3\xA8""che plus grande, au liser\xC3\xA9 or vif."},
    {1, "The *zoom* decides how much detail you get. Zoomed out, monsters are small dots, so a busy camp stays readable ; zoom in and they grow into their arrows. Your party and trusts walking right beside you are kept as dots around your arrow, so they never hide under it.",
        "Le *zoom* d\xC3\xA9""cide du niveau de d\xC3\xA9tail. D\xC3\xA9zoom\xC3\xA9, les monstres sont de petits points, pour qu'un camp charg\xC3\xA9 reste lisible ; en zoomant, ils grandissent en fl\xC3\xA8""ches. Ta party et tes trusts qui marchent juste \xC3\xA0 c\xC3\xB4t\xC3\xA9 de toi restent des points autour de ta fl\xC3\xA8""che, pour ne jamais dispara\xC3\xAEtre dessous."},

    {0, "The Vana'diel clock", "L'horloge Vana'diel"},
    {1, "The clock box on top gathers everything time-related in Vana'diel. It carries the current *time*, the *elemental day* flowing into the next one, the *moon* with its phase, and, if you like, your own *real* and *GMT* time to keep an eye on the clock behind the game.",
        "Le cadre d'horloge en haut rassemble tout ce qui touche au temps dans Vana'diel. Il porte l'*heure* actuelle, le *jour \xC3\xA9l\xC3\xA9mentaire* qui s'encha\xC3\xAEne vers le suivant, la *lune* avec sa phase, et, si tu veux, ta propre heure *r\xC3\xA9""elle* et *GMT* pour garder un oeil sur l'horloge derri\xC3\xA8re le jeu."},

    {0, "The moon", "La lune"},
    {1, "The moon is drawn for real, lit from the correct side and shaded by its phase, and it counts down the days to the next *New* and *Full* moon, handy for anything in Vana'diel that leans on the lunar phase.",
        "La lune est dessin\xC3\xA9""e pour de vrai, \xC3\xA9""clair\xC3\xA9""e du bon c\xC3\xB4t\xC3\xA9 et ombr\xC3\xA9""e selon sa phase, et elle d\xC3\xA9""compte les jours jusqu'aux prochaines *Nouvelle* et *Pleine* lunes, pratique pour tout ce qui d\xC3\xA9pend de la phase lunaire dans Vana'diel."},
    {32, "live, sweeping New to Full", "en direct, de la Nouvelle \xC3\xA0 la Pleine lune"},

    {0, "The elemental day", "Le jour \xC3\xA9l\xC3\xA9mentaire"},
    {1, "Each Vana'diel day belongs to one of the eight elements, Fire, Earth, Water, Wind, Ice, Lightning, Light and Dark, shown by its icon and tint. The clock names the current day and points to the next, so you always know which element is in the air.",
        "Chaque jour de Vana'diel appartient \xC3\xA0 l'un des huit \xC3\xA9l\xC3\xA9ments, Feu, Terre, Eau, Vent, Glace, Foudre, Lumi\xC3\xA8re et T\xC3\xA9n\xC3\xA8""bres, montr\xC3\xA9 par son ic\xC3\xB4ne et sa teinte. L'horloge nomme le jour en cours et pointe vers le suivant, pour toujours savoir quel \xC3\xA9l\xC3\xA9ment est dans l'air."},
    {33, "the eight elemental days", "les huit jours \xC3\xA9l\xC3\xA9mentaires"},

    {0, "Configuration", "Configuration"},
    {1, "In Simple, the Minimap module in //aio config has two cards, *Display* and *Shape & Frame*. Advanced lays it out as *Display*, *Frame*, *Box frame*, *Clock* (or *Dial*), *Info band* (Compact only), *Markers* and *Text* : the *Layout* decides which sections and rows exist.",
        "En Simple, le module Minicarte dans //aio config a deux cartes, *Affichage* et *Forme & Cadre*. Avanc\xC3\xA9 le range en *Affichage*, *Cadre*, *Cadre de la bo\xC3\xAEte*, *Horloge* (ou *Cadran*), *Bande d'infos* (Compact seulement), *Marqueurs* et *Texte* : la *Disposition* d\xC3\xA9""cide quelles sections et lignes existent."},
    {2, "*Display* turns the map on with *Show*, sets the *Size* and the *Map size* on their own, the *Zoom* level, and (in Simple) the *Map background* opacity, from a solid backdrop down to fully transparent so the game shows through. In Advanced it adds the *Layout* : *Classic* (a header on one side of the map), *Compact* (thin bands above or below it) or *Dial (round)* (the clock built around the map) ; and the *Shape*, Square or Round.",
        "*Affichage* active la carte avec *Afficher*, r\xC3\xA8gle la *Taille* et la *Taille carte* s\xC3\xA9par\xC3\xA9ment, le niveau de *Zoom*, et (en Simple) l'opacit\xC3\xA9 du *Fond carte*, d'un fond plein jusqu'\xC3\xA0 totalement transparent pour laisser voir le jeu. En Avanc\xC3\xA9 il ajoute la *Disposition* : *Classique* (un en-t\xC3\xAAte sur un c\xC3\xB4t\xC3\xA9 de la carte), *Compact* (de fines bandes au-dessus ou en dessous) ou *Cadran (rond)* (l'horloge b\xC3\xA2tie autour de la carte) ; et la *Forme*, Carr\xC3\xA9 ou Rond."},
    {2, "*Shape & Frame* (Simple) picks the *Shape*, Square or Round, and the *Frame*, None, your Box Theme, or a Custom colour of your own. A round map can wear the *Bezel ring*, with its *Bezel width* and *Cardinal size* ; a Custom square frame has its *Border width*.",
        "*Forme & Cadre* (Simple) choisit la *Forme*, Carr\xC3\xA9 ou Rond, et le *Cadre*, Aucun, ton Th\xC3\xA8me box, ou une couleur Perso \xC3\xA0 toi. Une carte ronde peut porter l'*Anneau bordure*, avec sa *Largeur anneau* et sa *Taille cardinaux* ; un cadre carr\xC3\xA9 Perso a sa *Largeur bordure*."},
    {2, "*Frame* (Advanced) : the *Frame style* (None, Box Theme or Custom), then only the rows your shape draws (*Bezel ring*, *Bezel width* -- *Case width* on a square Dial --, *Cardinal size*, or the square's *Border width*), the *Map background*, and the *Gem (zone without a map)* that closes the lens where the zone has no map. *Box frame* themes the box round the clock and the map : *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "*Cadre* (Avanc\xC3\xA9) : le *Style du cadre* (Aucun, Th\xC3\xA8me box ou Perso), puis seulement les lignes que ta forme dessine (*Anneau bordure*, *Largeur anneau* -- *Largeur du bo\xC3\xAEtier* sur un Cadran carr\xC3\xA9 --, *Taille cardinaux*, ou la *Largeur bordure* du carr\xC3\xA9), le *Fond carte*, et la *Pierre (zone sans carte)* qui ferme la lentille quand la zone n'a pas de carte. *Cadre de la bo\xC3\xAEte* habille la bo\xC3\xAEte autour de l'horloge et de la carte : *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},
    {2, "*Clock* (Classic, Compact) is one choice, Off or where it sits (*Clock* : Top, Bottom, Left or Right ; *Clock band* in Compact : Top or Bottom), then *Vana'diel time*, *Elemental day*, *Moon phase* and *Real / GMT time*. *Info band* (Compact) sets its *Position* (Off, Top or Bottom) and the *Distances (target, Wide Scan)*.",
        "*Horloge* (Classique, Compact) est un seul choix, D\xC3\xA9sactiv\xC3\xA9 ou sa place (*Horloge* : Haut, Bas, Gauche ou Droite ; *Bande horloge* en Compact : Haut ou Bas), puis *Heure Vana'diel*, *Jour \xC3\xA9l\xC3\xA9mentaire*, *Phase de lune* et *Heure r\xC3\xA9""elle / GMT*. *Bande d'infos* (Compact) r\xC3\xA8gle sa *Position* (Aucune, Haut ou Bas) et les *Distances (cible, Wide Scan)*."},
    {2, "*Dial* replaces the Clock on the Dial layout : the *Day dial* (*Globe (colours)*, *Carousel (icons)* or *Disc (icons)*), then *Vana'diel time*, *Elemental day* and *Moon phase*. With the Disc, *Map square (F-10)* and *Hour / map square* join them, and each module, *Elemental day*, *Moon phase*, *Day / night* and *Weather*, is Off or one place round the compass (a place another module holds is skipped). The Dial has no Real / GMT time.",
        "*Cadran* remplace l'Horloge sur la disposition Cadran : le *Cadran du jour* (*Globe (couleurs)*, *Carrousel (ic\xC3\xB4nes)* ou *Disque (ic\xC3\xB4nes)*), puis *Heure Vana'diel*, *Jour \xC3\xA9l\xC3\xA9mentaire* et *Phase de lune*. Avec le Disque, *Case de carte (F-10)* et *Heure / case de carte* s'y ajoutent, et chaque module, *Jour \xC3\xA9l\xC3\xA9mentaire*, *Phase de lune*, *Jour / nuit* et *M\xC3\xA9t\xC3\xA9o*, est D\xC3\xA9sactiv\xC3\xA9 ou une place autour de la boussole (une place d\xC3\xA9j\xC3\xA0 prise par un autre module est saut\xC3\xA9""e). Le Cadran n'a pas d'Heure r\xC3\xA9""elle / GMT."},
    {2, "*Markers* (Advanced) sets the *Marker style* (Classic or Icons) and the *Marker Size*, turns each type on or off, *Players (PC)*, *NPCs* and *Monsters*, and adds the *Target line*, the *Range ring* with its *Radius*, the *Wide Scan track* (each with its colour) and the *Map square (F-10)* ; on the Dial, *Distances on the marks (target, Wide Scan)*.",
        "*Marqueurs* (Avanc\xC3\xA9) r\xC3\xA8gle le *Style des marqueurs* (Classique ou Ic\xC3\xB4nes) et la *Taille marqueurs*, active ou non chaque type, *Joueurs (PC)*, *PNJ* et *Monstres*, et ajoute la *Ligne cible*, l'*Anneau de port\xC3\xA9""e* avec son *Rayon*, le *Suivi Wide Scan* (chacun avec sa couleur) et la *Case de carte (F-10)* ; sur le Cadran, les *Distances sur les marques (cible, Wide Scan)*."},
    {2, "*Text* (Advanced, with the clock on) styles each clock element, *Time*, *Day*, *Moon* and *Real / GMT*, on its own, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9, avec l'horloge) stylise chaque \xC3\xA9l\xC3\xA9ment d'horloge, *Heure*, *Jour*, *Lune* et *R\xC3\xA9""el / GMT*, s\xC3\xA9par\xC3\xA9ment, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Minimap module while you play to tune the box, the map and the clock against the real thing. Drag it anywhere in //aio edit, and roll the wheel over it in game to set your zoom.",
        "Ouvre //aio config sur le module Minicarte pendant que tu joues pour r\xC3\xA9gler le cadre, la carte et l'horloge sur le vrai rendu. D\xC3\xA9place-la o\xC3\xB9 tu veux avec //aio edit, et utilise la molette dessus en jeu pour r\xC3\xA9gler ton zoom."},
};
static const int HELP_MINIMAP_N = (int)(sizeof(HELP_MINIMAP) / sizeof(HELP_MINIMAP[0]));

// HATE LIST : the mobs that have aggro on you / your party, one HP fiole per mob, lowest-HP on top. Live sample
// (kind 40) draws the REAL box in preview mode via hatelist_help_box, so it matches your config exactly.
static const HelpItem HELP_HATELIST[] = {
    {0, "Hate List", "Liste de haine"},
    {1, "The *Hate List* shows every monster that has aggro on you or your party, one slim HP fiole per mob, sorted so the closest to death sits on top. It is your kill order at a glance in a big pull, so you finish the low ones first and never lose track of what is beating on the group.",
        "La *Liste de haine* montre chaque monstre qui a de la haine sur toi ou ta party, une fine fiole de HP par mob, tri\xC3\xA9""e pour que le plus proche de la mort soit tout en haut. C'est ton ordre de kill d'un coup d'oeil dans un gros pull, pour finir les bas d'abord et ne jamais perdre de vue ce qui tape sur le groupe."},
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
        "Le module Liste de haine dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* : *Show*, *Size* (scales the whole box) and *Grows toward*. The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity* ; with the box off, the fioles float bare.",
        "*Affichage* : *Afficher*, *Taille* (met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle) et *Grandit vers*. La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9* ; sans cadre, les fioles flottent nues."},
    {2, "*Mob list* : *Max mobs* caps how many mobs show at once, so you keep only the lowest-HP few. *Distance column* and *Target column* toggle those two side columns.",
        "*Liste des mobs* : *Mobs max* limite combien de mobs s'affichent, pour ne garder que les quelques plus bas en HP. *Colonne distance* et *Colonne cible* activent ces deux colonnes lat\xC3\xA9rales."},
    {2, "*Text* (Advanced) styles each piece, Distance, Name, HP% and Target (the columns shown), on its own, with its font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise chaque \xC3\xA9l\xC3\xA9ment, Distance, Nom, PV% et Cible (les colonnes affich\xC3\xA9""es), s\xC3\xA9par\xC3\xA9ment, avec sa police, sa taille, son contour, gras / italique / capitales et sa couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Hate List module to tune it against a sample list, or pull a pack of mobs and watch it fill live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Liste de haine pour le r\xC3\xA9gler sur une liste d'exemple, ou pull un paquet de mobs et regarde-la se remplir en direct. D\xC3\xA9place-la o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_HATELIST_N = (int)(sizeof(HELP_HATELIST) / sizeof(HELP_HATELIST[0]));

// POINTWATCH : your progression (XP / CP / ML) + Merits, each with a live per-hour rate. Live sample (kind 41)
// draws the REAL box in preview mode via pointwatch_help_box, config-aware (layout / display / text / rate).
static const HelpItem HELP_POINTWATCH[] = {
    {0, "PointWatch", "PointWatch"},
    {1, "*PointWatch* tracks your progression at a glance : your current stage -- XP under level 99, Capacity Points at 99, or Master-Level Exemplar Points once you are mastered -- plus your Merits, each shown with how far along you are and how fast you are earning per hour.",
        "*PointWatch* suit ta progression d'un coup d'oeil : ton stade actuel -- XP sous le niveau 99, Capacity Points \xC3\xA0 99, ou Exemplar Points de Master Level une fois master -- plus tes Merits, chacun avec ta progression et ta vitesse de gain par heure."},
    {41, "", ""},

    {0, "The progression row", "La ligne de progression"},
    {1, "The first row follows the stage that fits your character and switches on its own as you level.",
        "La premi\xC3\xA8re ligne suit le stade qui correspond \xC3\xA0 ton personnage et bascule seule quand tu montes."},
    {2, "*XP* while under level 99, current over the amount required for the next level.",
        "*XP* tant que tu es sous le niveau 99, actuel sur le montant requis pour le niveau suivant."},
    {2, "*CP* (Capacity Points) at 99, with your *Job Points* count beside it.",
        "*CP* (Capacity Points) \xC3\xA0 99, avec ton nombre de *Job Points* \xC3\xA0 c\xC3\xB4t\xC3\xA9."},
    {2, "*ML* (Master Level) once you are mastered, your Exemplar Points toward the next master level.",
        "*ML* (Master Level) une fois master, tes Exemplar Points vers le prochain master level."},
    {2, "The *X/h* on the right is your live earn rate, built from your recent gains, so you know your pace and can estimate the time left.",
        "Le *X/h* \xC3\xA0 droite est ta vitesse de gain en direct, calcul\xC3\xA9""e sur tes gains r\xC3\xA9""cents, pour conna\xC3\xAEtre ton rythme et estimer le temps restant."},

    {0, "The Merits row", "La ligne Merits"},
    {1, "The second row is always your *Merits* : your Limit Points toward the next merit, and your held merits over the maximum you can carry -- when you are capped both counts simply show the max (e.g. 10k / 10k, 75 / 75).",
        "La deuxi\xC3\xA8me ligne est toujours tes *Merits* : tes Limit Points vers le prochain merit, et tes merits d\xC3\xA9tenus sur le maximum que tu peux porter -- une fois capp\xC3\xA9, les deux compteurs affichent simplement le max (ex. 10k / 10k, 75 / 75)."},

    {0, "Configuration", "Configuration"},
    {1, "The PointWatch module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module PointWatch dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* : *Show*, *Size* (scales the whole box), *Grows toward* and the *Layout*, Vertical or Horizontal. The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "*Affichage* : *Afficher*, *Taille* (met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle), *Grandit vers* et la *Disposition*, Vertical ou Horizontal. La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},
    {2, "*Progress bars* : *Progression* is Auto (by level / ML) or forced to XP, CP or ML. *Shows* is *Both*, *Text only* or *Bar only*. *Show rate (X/h)* toggles the per-hour readout (there is no text line, so no rate, with *Bar only*).",
        "*Barres de progression* : *Progression* est Auto (par niveau / ML) ou forc\xC3\xA9 sur XP, CP ou ML. *Affiche* vaut *Les deux*, *Texte seul* ou *Barre seule*. *Afficher le taux (X/h)* active le gain par heure (avec *Barre seule* il n'y a pas de ligne de texte, donc pas de taux)."},
    {2, "*Text* (Advanced) styles the Label, Value and Rate on their own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise le Label, la Valeur et le Taux s\xC3\xA9par\xC3\xA9ment, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the PointWatch module to tune it against a sample, then kill a few mobs to watch the value and the rate move live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module PointWatch pour le r\xC3\xA9gler sur un exemple, puis tue quelques mobs pour voir la valeur et le taux bouger en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_POINTWATCH_N = (int)(sizeof(HELP_POINTWATCH) / sizeof(HELP_POINTWATCH[0]));

// TIMERS : your active buff durations (exact server timers) + your recasts, on one or two boxes. Live sample
// (kind 42) draws the REAL box in preview mode via timers_help_box, config-aware (fused/separate, icon/name, colours).
static const HelpItem HELP_TIMERS[] = {
    {0, "Timers", "Timers"},
    {1, "*Timers* counts down your active *buffs* and your *recasts* on one clean panel, each row an icon and a name with the time left, coloured brighter as it runs out. Buff durations are the game's *exact* server timers, not estimates, so a countdown ends the moment the effect really wears.",
        "*Timers* d\xC3\xA9""compte tes *buffs* actifs et tes *recasts* sur un panneau \xC3\xA9pur\xC3\xA9, chaque ligne une ic\xC3\xB4ne et un nom avec le temps restant, color\xC3\xA9 plus vif \xC3\xA0 mesure que \xC3\xA7""a s'\xC3\xA9puise. Les dur\xC3\xA9""es de buffs sont les minuteurs *exacts* du serveur, pas des estimations, donc un d\xC3\xA9""compte se termine pile quand l'effet s'estompe vraiment."},
    {42, "", ""},

    {0, "The two columns", "Les deux colonnes"},
    {2, "*Duration* lists your active buffs with the time left on each, so you know when to refresh your food, songs, rolls or self-buffs. It also carries what *other people* put on you \xE2\x80\x94 each of those rows names its owner in parentheses.",
        "*Duration* liste tes buffs actifs avec le temps restant sur chacun, pour savoir quand refresh ta bouffe, tes chants, tes rolls ou tes self-buffs. Elle porte aussi ce que les *autres* t'ont pos\xC3\xA9 \xE2\x80\x94 chacune de ces lignes nomme son propri\xC3\xA9taire entre parenth\xC3\xA8ses."},
    {2, "*Recast* lists your spells and abilities on cooldown, counting down to when you can use them again.",
        "*Recast* liste tes sorts et aptitudes en r\xC3\xA9""cup\xC3\xA9ration, en d\xC3\xA9""comptant jusqu'\xC3\xA0 ce que tu puisses les relancer."},

    {0, "Reading a timer", "Lire un minuteur"},
    {2, "A *buff* timer is *white* while it has time, turns *orange* under 30 seconds, and flashes *red* in the last 10 seconds, so an expiring effect grabs your eye.",
        "Un minuteur de *buff* est *blanc* tant qu'il a du temps, passe *orange* sous 30 secondes, et clignote en *rouge* dans les 10 derni\xC3\xA8res secondes, pour qu'un effet qui expire attire l'oeil."},
    {2, "A *recast* is the opposite : *red* just after you use it (a long wait), turning *orange* then *green* as it nears ready, so a glance tells you what is coming back up.",
        "Un *recast* est l'inverse : *rouge* juste apr\xC3\xA8s l'avoir utilis\xC3\xA9 (longue attente), passant *orange* puis *vert* \xC3\xA0 mesure qu'il approche d'\xC3\xAAtre pr\xC3\xAAt, pour voir d'un coup d'oeil ce qui revient bient\xC3\xB4t."},
    {2, "A Duration row can show the *icon*, the *name*, or *both* \xE2\x80\x94 and a row about *someone else* always names them : *Icon* is icon + person, *Name* is person + spell, *Both* is all three. An ally row never turns into an anonymous icon. Recast rows are always the *name* (no icon set exists for recasts).",
        "Une ligne Duration peut montrer l'*ic\xC3\xB4ne*, le *nom*, ou les *deux* \xE2\x80\x94 et une ligne qui concerne *quelqu'un d'autre* le nomme toujours : *Ic\xC3\xB4ne* donne ic\xC3\xB4ne + personne, *Nom* personne + sort, *Les deux* les trois. Une ligne alli\xC3\xA9""e ne devient jamais une ic\xC3\xB4ne anonyme. Les lignes Recast sont toujours le *nom* (pas de jeu d'ic\xC3\xB4nes pour les recasts)."},

    {0, "Fused or separate", "Fusionn\xC3\xA9 ou s\xC3\xA9par\xC3\xA9"},
    {1, "Keep the two columns *fused* in one box, or split them into two boxes you place independently. An empty column simply disappears until something fills it.",
        "Garde les deux colonnes *fusionn\xC3\xA9""es* dans un cadre, ou s\xC3\xA9pare-les en deux cadres que tu places ind\xC3\xA9pendamment. Une colonne vide dispara\xC3\xAEt simplement jusqu'\xC3\xA0 ce que quelque chose la remplisse."},

    {0, "Configuration", "Configuration"},
    {1, "The Timers module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Timers dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* : *Show*, *Size* (scales the whole box), *Grows toward* and the *Layout*, Fused or Separate. The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "*Affichage* : *Afficher*, *Taille* (met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle), *Grandit vers* et la *Disposition*, Fusionn\xC3\xA9 ou S\xC3\xA9par\xC3\xA9. La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},
    {2, "In Simple, *Content* holds *Show titles* (the column titles), *Buff source* (whose buffs are kept, see below) and *My buffs on allies* (the buffs you cast on other players, with their name and an estimated timer).",
        "En Simple, *Contenu* contient *Afficher les titres* (les titres des colonnes), *Source des buffs* (quels buffs sont gard\xC3\xA9s, voir plus bas) et *Mes buffs sur alli\xC3\xA9s* (les buffs que tu as lanc\xC3\xA9s sur d'autres joueurs, avec leur nom et un minuteur estim\xC3\xA9)."},
    {2, "In Advanced, each list is named by its column. *Duration (buffs)* : *Buff source*, *My buffs on allies*, *Each buff shows* (*Icon*, *Name* or *Both* \xE2\x80\x94 on a row about someone else the *person is always named*, so *Icon* is icon + person, *Name* is person + spell, *Both* is all three) and its *Order* : *Person, then time* groups your buffs, then the ones you put on allies, then the ones others put on you -- or *Shortest first*, everyone mixed. Trusts stay last either way.",
        "En Avanc\xC3\xA9, chaque liste porte le nom de sa colonne. *Dur\xC3\xA9""e (buffs)* : *Source des buffs*, *Mes buffs sur alli\xC3\xA9s*, *Chaque buff montre* (*Ic\xC3\xB4ne*, *Nom* ou *Les deux* \xE2\x80\x94 sur une ligne qui concerne quelqu'un d'autre, la *personne est toujours nomm\xC3\xA9""e* : *Ic\xC3\xB4ne* donne ic\xC3\xB4ne + personne, *Nom* donne personne + sort, *Les deux* donne les trois) et son *Ordre* : *Personne, puis temps* groupe tes buffs, puis ceux que tu as pos\xC3\xA9s sur des alli\xC3\xA9s, puis ceux qu'on t'a pos\xC3\xA9s -- ou *Plus court d'abord*, tout le monde m\xC3\xA9lang\xC3\xA9. Les trusts restent derniers dans les deux cas."},
    {2, "*Recast* : its *Order*, *Shortest first* or *By name*. *Rows (both columns)* : *Column titles*, *Max per column*, *Icon size* and *Row spacing*. Then the *Buff filter* and the *Alerts* (*Tracking numbers (//aio out)*, *SP last-min alert*, *Alert under*, *Alert duration*, and *Reset timers*), described below.",
        "*Recast* : son *Ordre*, *Plus court d'abord* ou *Par nom*. *Lignes (les deux colonnes)* : *Titres des colonnes*, *Max par colonne*, *Taille ic\xC3\xB4nes* et *Espacement lignes*. Puis le *Filtre de buffs* et les *Alertes* (*Num\xC3\xA9ros de suivi (//aio out)*, *Alerte SP derni\xC3\xA8re min*, *Alerter sous*, *Dur\xC3\xA9""e de l'alerte*, et *R\xC3\xA9initialiser timers*), d\xC3\xA9""crits plus bas."},
    {2, "*Text* (Advanced) styles the Title, Name and Timer on their own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise le Titre, le Nom et le Timer s\xC3\xA9par\xC3\xA9ment, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Buff filter", "Filtre de buffs"},
    {1, "A full spellbook is noise. The *Buff filter* section lets you pick, buff by buff, exactly what the Duration column follows. Buffs are grouped by *magic family* \xE2\x80\x94 Protect / Shell, Haste, Barspells, Songs, Rolls, Ninjutsu, Food, Aftermath, Signet-type and Crafting \xE2\x80\x94 that you fold or unfold, each family header carrying a group toggle that sets the *whole* family at once. It is *job-agnostic* : one setting per buff, stored on the profile and shared across every job \xE2\x80\x94 hide Shell once and it stays hidden whatever you play.",
        "Un grimoire complet, c'est du bruit. La section *Filtre de buffs* te laisse choisir, buff par buff, ce que la colonne Duration suit exactement. Les buffs sont group\xC3\xA9s par *famille de magie* \xE2\x80\x94 Protect / Shell, Haste, Barspells, Chants, Rolls, Ninjutsu, Nourriture, Aftermath, Signet et Artisanat \xE2\x80\x94 que tu d\xC3\xA9plies ou replies, l'en-t\xC3\xAAte de chaque famille portant un bouton de groupe qui r\xC3\xA8gle *toute* la famille d'un coup. C'est *ind\xC3\xA9pendant du job* : un r\xC3\xA9glage par buff, stock\xC3\xA9 sur le profil et partag\xC3\xA9 entre tous les jobs \xE2\x80\x94 masque Shell une fois et il reste masqu\xC3\xA9 quel que soit ce que tu joues."},
    {2, "*Recasts are always shown* \xE2\x80\x94 the filter only ever touches buffs, never your cooldowns.",
        "*Les recasts sont toujours affich\xC3\xA9s* \xE2\x80\x94 le filtre ne touche que les buffs, jamais tes r\xC3\xA9""cup\xC3\xA9rations."},

    {0, "The four states", "Les quatre \xC3\xA9tats"},
    {1, "Each buff carries a *dot*. Click it to cycle through four behaviours \xE2\x80\x94 the legend just above the list shows each one. The cycle is *Show \xE2\x86\x92 Show + alert \xE2\x86\x92 Hide \xE2\x86\x92 Hide + alert* and back.",
        "Chaque buff porte une *pastille*. Clique dessus pour parcourir quatre comportements \xE2\x80\x94 la l\xC3\xA9gende juste au-dessus de la liste les montre. Le cycle est *Afficher \xE2\x86\x92 Afficher + alerte \xE2\x86\x92 Masquer \xE2\x86\x92 Masquer + alerte* puis retour."},
    {2, "*Show* : shown normally in the panel, no alert.",
        "*Afficher* : affich\xC3\xA9 normalement dans le panneau, aucune alerte."},
    {2, "*Show + alert* : shown, and once it drops under the *Alert under* threshold its name and timer *blink* (recast soon) ; a red \"OUT\" line then replaces it the moment it falls or is dispelled, until you re-cast.",
        "*Afficher + alerte* : affich\xC3\xA9, et d\xC3\xA8s qu'il passe sous le seuil *Alerter sous* son nom et son minuteur *clignotent* (recast bient\xC3\xB4t) ; une ligne rouge \"OUT\" le remplace ensuite d\xC3\xA8s qu'il tombe ou est dispel, jusqu'\xC3\xA0 ce que tu le relances."},
    {2, "*Hide* : never shown. Silence.",
        "*Masquer* : jamais affich\xC3\xA9. Silence."},
    {2, "*Hide + alert* : hidden while healthy, but *appears and blinks* once it drops under the *Alert under* threshold, or shows a red \"OUT\" when it falls / is dispelled. The alert holds for the *Alert duration* then clears itself if you don't re-cast. Both are sliders in the *Alerts* section. Zero clutter, zero misses.",
        "*Masquer + alerte* : cach\xC3\xA9 tant que tout va bien, mais *appara\xC3\xAEt et clignote* d\xC3\xA8s qu'il passe sous le seuil *Alerter sous*, ou affiche un \"OUT\" rouge quand il tombe / est dispel. L'alerte tient le temps r\xC3\xA9gl\xC3\xA9 par *Dur\xC3\xA9""e de l'alerte* puis s'efface si tu ne relances pas. Les deux sont des curseurs dans la section *Alertes*. Z\xC3\xA9ro encombrement, z\xC3\xA9ro oubli."},

    {0, "Job Abilities by job", "Aptitudes par job"},
    {1, "Job Abilities don't fit a magic family, so they get their own list *grouped by job* \xE2\x80\x94 your *current job* first, in *gold* and expanded, every other job under its own collapsible header. The setting is still global per buff : set an ability once and it applies wherever it shows up, whatever job you are on.",
        "Les aptitudes de job n'entrent pas dans une famille de magie, alors elles ont leur propre liste *group\xC3\xA9""e par job* \xE2\x80\x94 ton *job actuel* en premier, en *dor\xC3\xA9* et d\xC3\xA9pli\xC3\xA9, chaque autre job sous son propre en-t\xC3\xAAte repliable. Le r\xC3\xA9glage reste global par buff : r\xC3\xA8gle une aptitude une fois et elle s'applique partout o\xC3\xB9 elle appara\xC3\xAEt, quel que soit ton job."},

    {0, "Focus alerts, no false alarms", "Alertes focus, sans faux positif"},
    {2, "*Cast a buff on the wrong person and you can take it back.* Every watched row carries a small number on its left ; type *//aio out <number>* and that line disappears \xE2\x80\x94 name, timer and its future red alert with it. The buff itself is untouched : what goes is our decision to watch it. The number belongs to the *row*, not to its position, so it is still the right one by the time you have finished typing.",
        "*Un buff lanc\xC3\xA9 sur la mauvaise personne peut \xC3\xAAtre annul\xC3\xA9.* Chaque ligne suivie porte un petit num\xC3\xA9ro \xC3\xA0 gauche ; tape *//aio out <num\xC3\xA9ro>* et la ligne dispara\xC3\xAEt \xE2\x80\x94 nom, minuteur et sa future alerte rouge avec. Le buff, lui, n'est pas touch\xC3\xA9 : ce qui part, c'est la d\xC3\xA9""cision de le surveiller. Le num\xC3\xA9ro appartient \xC3\xA0 la *ligne*, pas \xC3\xA0 sa position, donc c'est encore le bon quand tu as fini de taper."},
    {2, "*//aio out <name>* or *<spell>* works just as well, both together in either order (*//aio out gab haste*). *//aio out list* prints everything being watched, numbers included \xE2\x80\x94 the one way to reach a row the column is not showing (clipped by Max per column, or hidden by a filter).",
        "*//aio out <nom>* ou *<sort>* marche aussi bien, les deux ensemble dans l'ordre que tu veux (*//aio out gab haste*). *//aio out list* affiche tout ce qui est suivi, num\xC3\xA9ros compris \xE2\x80\x94 le seul moyen d'atteindre une ligne que la colonne ne montre pas (coup\xC3\xA9""e par Max par colonne, ou cach\xC3\xA9""e par un filtre)."},
    {2, "*//aio out alerts* removes only the *red* rows \xE2\x80\x94 the buffs that have just run out \xE2\x80\x94 and leaves every running timer alone. That is the everyday one, after a wipe or a long fight. *//aio out all* clears the lot, red or not.",
        "*//aio out alertes* ne retire que les lignes *rouges* \xE2\x80\x94 les buffs qui viennent d'expirer \xE2\x80\x94 et laisse tourner tous les minuteurs. C'est celle du quotidien, apr\xC3\xA8s une wipe ou un long combat. *//aio out all* efface tout, rouge ou pas."},
    {2, "*A line is removed for that one cast, not forever.* If the buff ends, the row is forgotten for good and *no red alert fires* \xE2\x80\x94 which is exactly why you removed it. If you *cast that spell on that person again*, the row comes back watched : that cast you meant, so its expiry is worth knowing. It comes back even when they still carried the buff \xE2\x80\x94 Haste overwrites itself and never lapses. A cast that does not land (no effect, resisted) changes nothing and leaves the row off.",
        "*Une ligne est retir\xC3\xA9""e pour ce cast-l\xC3\xA0, pas pour toujours.* Si le buff se termine, la ligne est oubli\xC3\xA9""e pour de bon et *aucune alerte rouge* ne se d\xC3\xA9""clenche \xE2\x80\x94 c'est pr\xC3\xA9""cis\xC3\xA9ment pour \xC3\xA7""a que tu l'as retir\xC3\xA9""e. Si tu *relances ce sort sur cette personne*, la ligne revient sous surveillance : ce cast-l\xC3\xA0 tu l'as voulu, donc son expiration t'int\xC3\xA9resse. Elle revient m\xC3\xAAme si la personne avait toujours le buff \xE2\x80\x94 Haste s'\xC3\xA9""crase lui-m\xC3\xAAme et ne tombe jamais. Un cast qui ne prend pas (sans effet, r\xC3\xA9sist\xC3\xA9) ne change rien et laisse la ligne retir\xC3\xA9""e."},
    {2, "*//aio in* puts back every line you removed, right now ; a number or a name puts back just one (*//aio in 3*, *//aio in gab*). A row that is still running comes back with its real remaining time \xE2\x80\x94 removing a line never touched the timer.",
        "*//aio in* remet tout ce que tu as retir\xC3\xA9, tout de suite ; un num\xC3\xA9ro ou un nom n'en remet qu'une (*//aio in 3*, *//aio in gab*). Une ligne qui tourne encore revient avec son temps restant r\xC3\xA9""el \xE2\x80\x94 retirer une ligne n'a jamais touch\xC3\xA9 au minuteur."},
    {2, "An alert fires *only* for a buff the HUD actually saw active then lost, and it obeys the same *Buff source* filter as the rows \xE2\x80\x94 so a buff the column hides never alerts either.",
        "Une alerte se d\xC3\xA9""clenche *uniquement* pour un buff que le HUD a r\xC3\xA9""ellement vu actif puis perdu, et elle suit le m\xC3\xAAme filtre *Source des buffs* que les lignes \xE2\x80\x94 un buff que la colonne masque ne d\xC3\xA9""clenche donc jamais d'alerte."},
    {2, "Change job (you or an ally) and the timers reset cleanly \xE2\x80\x94 except SP abilities, whose recast is shared across all jobs.",
        "Change de job (toi ou un alli\xC3\xA9) et les timers se r\xC3\xA9initialisent proprement \xE2\x80\x94 sauf les aptitudes SP, dont le recast est partag\xC3\xA9 entre tous les jobs."},
    {2, "An ally leaving the party, or a zone change, purges their focus alerts automatically.",
        "Un alli\xC3\xA9 qui quitte la party, ou un changement de zone, purge ses alertes focus automatiquement."},

    {0, "Grouping ally buffs", "Grouper les buffs alli\xC3\xA9s"},
    {1, "Single-target buffs (Haste, Protect) you spread over several allies show *one row per person* -- which is also the only form that can name who is missing one. A row folds into a single \"(AoE N)\" line only when the cast really did hit N people: a song, Protectra, a COR roll, or a spell made area-wide by *SCH Accession / Manifestation*. Rows are ordered *what you cast first, then buffs with no known caster, then other players, then trusts*, and finally AoE groups and each ally. Within every block, the soonest to expire comes first.",
        "Les buffs monocibles (Haste, Protect) que tu r\xC3\xA9partis sur plusieurs alli\xC3\xA9s s'affichent *une ligne par personne* \xE2\x80\x94 la seule forme qui puisse aussi te dire QUI n'en a pas. Une ligne ne se replie en un \"(AoE N)\" unique que si le cast a r\xC3\xA9""ellement touch\xC3\xA9 N personnes : une chanson, un Protectra, un roll COR, ou un sort pass\xC3\xA9 en zone par *SCH Accession / Manifestation*. Les lignes sont ordonn\xC3\xA9""es *ce que tu as lanc\xC3\xA9 d'abord, puis les buffs sans lanceur connu, puis les autres joueurs, puis les trusts*, et enfin les groupes AoE et chaque alli\xC3\xA9. Dans chaque bloc, le plus proche d'expirer passe en premier."},
    {2, "A *grouped* row is about several people at once, so it names none of them and follows *Each buff shows* on its own \xE2\x80\x94 in *Icon* it is one icon, with the \"(AoE N)\" mark riding along with the name.",
        "Une ligne *group\xC3\xA9""e* concerne plusieurs personnes \xC3\xA0 la fois : elle n'en nomme aucune et suit *Chaque buff montre* de bout en bout \xE2\x80\x94 en *Ic\xC3\xB4ne*, c'est une ic\xC3\xB4ne seule, la marque \"(AoE N)\" partant avec le nom."},
    {2, "An area ENHANCING buff you cast (Protect / Regen under SCH Accession, PLD Majesty) keeps its group and countdown across a *zone change* ; songs and rolls drop on a zone, as in game.",
        "Un buff d'AM\xC3\x89LIORATION de zone que tu lances (Protect / Regen sous Accession SCH, Majesty PLD) garde son groupe et son d\xC3\xA9""compte \xC3\xA0 travers un *changement de zone* ; les chants et rolls disparaissent au zone, comme en jeu."},
    {2, "The game never sends buff durations for *other* players \xE2\x80\x94 only your own \xE2\x80\x94 so a timer on an ally is an *estimate* : exact when the buff also lands on you (an AoE), and within a second or two otherwise.",
        "Le jeu n'envoie jamais les dur\xC3\xA9""es de buff des *autres* joueurs \xE2\x80\x94 seulement les tiennes \xE2\x80\x94 donc un minuteur sur un alli\xC3\xA9 est un *estim\xC3\xA9* : exact quand le buff te touche aussi (un AoE), et \xC3\xA0 une ou deux secondes pr\xC3\xA8s sinon."},

    {0, "Buff source", "Source des buffs"},
    {1, "The *Buff source* filter picks whose buffs the Duration column keeps : *Mine* only, *Mine + players*, *Mine + trusts*, or *All* (the default). Your own are always kept ; it cleanly drops a trust's Haste or a stat boost you didn't cast. Anything you did not cast carries its owner's name in parentheses.",
        "Le filtre *Source des buffs* choisit quels buffs la colonne Duration garde : *Moi* seul, *Moi + joueurs*, *Moi + trusts*, ou *Tout* (par d\xC3\xA9""faut). Les tiens sont toujours gard\xC3\xA9s ; il retire proprement le Haste d'un trust ou un stat boost que tu n'as pas lanc\xC3\xA9. Tout ce que tu n'as pas lanc\xC3\xA9 porte le nom de son propri\xC3\xA9taire entre parenth\xC3\xA8ses."},
    {2, "Buffs you carry yourself with no caster \xE2\x80\x94 food, the conquest Signet family, Aftermath, crafting Imagery \xE2\x80\x94 are never dropped by *Mine only* ; their own Buff-filter toggle controls them, and very long ones (a 13-hour Signet) still show.",
        "Les buffs que tu portes toi-m\xC3\xAAme sans lanceur \xE2\x80\x94 nourriture, la famille Signet de conqu\xC3\xAAte, Aftermath, l'Imagery d'artisanat \xE2\x80\x94 ne sont jamais retir\xC3\xA9s par *Moi seul* ; c'est leur propre interrupteur dans le Filtre de buffs qui les contr\xC3\xB4le, et les tr\xC3\xA8s longs (un Signet de 13 heures) s'affichent quand m\xC3\xAAme."},

    {0, "Who cast what", "Qui a lanc\xC3\xA9 quoi"},
    {1, "Several spells can share one internal effect id \xE2\x80\x94 every march is *Enhances Attack*, every minuet *Enhances Attack Speed*. The panel resolves each row to the *real* spell and the *real* caster, so your Honor March and a Trust's Victory March are two distinct rows with the right names and the right owners, even when they expire in the same second.",
        "Plusieurs sorts peuvent partager un m\xC3\xAAme identifiant d'effet interne \xE2\x80\x94 toutes les marches, tous les menuets. Le panneau r\xC3\xA9sout chaque ligne vers le *vrai* sort et le *vrai* lanceur, donc ton Honor March et la Victory March d'un trust sont deux lignes distinctes, avec les bons noms et les bons propri\xC3\xA9taires, m\xC3\xAAme si elles expirent \xC3\xA0 la m\xC3\xAAme seconde."},
    {2, "Song modifier tags (*SV*, *NT*, *TR*, *M*) reflect the *singer's* job abilities at cast time \xE2\x80\x94 on your own songs and on another player's or a trust's alike.",
        "Les marqueurs de chants (*SV*, *NT*, *TR*, *M*) refl\xC3\xA8tent les aptitudes du *chanteur* au moment de l'incantation \xE2\x80\x94 sur tes propres chants comme sur ceux d'un autre joueur ou d'un trust."},
    {2, "When a buff cannot be traced to anyone \xE2\x80\x94 food, gear, an effect the game never announced \xE2\x80\x94 the row stays, simply without an owner. The panel never guesses a caster, because a wrong guess could hide a buff you actually have.",
        "Quand un buff ne peut \xC3\xAAtre rattach\xC3\xA9 \xC3\xA0 personne \xE2\x80\x94 nourriture, \xC3\xA9quipement, un effet que le jeu n'annonce pas \xE2\x80\x94 la ligne reste, simplement sans propri\xC3\xA9taire. Le panneau ne devine jamais un lanceur : une erreur pourrait masquer un buff que tu as r\xC3\xA9""ellement."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Timers module to tune it against a sample, or buff up and blow a few recasts to watch it live. Drag each box in //aio edit.",
        "Ouvre //aio config sur le module Timers pour le r\xC3\xA9gler sur un exemple, ou buff-toi et lance quelques sorts pour le voir en direct. D\xC3\xA9place chaque cadre avec //aio edit."},
};
static const int HELP_TIMERS_N = (int)(sizeof(HELP_TIMERS) / sizeof(HELP_TIMERS[0]));

// SKILLCHAINS : the live resonance on your target -- property, window timer, step + continuation weapon skills.
// Live sample (kind 43) draws the REAL box in preview mode via skillchains_help_box, config-aware (lines / theme).
static const HelpItem HELP_SKILLCHAINS[] = {
    {0, "Skillchains", "Skillchains"},
    {1, "*Skillchains* reads the resonance on your target and shows you, live, the open skillchain : the element to magic burst, a countdown window, the weapon skills that would continue it, and whether your TP is up. When you or your party open a chain, the box lights up so you know exactly what to feed it and when.",
        "*Skillchains* lit la r\xC3\xA9sonance sur ta cible et t'affiche, en direct, le skillchain ouvert : l'\xC3\xA9l\xC3\xA9ment \xC3\xA0 magic burst, une fen\xC3\xAAtre de d\xC3\xA9""compte, les weapon skills qui le prolongeraient, et si tes TP sont pr\xC3\xAAts. Quand toi ou ta party ouvrez un chain, le cadre s'allume pour savoir exactement quoi encha\xC3\xAEner et quand."},
    {43, "", ""},

    {0, "Reading the box", "Lire le cadre"},
    {2, "The *title* names the box. The *timer* line reads *Wait* (red), then *Go!* (green) \xE2\x80\x94 it flips a touch early so a weapon skill you start still lands with normal server lag. A closing *Burst* counts down precisely, fading green through yellow to red as it ends.",
        "Le *titre* nomme le cadre. La ligne *timer* affiche *Wait* (rouge), puis *Go!* (vert) \xE2\x80\x94 elle bascule un peu en avance pour qu'une weapon skill lanc\xC3\xA9""e \xC3\xA0 ce moment atterrisse malgr\xC3\xA9 la latence serveur normale. Un *Burst* de fermeture d\xC3\xA9""compte pr\xC3\xA9""cis\xC3\xA9ment, virant du vert au jaune puis au rouge en finissant."},
    {2, "The *Step* line shows the chain length and the move that opened it, *Step: 2 -> Savage Blade* for example.",
        "La ligne *Step* montre la longueur du chain et le coup qui l'a ouvert, *Step: 2 -> Savage Blade* par exemple."},
    {2, "Once the chain forms, the *Nuke:* line shows the burst *elements* it opens \xE2\x80\x94 each a colour *pip* plus its name \xE2\x80\x94 so a nuker reads the element to magic burst at a glance. Before it forms, it shows the active property in its element colour instead.",
        "Une fois le chain form\xC3\xA9, la ligne *Nuke:* montre les *\xC3\xA9l\xC3\xA9ments* de burst qu'il ouvre \xE2\x80\x94 chacun une *pastille* de couleur et son nom \xE2\x80\x94 un nukeur lit l'\xC3\xA9l\xC3\xA9ment \xC3\xA0 magic burst d'un coup d'oeil. Avant qu'il se forme, elle montre la propri\xC3\xA9t\xC3\xA9 active dans sa couleur d'\xC3\xA9l\xC3\xA9ment."},
    {2, "The *continuation list* lines up the weapon skills that would extend the chain, each with its level, the property it would make, and colour *pips* for the burst elements that property opens \xE2\x80\x94 so you pick the next move, and its element, at a glance.",
        "La *liste de continuation* aligne les weapon skills qui prolongeraient le chain, chacune avec son niveau, la propri\xC3\xA9t\xC3\xA9 qu'elle ferait, et des *pastilles* de couleur pour les \xC3\xA9l\xC3\xA9ments de burst que cette propri\xC3\xA9t\xC3\xA9 ouvre \xE2\x80\x94 pour choisir le prochain coup, et son \xC3\xA9l\xC3\xA9ment, d'un coup d'oeil."},
    {2, "A *TP gauge* at the bottom fills toward 1000 and turns *green* the moment you can weapon skill, so you know the listed moves are actually doable now.",
        "Une *jauge de TP* en bas se remplit vers 1000 et passe au *vert* d\xC3\xA8s que tu peux weapon skill, pour savoir si les coups list\xC3\xA9s sont r\xC3\xA9""ellement faisables maintenant."},
    {2, "Detection covers *SCH Immanence* elemental spells \xE2\x80\x94 they open and continue chains \xE2\x80\x94 and *Aeonic* weapons : while you wield one with the matching Aftermath its Radiance / Umbra close is offered, and a look-alike non-Aeonic never is.",
        "La d\xC3\xA9tection couvre les sorts \xC3\xA9l\xC3\xA9mentaires sous *Immanence* (SCH) \xE2\x80\x94 ils ouvrent et prolongent les chains \xE2\x80\x94 et les armes *Aeonic* : tant que tu en portes une avec l'Aftermath correspondant sa fermeture Radiance / Umbra est propos\xC3\xA9""e, et une arme non-Aeonic qui y ressemble ne l'est jamais."},

    {0, "The window", "La fen\xC3\xAAtre"},
    {1, "After a skill opens the chain there is a short *Wait*, then the *Go! / Burst* window during which the next weapon skill, or a magic burst, lands on the resonance. Miss the window and it fades and the box clears.",
        "Apr\xC3\xA8s qu'un coup ouvre le chain il y a un court *Wait*, puis la fen\xC3\xAAtre *Go! / Burst* pendant laquelle la weapon skill suivante, ou un magic burst, tombe sur la r\xC3\xA9sonance. Rate la fen\xC3\xAAtre et elle s'estompe, le cadre se vide."},

    {0, "Configuration", "Configuration"},
    {1, "The Skillchains module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Skillchains dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* : *Show*, *Style* (*Circle*, the default : a sphere with its weapon skill list around it, or *Classic* : the text box described above), *Preview* (what the preview plays : an opening move or any skillchain), *Sphere size* (Circle) or *Size* (Classic), and *Grows toward*.",
        "*Affichage* : *Afficher*, *Style* (*Cercle*, le d\xC3\xA9""faut : une sph\xC3\xA8re avec sa liste de weapon skills autour, ou *Classique* : le cadre de texte d\xC3\xA9""crit plus haut), *Aper\xC3\xA7u* (ce que joue l'aper\xC3\xA7u : une WS d'ouverture ou n'importe quel skillchain), *Taille de la sph\xC3\xA8re* (Cercle) ou *Taille* (Classique), et *Grandit vers*."},
    {2, "In Simple, *Elements* toggles each part on its own : the *Title*, the *Timer (Go! / Burst)*, the *Step line* (Classic), the *Property + elements* (Classic) or the *Burst element icons* (Circle), the *Weaponskill list*, the *TP indicator* and the *WS spacing*. In Advanced each part sits in its own section : *Sphere* (Circle : *Title* and its *Title height*, *Timer (Go! / Burst)*, *Burst element icons*, *TP indicator*) or *Lines* (Classic : *Title*, *Timer (Go! / Burst)*, *Step line*, *Property + elements*, *TP indicator*).",
        "En Simple, *\xC3\x89l\xC3\xA9ments* active chaque partie s\xC3\xA9par\xC3\xA9ment : le *Titre*, le *Timer (Go! / Burst)*, la *Ligne Step* (Classique), la *Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments* (Classique) ou les *Ic\xC3\xB4nes des \xC3\xA9l\xC3\xA9ments* (Cercle), la *Liste weaponskills*, l'*Indicateur TP* et l'*Espacement WS*. En Avanc\xC3\xA9 chaque partie a sa section : *Sph\xC3\xA8re* (Cercle : *Titre* et sa *Hauteur du titre*, *Timer (Go! / Burst)*, *Ic\xC3\xB4nes des \xC3\xA9l\xC3\xA9ments*, *Indicateur TP*) ou *Lignes* (Classique : *Titre*, *Timer (Go! / Burst)*, *Ligne Step*, *Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments*, *Indicateur TP*)."},
    {2, "*Weaponskill list* (Advanced) : on the Circle, its *Layout* is Off, *Rows*, *Tree*, *Cards* or *Grouped*, then *Weapon skills* (*My weapon* or *All my job's weapons*), *List size*, *Columns*, *Card size*, *WS spacing*, *List position*, *List distance*, *Element icons in the list* and the *List icons size* ; on Classic, the *Weaponskill list* on or off, *Weapon skills* and *WS spacing*.",
        "*Liste des WS* (Avanc\xC3\xA9) : sur le Cercle, sa *Disposition* est D\xC3\xA9sactiv\xC3\xA9, *Lignes*, *Arbre*, *Cartes* ou *Group\xC3\xA9""e*, puis *WS propos\xC3\xA9""es* (*Mon arme* ou *Toutes les armes du job*), *Taille de la liste*, *Colonnes*, *Taille des cartes*, *Espacement WS*, *Position de la liste*, *Distance de la liste*, *Ic\xC3\xB4nes d'\xC3\xA9l\xC3\xA9ment dans la liste* et la *Taille des ic\xC3\xB4nes (liste)* ; en Classique, la *Liste des WS* activ\xC3\xA9""e ou non, *WS propos\xC3\xA9""es* et *Espacement WS*."},
    {2, "*Behaviour* : *Show party/nearby chains* also shows a party member's chain on your battle target or a nearby mob, not only your cursor target, so a supporter still sees the chain to burst. On the Circle, it adds *Animations*, *Scene distortion*, *Particles* and *Effect reach*.",
        "*Comportement* : *Afficher SC du groupe/proches* montre aussi le chain d'un membre de party sur ta cible de combat ou un mob proche, pas seulement ta cible curseur, pour qu'un support voie quand m\xC3\xAAme le chain \xC3\xA0 burst. Sur le Cercle, il ajoute *Animations*, *D\xC3\xA9""formation du d\xC3\xA9""cor*, *Particules* et *Port\xC3\xA9""e des effets*."},
    {2, "*WS / Magic Burst popup* (Advanced, Circle) flashes your weapon skill and your magic bursts next to the sphere : *Popup* is Off, *Your weaponskill*, *Your Magic Burst* or *Both*, then the *Popup style*, the *Font*, the *Text size*, *Impact effects*, its *Position*, *Move left / right*, *Move up / down*, the *Name line height* and the *Damage height* ; for a burst, the *\"MAGIC BURST\" line* and the *Burst spell name* ; for your WS, its *Colour* (WS name, Damage A, Damage B). One weapon skill shows one popup : while this one is on, the centre-screen *Arcade WS* popup stands down.",
        "*Popup WS / Magic Burst* (Avanc\xC3\xA9, Cercle) affiche ta weapon skill et tes magic bursts \xC3\xA0 c\xC3\xB4t\xC3\xA9 de la sph\xC3\xA8re : *Popup* vaut D\xC3\xA9sactiv\xC3\xA9, *Ta WS*, *Ton Magic Burst* ou *Les deux*, puis le *Style du popup*, la *Police*, la *Taille du texte*, *Effets d'impact*, sa *Position*, *D\xC3\xA9placer gauche / droite*, *D\xC3\xA9placer haut / bas*, la *Hauteur de la ligne du nom* et la *Hauteur des d\xC3\xA9g\xC3\xA2ts* ; pour un burst, la *Ligne \"MAGIC BURST\"* et le *Nom du sort burst\xC3\xA9* ; pour ta WS, sa *Couleur* (Nom de la WS, D\xC3\xA9g\xC3\xA2ts A, D\xC3\xA9g\xC3\xA2ts B). Une weapon skill donne un seul popup : tant que celui-ci est actif, le popup *Arcade WS* du centre de l'\xC3\xA9""cran s'efface."},
    {2, "The *Frame* section (Advanced) is the Classic box's, or on the Circle the frame of its weapon skill list : *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*. *Text* (Advanced) styles the Title, Timer, Step (Classic) and the list each on its own.",
        "La section *Cadre* (Avanc\xC3\xA9) est celui du cadre Classique, ou sur le Cercle celui de sa liste de WS : *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*. *Texte* (Avanc\xC3\xA9) stylise le Titre, le Timer, le Step (Classique) et la liste chacun s\xC3\xA9par\xC3\xA9ment."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Skillchains module to tune it against a looping sample, or open a real chain on your target to see it live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Skillchains pour le r\xC3\xA9gler sur un exemple qui boucle, ou ouvre un vrai chain sur ta cible pour le voir en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_SKILLCHAINS_N = (int)(sizeof(HELP_SKILLCHAINS) / sizeof(HELP_SKILLCHAINS[0]));

// ABSORB-TP : who in the party / alliance cast Absorb-TP, how much it drained, how long ago, how many (hud_absorb.cpp).
static const HelpItem HELP_ABSORB[] = {
    {0, "Absorb-TP", "Absorb-TP"},
    {1, "The *Absorb-TP* box follows every Absorb-TP cast in your party and alliance : who cast it, how much TP it drained, how long ago, and how many they landed. It is made for keeping a TP-heavy fight in check, where each caster has to keep the spell going.",
        "Le cadre *Absorb-TP* suit chaque Absorb-TP lanc\xC3\xA9 dans ta party et ton alliance : qui l'a lanc\xC3\xA9, combien de TP il a drain\xC3\xA9, il y a combien de temps, et combien il en a plac\xC3\xA9. Il sert \xC3\xA0 tenir un combat o\xC3\xB9 le mob vit de ses TP, quand chaque lanceur doit garder le sort en rotation."},

    {0, "Reading the box", "Lire le cadre"},
    {2, "One row per caster. The drained TP is the big coloured number : white, then green, yellow, orange and red the more TP it took, and *blue* when it drained nothing (resisted or no effect).",
        "Une ligne par lanceur. Les TP drain\xC3\xA9s sont le gros nombre en couleur : blanc, puis vert, jaune, orange et rouge plus il en a pris, et *bleu* quand il n'a rien drain\xC3\xA9 (r\xC3\xA9sist\xC3\xA9 ou sans effet)."},
    {2, "The time since the last cast burns down from green through yellow to *red*. A row turns red once that player goes *Red alert after* seconds without a new Absorb-TP.",
        "Le temps depuis le dernier cast passe du vert au jaune puis au *rouge*. Une ligne passe en rouge quand ce joueur reste *Alerte rouge apr\xC3\xA8s* secondes sans nouvel Absorb-TP."},
    {2, "The *Animated list* style can carry a beacon with the last cast, on top or at the bottom ; the *Table (classic)* style is a plain table.",
        "Le style *Liste anim\xC3\xA9""e* peut porter un rep\xC3\xA8re avec le dernier cast, en haut ou en bas ; le style *Tableau (classique)* est un simple tableau."},

    {0, "Configuration", "Configuration"},
    {2, "*Display* : *Show*, *Style* (*Animated list* or *Table (classic)*), *Size* and *Grows toward*.",
        "*Affichage* : *Afficher*, *Style* (*Liste anim\xC3\xA9""e* ou *Tableau (classique)*), *Taille* et *Grandit vers*."},
    {2, "In Simple, *Content* holds the *Layout* of the animated list (*Beacon on top*, *Beacon at the bottom* or *List only*), *Colour the TP*, *Time since*, *Absorb count* and the two times below.",
        "En Simple, *Contenu* contient la *Disposition* de la liste anim\xC3\xA9""e (*Rep\xC3\xA8re en haut*, *Rep\xC3\xA8re en bas* ou *Liste seule*), *Couleur des TP*, *Temps \xC3\xA9""coul\xC3\xA9*, *Nombre d'Absorb* et les deux dur\xC3\xA9""es plus bas."},
    {2, "In Advanced, *Animated list* (that style only) : its *Layout*, the *Text size*, the *Orb size* (with a beacon), the job *Badges* (None, Job or Initials) and their *Badge size*, the *Absorb animation* and its *Scene distortion*. *Caster rows* (both styles) : *Colour the TP*, *Time since* and *Absorb count*.",
        "En Avanc\xC3\xA9, *Liste anim\xC3\xA9""e* (ce style seulement) : sa *Disposition*, la *Taille du texte*, la *Taille de l'orbe* (avec un rep\xC3\xA8re), les *Badges* de job (Aucun, Job ou Initiales) et leur *Taille des badges*, l'*Animation d'absorption* et sa *D\xC3\xA9""formation du d\xC3\xA9""cor*. *Lignes des lanceurs* (les deux styles) : *Couleur des TP*, *Temps \xC3\xA9""coul\xC3\xA9* et *Nombre d'Absorb*."},
    {2, "*Behaviour* (Advanced ; in Simple, under *Content*) : *Red alert after* sets the seconds without a new Absorb-TP before a row turns red. *Keep & count over* sets how far back the count looks, and how long a row stays at all. The memory can never be shorter than the alert : moving one pushes the other.",
        "*Comportement* (Avanc\xC3\xA9 ; en Simple, dans *Contenu*) : *Alerte rouge apr\xC3\xA8s* r\xC3\xA8gle les secondes sans nouvel Absorb-TP avant qu'une ligne passe en rouge. *Garder et compter sur* r\xC3\xA8gle jusqu'o\xC3\xB9 le compte remonte, et combien de temps une ligne reste tout court. La m\xC3\xA9moire ne peut jamais \xC3\xAAtre plus courte que l'alerte : bouger l'une pousse l'autre."},
    {2, "The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Absorb-TP module to tune it against an animated sample. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Absorb-TP pour le r\xC3\xA9gler sur un exemple anim\xC3\xA9. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_ABSORB_N = (int)(sizeof(HELP_ABSORB) / sizeof(HELP_ABSORB[0]));

// GRIMOIRE (SCH) : the Light / Dark Arts book, the Addendum aura, the stratagem charges + recast (hud_grimoire.cpp).
static const HelpItem HELP_GRIMOIRE[] = {
    {0, "Grimoire (SCH)", "Grimoire (SCH)"},
    {1, "The *Grimoire* is a small book for Scholars. It only appears when SCH is your main or sub job, and shows which Arts you are in and where your stratagems stand.",
        "Le *Grimoire* est un petit livre pour les Scholars. Il n'appara\xC3\xAEt que si SCH est ton job principal ou ton sub, et montre dans quels Arts tu es et o\xC3\xB9 en sont tes stratag\xC3\xA8mes."},

    {0, "Reading the book", "Lire le livre"},
    {2, "The book is gold under *Light Arts* and purple under *Dark Arts*. Without any Arts it stays closed, with no stratagem display.",
        "Le livre est dor\xC3\xA9 sous *Light Arts* et violet sous *Dark Arts*. Sans aucun Arts il reste ferm\xC3\xA9, sans affichage de stratag\xC3\xA8mes."},
    {2, "Under *Addendum: White* or *Addendum: Black* a soft aura breathes around the book.",
        "Sous *Addendum: White* ou *Addendum: Black* une aura douce respire autour du livre."},
    {2, "The left page carries your stratagem *Charge* count, the right page the *Timer* to the next one.",
        "La page de gauche porte ton nombre de *Charge* de stratag\xC3\xA8mes, la page de droite le *Timer* avant la suivante."},

    {0, "Configuration", "Configuration"},
    {2, "*Display* : *Show*, *Size* and *Grows toward*. *Preview image (config only)* picks the book the preview shows (Light Arts, Dark Arts, Addendum: White or Addendum: Black) ; it changes nothing in game.",
        "*Affichage* : *Afficher*, *Taille* et *Grandit vers*. *Image de l'aper\xC3\xA7u (config seulement)* choisit le livre que montre l'aper\xC3\xA7u (Light Arts, Dark Arts, Addendum: White ou Addendum: Black) ; \xC3\xA7""a ne change rien en jeu."},
    {2, "*Text* (Advanced) styles the Charge and the Timer, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise la Charge et le Timer, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Grimoire (SCH) module to tune it against a sample book. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Grimoire (SCH) pour le r\xC3\xA9gler sur un livre d'exemple. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_GRIMOIRE_N = (int)(sizeof(HELP_GRIMOIRE) / sizeof(HELP_GRIMOIRE[0]));

// ARCADE WS : the centre-screen weaponskill popup (hud.cpp draw_ws_popup).
static const HelpItem HELP_ARCADE[] = {
    {0, "Arcade WS", "Arcade WS"},
    {1, "*Arcade WS* flashes your weapon skill in the middle of the screen when you land it : its name and its total damage pop in, pulse, and fade out, like a fighting-game combo.",
        "*Arcade WS* affiche ta weapon skill au milieu de l'\xC3\xA9""cran quand tu la places : son nom et ses d\xC3\xA9g\xC3\xA2ts totaux apparaissent, pulsent puis s'effacent, comme un combo de jeu de combat."},
    {1, "One weapon skill shows one popup. While the Skillchains sphere (*Circle* style) shows it with its own *WS / Magic Burst popup*, this centre popup stands down.",
        "Une weapon skill donne un seul popup. Tant que la sph\xC3\xA8re des Skillchains (style *Cercle*) l'affiche avec son propre *Popup WS / Magic Burst*, ce popup central s'efface."},

    {0, "Configuration", "Configuration"},
    {2, "*Display* : *Show* and *Size*. *Effects* : *Impact effects*, the burst, rings and glow behind the text.",
        "*Affichage* : *Afficher* et *Taille*. *Effets* : *Effets d'impact*, l'\xC3\xA9""clat, les anneaux et la lueur derri\xC3\xA8re le texte."},
    {2, "*Text* (Advanced) : the *Font*, then the *Colour* of the *Name* and of the damage, which flashes between *Damage A* and *Damage B* (pick one, then set it in the picker).",
        "*Texte* (Avanc\xC3\xA9) : la *Police*, puis la *Couleur* du *Nom* et des d\xC3\xA9g\xC3\xA2ts, qui clignotent entre *D\xC3\xA9g\xC3\xA2ts A* et *D\xC3\xA9g\xC3\xA2ts B* (choisis-en une, puis r\xC3\xA8gle-la dans le s\xC3\xA9lecteur de couleur)."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Arcade WS module to watch a looping sample. In //aio edit, drag the popup to place it and roll the wheel over it to resize.",
        "Ouvre //aio config sur le module Arcade WS pour voir un exemple en boucle. Dans //aio edit, glisse le popup pour le placer et utilise la molette dessus pour le redimensionner."},
};
static const int HELP_ARCADE_N = (int)(sizeof(HELP_ARCADE) / sizeof(HELP_ARCADE[0]));

// TREASURE POOL : the party lottery pool -- coffer icon + one row per item (index / name / expiry timer / lotter).
// Live sample (kind 44) draws the REAL box in preview mode via treasure_help_box, config-aware (icon / theme / text).
static const HelpItem HELP_TREASURE[] = {
    {0, "Treasure Pool", "Tr\xC3\xA9sor"},
    {1, "*Treasure Pool* shows the party's lottery pool at a glance : every item waiting to be lotted, the time left before it drops, and who is winning it right now. No more opening the game menu to see what is up for grabs.",
        "*Tr\xC3\xA9sor* montre le pool de loterie de la party d'un coup d'oeil : chaque objet en attente de lot, le temps restant avant qu'il tombe, et qui le remporte \xC3\xA0 l'instant. Fini d'ouvrir le menu du jeu pour voir ce qui est \xC3\xA0 gagner."},
    {44, "", ""},

    {0, "Reading a row", "Lire une ligne"},
    {1, "Each row is one item in the pool, read left to right.",
        "Chaque ligne est un objet du pool, lu de gauche \xC3\xA0 droite."},
    {2, "The *index* numbers the rows shown, from 1 upward, in order of time left -- it is a display counter, not the game's own pool slot.",
        "L'*index* num\xC3\xA9rote les lignes affich\xC3\xA9""es, \xC3\xA0 partir de 1, dans l'ordre du temps restant -- c'est un compteur d'affichage, pas la place r\xC3\xA9""elle de l'objet dans le pool du jeu."},
    {2, "The *name* is the item itself.",
        "Le *nom* est l'objet lui-m\xC3\xAAme."},
    {2, "The *timer* counts down the roughly five minutes before the item drops out of the pool, going *green* (over 3 min) to *orange* (1-3 min) to *red* (under a minute) as it runs out.",
        "Le *timer* d\xC3\xA9""compte les cinq minutes environ avant que l'objet quitte le pool, passant du *vert* (plus de 3 min) \xC3\xA0 l'*orange* (1-3 min) puis au *rouge* (moins d'une minute) \xC3\xA0 mesure que le temps s'\xC3\xA9puise."},
    {2, "The *Top lot* on the right is whoever currently holds the highest lot, with their score, so you know whether to lot or pass.",
        "Le *Meilleur lot* \xC3\xA0 droite est celui qui a le plus haut lot pour l'instant, avec son score, pour savoir s'il faut loter ou passer."},

    {0, "The coffer", "Le coffre"},
    {1, "An optional treasure-chest icon sits on the left of the box, a clear cue that the pool has something in it. Turn it off to keep the box compact.",
        "Une ic\xC3\xB4ne de coffre optionnelle est \xC3\xA0 gauche du cadre, un rep\xC3\xA8re clair que le pool contient quelque chose. Coupe-la pour garder le cadre compact."},

    {0, "Configuration", "Configuration"},
    {1, "The Treasure Pool module in //aio config tunes the box, with a live preview on the right that follows your changes.",
        "Le module Tr\xC3\xA9sor dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite qui suit tes changements."},
    {2, "*Display* : *Show*, *Size* (scales the whole box) and *Grows toward*. *Item list* : *Max items* caps how many rows show at once, *Coffer icon* shows or hides the chest. The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "*Affichage* : *Afficher*, *Taille* (met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle) et *Grandit vers*. *Liste des items* : *Items max* limite le nombre de lignes affich\xC3\xA9""es, *Ic\xC3\xB4ne coffre* affiche ou masque le coffre. La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},
    {2, "*Text* (Advanced) styles the Index, Name, Timer and Top lot each on its own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise l'Index, le Nom, le Timer et le Meilleur lot chacun s\xC3\xA9par\xC3\xA9ment, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Treasure Pool module to tune it against a sample pool, or fight for drops to see it fill live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Tr\xC3\xA9sor pour le r\xC3\xA9gler sur un pool d'exemple, ou combats pour des drops pour le voir se remplir en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_TREASURE_N = (int)(sizeof(HELP_TREASURE) / sizeof(HELP_TREASURE[0]));

// ZONE TRACKER : a chameleon box that follows the special content you are in (Dynamis / Abyssea / Omen / Nyzul /
// Odyssey). Live sample (kind 45) draws the REAL box in preview mode via zonetracker_help_box (your chosen variant).
static const HelpItem HELP_ZONETRACKER[] = {
    {0, "Zone Tracker", "Zone Tracker"},
    {1, "*Zone Tracker* is a chameleon box that follows the special content you are in and shows exactly what matters there. It appears on its own when you enter that content and reshapes to fit, then folds away when you leave.",
        "*Zone Tracker* est un cadre cam\xC3\xA9l\xC3\xA9on qui suit le contenu sp\xC3\xA9""cial o\xC3\xB9 tu es et montre exactement ce qui compte l\xC3\xA0. Il appara\xC3\xAEt seul quand tu entres dans ce contenu et se reforme pour s'adapter, puis se range quand tu pars."},
    {45, "", ""},

    {0, "What it tracks", "Ce qu'il suit"},
    {2, "*Dynamis* : your time-extension granules and a run timer counting your time in the zone.",
        "*Dynamis* : tes granules d'extension de temps et un timer de run qui d\xC3\xA9""compte ton temps dans la zone."},
    {2, "*Abyssea* : the seven lights (Pearl, Azure, Ruby, Amber, Gold, Silver, Ebon) and your visitant time left.",
        "*Abyssea* : les sept lumi\xC3\xA8res (Pearl, Azure, Ruby, Amber, Gold, Silver, Ebon) et ton temps de visitant restant."},
    {2, "*Omen* : your bonus objectives and their progress, with the omen count and the bonus timer.",
        "*Omen* : tes objectifs bonus et leur progression, avec le compteur d'omens et le timer de bonus."},
    {2, "*Nyzul Isle* : the current floor, the floor timer, your objective and restriction, floors cleared, and a live token estimate.",
        "*Nyzul Isle* : l'\xC3\xA9tage actuel, le timer d'\xC3\xA9tage, ton objectif et ta restriction, les \xC3\xA9tages faits, et une estimation de tokens en direct."},
    {2, "*Sheol* (Odyssey) : the Mog Segments you have banked this run, plus resistances, the NM family and any Cruel Joke.",
        "*Sheol* (Odyssey) : les Mog Segments accumul\xC3\xA9s durant ce run, plus les r\xC3\xA9sistances, la famille du NM et l'\xC3\xA9ventuel Cruel Joke."},
    {2, "*Limbus* : the area and its level, the floor with its progress gauge, your Temenos / Apollyon units, what this run has banked and how many data collections are left this week -- plus one dot per quadrant coffer (dim = not opened, red = 3k, green = 5k).",
        "*Limbus* : la zone et son niveau, l'\xC3\xA9tage et sa jauge de progression, tes units Temenos / Apollyon, ce que ce run a rapport\xC3\xA9 et le nombre de collectes restantes cette semaine -- plus une pastille par coffre de quadrant (\xC3\xA9teinte = pas ouvert, rouge = 3k, verte = 5k)."},
    {2, "*Sortie* : your Gallimaufry, the bosses and their shards, the coffers and the NM, the items obtained, the wing's NM / bitzer, and an automatic Wide Scan track.",
        "*Sortie* : ta Gallimaufry, les boss et leurs shards, les coffres et le NM, les objets obtenus, le NM / bitzer de l'aile, et un suivi Wide Scan automatique."},

    {0, "Configuration", "Configuration"},
    {1, "The Zone Tracker module in //aio config tunes the box, with a live preview on the right.",
        "Le module Zone Tracker dans //aio config r\xC3\xA8gle le cadre, avec un aper\xC3\xA7u en direct \xC3\xA0 droite."},
    {2, "In Simple, *Display* holds the *Preview zone*, *Show*, *Size* and *Grows toward*. The *Preview zone* picks which zone to work on, so you can style and place the box even when you are not in that zone.",
        "En Simple, *Affichage* contient la *Zone de l'aper\xC3\xA7u*, *Afficher*, *Taille* et *Grandit vers*. La *Zone de l'aper\xC3\xA7u* choisit la zone sur laquelle tu travailles, pour styliser et placer le cadre m\xC3\xAAme quand tu n'es pas dans cette zone."},
    {2, "In Advanced, *Display* and *Frame* are the box's, the same in every zone : *Show*, *Size*, *Grows toward* and *Title (zone name)* ; *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*. Then a zone switch (*Dynamis | Abyssea | Omen | Nyzul | Sheol | Limbus | Sortie*) picks the zone you set : it drives the preview AND the sections below it.",
        "En Avanc\xC3\xA9, *Affichage* et *Cadre* sont ceux du cadre, les m\xC3\xAAmes dans toutes les zones : *Afficher*, *Taille*, *Grandit vers* et *Titre (nom de la zone)* ; *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*. Puis un s\xC3\xA9lecteur de zone (*Dynamis | Abyssea | Omen | Nyzul | Sheol | Limbus | Sortie*) choisit la zone que tu r\xC3\xA8gles : il pilote l'aper\xC3\xA7u ET les sections en dessous."},
    {2, "*Every zone* is configurable row by row, in a section named after it : Dynamis (run timer, key items), Abyssea (visitant timer, lights), Omen (floor objective, omens + bonus, objective rows), Nyzul (floor, floor timer, objective, restriction, floors cleared, reward rate, tokens), Sheol (segments, resistances, family name, Cruel Joke), Limbus (area & level, floor on the gauge, currencies, run total, coffer dots) and Sortie (Gallimaufry, bosses and shards, coffers and NM, items obtained, wing NM / bitzer). Sortie adds a *Behaviour* section : *Auto Widescan track (wing NM)*.",
        "*Chaque zone* se r\xC3\xA8gle ligne par ligne, dans une section \xC3\xA0 son nom : Dynamis (timer de run, objets cl\xC3\xA9s), Abyssea (timer visitant, lumi\xC3\xA8res), Omen (objectif d'\xC3\xA9tage, omens + bonus, lignes d'objectifs), Nyzul (\xC3\xA9tage, timer d'\xC3\xA9tage, objectif, restriction, \xC3\xA9tages faits, taux de r\xC3\xA9""compense, tokens), Sheol (segments, r\xC3\xA9sistances, nom de famille, Cruel Joke), Limbus (zone et niveau, \xC3\xA9tage sur la jauge, monnaies, total du run, pastilles de coffres) et Sortie (Gallimaufry, boss et shards, coffres et NM, objets obtenus, NM / bitzer de l'aile). Sortie ajoute une section *Comportement* : *Track Widescan auto (NM de l'aile)*."},
    {2, "*Text* styles each of those rows on its own : the element list follows the zone switch and what is shown, so every zone exposes exactly its own text elements (font, size, outline, bold / italic / caps and colour).",
        "*Texte* stylise chacune de ces lignes s\xC3\xA9par\xC3\xA9ment : la liste d'\xC3\xA9l\xC3\xA9ments suit le s\xC3\xA9lecteur de zone et ce qui est affich\xC3\xA9, donc chaque zone expose exactement ses propres \xC3\xA9l\xC3\xA9ments de texte (police, taille, contour, gras / italique / capitales et couleur)."},
    {2, "*Bars and gauges are dimensionable* : the Dynamis / Abyssea timer bar width and height, the Dynamis key-item dot size, the Abyssea light-bar width and height, the Sheol weapon-icon and element-dot sizes, and the Limbus gauge width and height.",
        "*Les barres et jauges sont dimensionnables* : largeur et hauteur de la barre de timer Dynamis / Abyssea, taille des pastilles d'objets cl\xC3\xA9s Dynamis, largeur et hauteur des barres de lumi\xC3\xA8res Abyssea, taille des ic\xC3\xB4nes d'armes et des pastilles d'\xC3\xA9l\xC3\xA9ments Sheol, et largeur / hauteur de la jauge Limbus."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the Zone Tracker module and pick a zone (*Preview zone* in Simple, the zone switch in Advanced) to tune it, or just enter the content to see it live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module Zone Tracker et choisis une zone (*Zone de l'aper\xC3\xA7u* en Simple, le s\xC3\xA9lecteur de zone en Avanc\xC3\xA9) pour le r\xC3\xA9gler, ou entre simplement dans le contenu pour le voir en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
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
    {2, "*Content* opens on a hunt board : the full Abyssea list in two columns, one row per NM with its lamp, its name, its *Pops* (how many pop items) and its *Collect.* (the collectable's target, - when it has none). Click a row to track that NM : its lamp lights up and the box turns on.",
        "*Contenu* s'ouvre sur un tableau de chasse : la liste compl\xC3\xA8te d'Abyssea en deux colonnes, une ligne par NM avec sa lampe, son nom, ses *Pops* (combien d'items de pop) et son *Collect.* (l'objectif du collectable, - quand il n'en a pas). Clique une ligne pour suivre ce NM : sa lampe s'allume et le cadre s'active."},
    {2, "*Display* : *Show*, *Size* (scales the whole box) and *Grows toward*. Under the board, *Collectable* shows or hides the collectable progress row. The *Frame* section (Advanced) sets *Box*, *Border*, *Transparency*, *Theme*, *Box theme* and *Luminosity*.",
        "*Affichage* : *Afficher*, *Taille* (met tout le cadre \xC3\xA0 l'\xC3\xA9""chelle) et *Grandit vers*. Sous le tableau, *Collectable* affiche ou masque la ligne de progression. La section *Cadre* (Avanc\xC3\xA9) r\xC3\xA8gle *Cadre*, *Bordure*, *Transparence*, *Th\xC3\xA8me*, *Th\xC3\xA8me de cadre* et *Luminosit\xC3\xA9*."},
    {2, "*Text* (Advanced) styles the Title, Pop, From and Collectable each on its own, with font, size, outline, bold / italic / caps and colour.",
        "*Texte* (Avanc\xC3\xA9) stylise le Titre, le Pop, le De et le Collectable chacun s\xC3\xA9par\xC3\xA9ment, avec police, taille, contour, gras / italique / capitales et couleur."},

    {0, "Preview", "Aper\xC3\xA7u"},
    {1, "Open //aio config on the EmpyPop module to tune it against a sample chain, then turn Show on to track your NM live. Drag it anywhere in //aio edit.",
        "Ouvre //aio config sur le module EmpyPop pour le r\xC3\xA9gler sur une cha\xC3\xAene d'exemple, puis active Afficher pour suivre ton NM en direct. D\xC3\xA9place-le o\xC3\xB9 tu veux avec //aio edit."},
};
static const int HELP_EMPYPOP_N = (int)(sizeof(HELP_EMPYPOP) / sizeof(HELP_EMPYPOP[0]));

// UPDATE : how to keep AioHud current from the Update tab (user-facing, no dev jargon).
static const HelpItem HELP_UPDATE[] = {
    {0, "Updating AioHud", "Mettre \xC3\xA0 jour AioHud"},
    {1, "AioHud updates itself, right in the game. Open the *Update* tab at the top of this window to see your installed version and whether a newer one is available.",
        "AioHud se met \xC3\xA0 jour tout seul, directement en jeu. Ouvre l'onglet *Mise \xC3\xA0 jour* en haut de cette fen\xC3\xAAtre pour voir ta version install\xC3\xA9""e et si une plus r\xC3\xA9""cente existe."},
    {1, "It checks automatically each time the plugin loads, so the Update tab is already up to date when you open it.",
        "Il v\xC3\xA9rifie automatiquement \xC3\xA0 chaque chargement du plugin, donc l'onglet est d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour quand tu l'ouvres."},

    {0, "One click", "En un clic"},
    {1, "When an update is available, press *Update now*. AioHud downloads it and reloads itself : the HUD blinks off for a second or two, then comes back on the new version. No window, nothing to download by hand.",
        "Quand une mise \xC3\xA0 jour est dispo, appuie sur *Mettre \xC3\xA0 jour*. AioHud la t\xC3\xA9l\xC3\xA9""charge et se recharge : le HUD dispara\xC3\xAEt une seconde ou deux, puis revient sur la nouvelle version. Aucune fen\xC3\xAAtre, rien \xC3\xA0 t\xC3\xA9l\xC3\xA9""charger \xC3\xA0 la main."},
    {2, "Your settings and profiles are always kept \xE2\x80\x94 an update never touches them.",
        "Tes r\xC3\xA9glages et profils sont toujours conserv\xC3\xA9s \xE2\x80\x94 une mise \xC3\xA0 jour n'y touche jamais."},
    {2, "The //aioupdate command does exactly the same thing.",
        "La commande //aioupdate fait exactement la m\xC3\xAAme chose."},

    {0, "Dual-boxing", "Dual-box"},
    {1, "If you run two clients, unload AioHud on the OTHER client first (//unload AioHud), then update on this one. Every client has to release the plugin before it can be swapped.",
        "Si tu lances deux clients, d\xC3\xA9""charge AioHud sur l'AUTRE client d'abord (//unload AioHud), puis mets \xC3\xA0 jour sur celui-ci. Chaque client doit lib\xC3\xA9rer le plugin avant qu'il puisse \xC3\xAAtre remplac\xC3\xA9."},
};
static const int HELP_UPDATE_N = (int)(sizeof(HELP_UPDATE) / sizeof(HELP_UPDATE[0]));

static const HelpItem HELP_NUI[] = {
    {0, "The game's own windows", "Les fen\xC3\xAAtres du jeu"},
    {1, "AioHUD can redraw the game's own windows on your theme, or take them out of the way when one of its modules replaces them. Open //aio config, *Game UI*.",
        "AioHUD peut redessiner les fen\xC3\xAAtres du jeu sur ton th\xC3\xA8me, ou les retirer quand un de ses modules les remplace. Ouvre //aio config, *Interface du jeu*."},
    {1, "Every redrawn window is a *Modern / FFXI* choice : *Modern* is drawn by AioHUD, *FFXI* keeps the game's own. *Game interface*, at the top of the page, switches them all at once (it reads *Mixed* when they differ).",
        "Chaque fen\xC3\xAAtre redessin\xC3\xA9""e est un choix *Moderne / FFXI* : *Moderne* est dessin\xC3\xA9""e par AioHUD, *FFXI* garde celle du jeu. *Interface du jeu*, en haut de la page, les bascule toutes d'un coup (elle affiche *Mixte* quand elles diff\xC3\xA8rent)."},
    {49, "", ""},

    {0, "What can be redrawn", "Ce qui peut \xC3\xAAtre redessin\xC3\xA9"},
    {2, "*Menus and screens* : the *Main menu* and every screen it opens (status, equipment, items, magic, map, Region Info, macros, missions, friend list...), the *Action menu* (Enter on a target), the *Top bar (menu name + help)* and the *Yes / No confirms*.",
        "*Menus et \xC3\xA9""crans* : le *Menu principal* et tous les \xC3\xA9""crans qu'il ouvre (statut, \xC3\xA9quipement, objets, magie, carte, Region Info, macros, missions, liste d'amis...), le *Menu d'action* (Entr\xC3\xA9""e sur une cible), la *Barre du haut (nom du menu + aide)* et les *Confirmations Oui / Non*."},
    {2, "*NPCs and trade* : the NPCs' dialogue and answers, the shops, the auction house, the delivery box, and the *Trade (player / NPC)* window.",
        "*PNJ et \xC3\xA9""changes* : les dialogues et r\xC3\xA9ponses des PNJ, les boutiques, l'h\xC3\xB4tel des ventes, la bo\xC3\xAEte de livraison, et la fen\xC3\xAAtre d'*\xC3\x89""change (joueur / PNJ)*."},
    {2, "*On screen* : the *Macro bar (Ctrl / Alt)*, *Your buff icons (top)* and the *Connection box (top right)*.",
        "*\xC3\x80 l'\xC3\xA9""cran* : la *Barre de macros (Ctrl / Alt)*, *Tes ic\xC3\xB4nes de buff (en haut)* et la *Box connexion (en haut \xC3\xA0 droite)*."},
    {1, "In Advanced, *Display* sets the *Menu size*, the *Main menu position*, which way a *Taller submenu grows* and the *Menu rows* (Text only or Icon + text), and *Frame* gives every redrawn menu one theme.",
        "En Avanc\xC3\xA9, *Affichage* r\xC3\xA8gle la *Taille des menus*, la *Position du menu principal*, le sens o\xC3\xB9 un *Sous-menu plus grand : grandit* et les *Lignes des menus* (Texte seul ou Ic\xC3\xB4ne + texte), et *Cadre* donne un seul th\xC3\xA8me \xC3\xA0 tous les menus redessin\xC3\xA9s."},

    {0, "Names over the characters", "Noms au-dessus des persos"},
    {1, "*Style* *Modern* draws the names above players and NPCs itself, hidden by walls like the game's ; *FFXI* keeps the game's names. With Modern you set the *Font*, the *Text size*, the *Names size* (*With the distance*, or *Fixed (thin, as far)*) and the *Weight*. Advanced adds *Italic*, *CAPS*, the *Icon size (pearl...)*, the *Icon gap before the name*, the *Master Level stars* with their *Stars height* and *Stars spread*. The preview follows live while this section is open.",
        "*Style* *Moderne* dessine lui-m\xC3\xAAme les noms au-dessus des joueurs et des PNJ, cach\xC3\xA9s par les murs comme ceux du jeu ; *FFXI* garde les noms du jeu. En Moderne tu r\xC3\xA8gles la *Police*, la *Taille du texte*, la *Taille des noms* (*Selon la distance*, ou *Fixe (fin, comme au loin)*) et la *Graisse*. Avanc\xC3\xA9 ajoute *Italique*, *Majuscules*, la *Taille des ic\xC3\xB4nes (perle...)*, l'*\xC3\x89""cart ic\xC3\xB4ne / nom*, les *\xC3\x89toiles du Master Level* avec leur *Hauteur des \xC3\xA9toiles* et leur *\xC3\x89""cartement des \xC3\xA9toiles*. L'aper\xC3\xA7u suit en direct tant que cette section est ouverte."},

    {0, "Game windows replaced by AioHUD", "Fen\xC3\xAAtres du jeu remplac\xC3\xA9""es par AioHUD"},
    {1, "These windows are not redrawn : an AioHUD module takes their place. The game's party and alliance windows follow AioHUD's boxes on their own : hidden while those show. The *Chat log* is *Shown* or *Hidden*, your choice.",
        "Ces fen\xC3\xAAtres ne sont pas redessin\xC3\xA9""es : un module AioHUD prend leur place. Les fen\xC3\xAAtres de groupe et d'alliance du jeu suivent d'elles-m\xC3\xAAmes les bo\xC3\xAEtes AioHUD : masqu\xC3\xA9""es tant qu'elles s'affichent. Le *Journal du chat* est *Affich\xC3\xA9""e* ou *Cach\xC3\xA9""e*, \xC3\xA0 toi de choisir."},
    {2, "Each stays shown while its AioHUD module is off, so you are never left without one. A hidden window keeps working, you just no longer see it.",
        "Chacune reste affich\xC3\xA9""e tant que son module AioHUD est d\xC3\xA9sactiv\xC3\xA9, pour ne jamais te laisser sans. Une fen\xC3\xAAtre cach\xC3\xA9""e fonctionne toujours, tu ne la vois juste plus."},

    {0, "Behaviour", "Comportement"},
    {1, "In Advanced, *Move the menus : horizontal* and *Move the menus : vertical* shift every game menu at once, even the ones you never opened ; *Reset* puts them back at the game's place.",
        "En Avanc\xC3\xA9, *D\xC3\xA9placer les menus : horizontal* et *D\xC3\xA9placer les menus : vertical* d\xC3\xA9""calent tous les menus du jeu d'un coup, m\xC3\xAAme ceux jamais ouverts ; *R\xC3\xA9initialiser* les remet \xC3\xA0 la place du jeu."},
    {2, "Nothing is permanent : choosing FFXI or Shown, or unloading AioHUD, puts every window back. If AioHUD cannot read a window, the game's own one stays.",
        "Rien n'est d\xC3\xA9""finitif : choisir FFXI ou Affich\xC3\xA9""e, ou d\xC3\xA9""charger AioHUD, remet chaque fen\xC3\xAAtre en place. Si AioHUD ne peut pas lire une fen\xC3\xAAtre, celle du jeu reste."},
};
static const int HELP_NUI_N = (int)(sizeof(HELP_NUI) / sizeof(HELP_NUI[0]));

static const HelpItem HELP_CHAT[] = {
    {0, "AioHUD's own chat", "Le chat d'AioHUD"},
    {1, "Two boxes on your theme : *Chat* (talk, NPC, system) and *Battle* (the combat log). Turn them on in //aio config, *Chat*.",
        "Deux box sur ton th\xC3\xA8me : *Discussion* (conversations, PNJ, syst\xC3\xA8me) et *Combat* (le journal de combat). Active-les dans //aio config, *Chat*."},
    {48, "", ""},
    {1, "Click a *tab* (its icon) to filter : Chat (everything) / Party / Linkshell / Tell / Say / System, or Battle / Me in the battle box. The open tab spells its name ; a number on an icon counts the messages that tab has and the open one does not show.",
        "Clique un *onglet* (son ic\xC3\xB4ne) pour filtrer : Discussion (tout) / Groupe / Linkshell / Tell / Dire / Syst\xC3\xA8me, ou Combat / Moi dans la box combat. L'onglet ouvert affiche son nom ; un nombre sur une ic\xC3\xB4ne compte les messages que l'onglet ouvert ne montre pas."},
    {1, "The *wheel* over a box scrolls back through the history ; the pill at the bottom brings it back to the newest line. Clicks on a box never reach the game.",
        "La *molette* sur une box remonte l'historique ; la pastille en bas revient aux derni\xC3\xA8res lignes. Un clic sur une box n'atteint jamais le jeu."},
    {1, "In *Edit layout*, drag a box to move it, the wheel sets its size, and the corner grip its width and height (the text size decides how many lines fit).",
        "Dans *\xC3\x89""diter dispo*, glisse une box pour la d\xC3\xA9placer, la molette r\xC3\xA8gle sa taille, et la poign\xC3\xA9""e du coin sa largeur et sa hauteur (la taille du texte d\xC3\xA9""cide combien de lignes y tiennent)."},
    {2, "To replace the game's log, set *Chat log* to *Hidden* in *Game UI* > *Game windows replaced by AioHUD*. Its lines keep arriving here. It stays shown while both AioHUD chat boxes are off.",
        "Pour remplacer le journal du jeu, r\xC3\xA8gle *Journal du chat* sur *Cach\xC3\xA9""e* dans *Interface du jeu* > *Fen\xC3\xAAtres du jeu remplac\xC3\xA9""es par AioHUD*. Ses lignes continuent d'arriver ici. Il reste affich\xC3\xA9 tant que les deux box de chat d'AioHUD sont d\xC3\xA9sactiv\xC3\xA9""es."},
    {2, "*Max lines* caps how many lines a box keeps on screen ; at 0 the box keeps a fixed height.",
        "*Lignes max* limite le nombre de lignes qu'une box garde \xC3\xA0 l'\xC3\xA9""cran ; \xC3\xA0 0 la box garde une hauteur fixe."},
    {2, "Every setting of the page is for one box : in Simple, *Box edited below* picks it (*Chat* or *Battle*) ; in Advanced, the *Chat | Battle* switch at the top. *Display* : *Show*, *Size*, *Box height* (Fixed or Follows the content), *Grows toward*, *Width*, *Height* and *Max lines*. In Simple, *Content* holds the *Tabs*, *Header size*, *Density*, *Time stamps* and the *Input line* (the Chat box's only).",
        "Chaque r\xC3\xA9glage de la page vaut pour une box : en Simple, *Box r\xC3\xA9gl\xC3\xA9""e ci-dessous* la choisit (*Discussion* ou *Combat*) ; en Avanc\xC3\xA9, le s\xC3\xA9lecteur *Discussion | Combat* en haut. *Affichage* : *Afficher*, *Taille*, *Hauteur de la box* (Fixe ou Suit le contenu), *Grandit vers*, *Largeur*, *Hauteur* et *Lignes max*. En Simple, *Contenu* contient les *Onglets*, la *Taille de l'en-t\xC3\xAAte*, la *Densit\xC3\xA9*, l'*Horodatage* et la *Barre de saisie* (celle de la box Discussion seulement)."},
    {2, "Advanced splits it by element : *Frame*, *Tabs and header* (*Tabs* : Auto, Icon + name, Icon or Name ; *Header size*), *Messages* (*Density*, *Time stamps*, and on the Chat box the *Input line* : Game's place, Under the box or In the box), *History* (*Export the history*), *Banned players*, *Behaviour* (*Collapse when idle* with *Each line stays* and *Lines kept*, *Fade when idle* with *Fade after* and *Faded opacity*) and *Text* (with the *Auto-translate colour*, for both boxes).",
        "Avanc\xC3\xA9 la d\xC3\xA9""coupe par \xC3\xA9l\xC3\xA9ment : *Cadre*, *Onglets et en-t\xC3\xAAte* (*Onglets* : Auto, Ic\xC3\xB4ne + nom, Ic\xC3\xB4ne ou Nom ; *Taille de l'en-t\xC3\xAAte*), *Messages* (*Densit\xC3\xA9*, *Horodatage*, et sur la box Discussion la *Barre de saisie* : Place du jeu, Sous la box ou Dans la box), *Historique* (*Exporter l'historique*), *Joueurs bannis*, *Comportement* (*Repli au repos* avec *Chaque ligne reste* et *Lignes gard\xC3\xA9""es*, *Fondu au repos* avec *Fondu apr\xC3\xA8s* et *Opacit\xC3\xA9 au repos*) et *Texte* (avec la *Couleur des auto-translate*, pour les deux box)."},
    {0, "Copy, links, banning", "Copier, liens, bannir"},
    {1, "*Drag* over the text to select it. Nothing is copied yet : *right-click* and pick *Copy the selection*.",
        "*Glisse* sur le texte pour le s\xC3\xA9lectionner. Rien n'est encore copi\xC3\xA9 : *clic droit* puis *Copier la s\xC3\xA9lection*."},
    {1, "A *link* is drawn in blue : click it to open it in your browser or copy it.",
        "Un *lien* est en bleu : clique-le pour l'ouvrir dans ton navigateur ou le copier."},
    {1, "*Right-click* a line : copy the line, the whole message, the speaker's name or the link -- or *Ban* the speaker. A banned player's messages leave both boxes ; the list, with an *Unban* button on each name, is in //aio config, *Chat*, *Banned players* (Advanced).",
        "*Clic droit* sur une ligne : copier la ligne, le message entier, le nom de l'auteur ou le lien -- ou *Bannir* l'auteur. Les messages d'un joueur banni quittent les deux box ; la liste, avec un bouton *D\xC3\xA9""bannir* sur chaque nom, est dans //aio config, *Chat*, *Joueurs bannis* (Avanc\xC3\xA9)."},
    {1, "*History* -> *Export* writes the box's history to a text file in plugins\\AioHud\\exports (//aio chat export does the same).",
        "*Historique* -> *Exporter* \xC3\xA9""crit l'historique de la box dans un fichier texte, dans plugins\\AioHud\\exports (//aio chat export fait pareil)."},
};
static const int HELP_CHAT_N = (int)(sizeof(HELP_CHAT) / sizeof(HELP_CHAT[0]));

// One help page per module (the Help tab's left menu lists these ; add a module = add a row here).
struct HelpModule { const char* en; const char* fr; const HelpItem* items; int count; };
static const HelpModule HELP_MODULES[] = {
    { "General",          "G\xC3\xA9n\xC3\xA9ral",            HELP_GENERAL, HELP_GENERAL_N },
    { "Party / Alliance", "Groupe / Alliance",  HELP_PA,      HELP_PA_N },
    { "Target",           "Cible",              HELP_TARGET,  HELP_TARGET_N },
    { "Player",           "Joueur",             HELP_PLAYER,  HELP_PLAYER_N },
    { "Minimap",          "Minicarte",          HELP_MINIMAP, HELP_MINIMAP_N },
    { "Hate List",        "Liste de haine",     HELP_HATELIST, HELP_HATELIST_N },
    { "PointWatch",       "PointWatch",         HELP_POINTWATCH, HELP_POINTWATCH_N },
    { "Timers",           "Timers",             HELP_TIMERS,  HELP_TIMERS_N },
    { "Skillchains",      "Skillchains",        HELP_SKILLCHAINS, HELP_SKILLCHAINS_N },
    { "Treasure Pool",    "Tr\xC3\xA9sor",             HELP_TREASURE, HELP_TREASURE_N },
    { "Zone Tracker",     "Zone Tracker",       HELP_ZONETRACKER, HELP_ZONETRACKER_N },
    { "EmpyPop",          "EmpyPop",            HELP_EMPYPOP, HELP_EMPYPOP_N },
    { "Game UI",          "Interface du jeu",   HELP_NUI,     HELP_NUI_N },
    { "Chat",             "Chat",               HELP_CHAT,    HELP_CHAT_N },
    { "Update",           "Mise \xC3\xA0 jour",        HELP_UPDATE,  HELP_UPDATE_N },
    { "Interface",        "Interface",          HELP_INTERFACE, HELP_INTERFACE_N },
    { "Pet",              "Pet",                HELP_PET,     HELP_PET_N },
    { "Absorb-TP",        "Absorb-TP",          HELP_ABSORB,  HELP_ABSORB_N },
    { "Grimoire (SCH)",   "Grimoire (SCH)",     HELP_GRIMOIRE, HELP_GRIMOIRE_N },
    { "Arcade WS",        "Arcade WS",          HELP_ARCADE,  HELP_ARCADE_N },
};
static const int HELP_MODULE_N = (int)(sizeof(HELP_MODULES) / sizeof(HELP_MODULES[0]));
// HELP_MODULES' indices, by name : the Help tab's rail lists them in GROUPS (config_page.cpp), not in this order.
// Keep in step with the table above (a static_assert there counts the rail against HELP_MODULE_N).
enum { HELP_GENERAL_I, HELP_PA_I, HELP_TARGET_I, HELP_PLAYER_I, HELP_MINIMAP_I, HELP_HATE_I, HELP_PW_I, HELP_TIMERS_I,
       HELP_SC_I, HELP_TREASURE_I, HELP_ZT_I, HELP_EP_I, HELP_NUI_I, HELP_CHAT_I, HELP_UPDATE_I,
       HELP_INTERFACE_I, HELP_PET_I, HELP_ABSORB_I, HELP_GRIM_I, HELP_ARCADE_I, HELP_INDEX_N };
static_assert(HELP_INDEX_N == sizeof(HELP_MODULES) / sizeof(HELP_MODULES[0]), "HELP_*_I out of step with HELP_MODULES");
