// bst_pets_gen.h -- AUTO-GENERATED from addons/BST_JUG_PETS_DATABASE.md (do not edit).
// Regenerate: python scripts/gen_bst_pets.py -- the BST box PREVIEW only (each jug pet and its Ready moves).
#pragma once

namespace aio {

// eco : the pet's correlation family (the database's "Espece : Tiger (Beast)") ; moves : 0-terminated
struct BstPetRow { const char* name; const char* eco; const char* moves[8]; };
static const BstPetRow BST_PETS[] = {
    {"Acuex Familiar", "Amorph", {"Foul Waters", "Pestilent Plume", 0}},
    {"Aged Angus", "Aquan", {"Bubble Shower", "Bubble Curtain", "Big Scissors", "Scissor Guard", "Metallic Body", 0}},
    {"Alluring Honey", "Plantoid", {"Tickling Tendrils", "Stink Bomb", "Nectarous Deluge", "Nepenthic Plunge", 0}},
    {"Amiable Roche", "Aquan", {"Intimidate", "Recoil Dive", "Water Wall", 0}},
    {"Anklebiter Jedd", "Vermin", {"Double Claw", "Grapple", "Spinning Top", "Filamented Hold", 0}},
    {"Attentive Ibuki", "Bird", {"Molting Plumage", "Swooping Frenzy", "Pentapeck", 0}},
    {"Blackbeard Randy", "Beast", {"Roar", "Razor Fang", "Claw Cyclone", "Crossthrash", "Predatory Glare", 0}},
    {"Bouncing Bertha", "Vermin", {"Sensilla Blades", "Tegmina Buffet", 0}},
    {"Brainy Waluis", "Plantoid", {"Frogkick", "Spore", "Queasyshroom", "Numbshroom", "Shakeshroom", "Silence Gas", "Dark Spore", 0}},
    {"Caring Kiyomaro", "Beast", {"Sweeping Gouge", "Zealous Snort", 0}},
    {"Choral Leera", "Bird", {"Pecking Flurry", 0}},
    {"Colibri Familiar", "Bird", {"Pecking Flurry", 0}},
    {"Cursed Annabelle", "Vermin", {"Sandblast", "Sandpit", "Venom Spray", "Mandibular Bite", 0}},
    {"Daring Roland", "Bird", {"Back Heel", "Jettatura", "Choke Breath", "Fantod", "Hoof Volley", "Nihility Song", 0}},
    {"Droopy Dortwin", "Beast", {"Foot Kick", "Dust Cloud", "Whirl Claws", "Wild Carrot", 0}},
    {"Energized Sefina", "Vermin", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", "Rhinowrecker", 0}},
    {"Fleet Reinhard", "Lizard", {"Scythe Tail", "Ripper Fang", "Chomp Rush", 0}},
    {"Fluffy Bredo", "Amorph", {"Foul Waters", "Pestilent Plume", 0}},
    {"Generous Arthur", "Amorph", {"Purulent Ooze", "Corrosive Ooze", 0}},
    {"Gussy Hachirobe", "Vermin", {"Sickle Slash", "Acid Spray", "Spider Web", 0}},
    {"Headbreaker Ken", "Vermin", {"Cursed Sphere", "Venom", "Somersault", 0}},
    {"Hip. Familiar", "Bird", {"Back Heel", "Jettatura", "Choke Breath", "Fantod", "Hoof Volley", "Nihility Song", 0}},
    {"Hurler Percival", "Vermin", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", 0}},
    {"Jovial Edwin", "Aquan", {"Bubble Curtain", "Scissor Guard", "Metallic Body", "Venom Shower", "Mega Scissors", 0}},
    {"Left-Handed Yoko", "Vermin", {"Infected Leech", "Gloom Spray", 0}},
    {"Lynx Familiar", "Beast", {"Chaotic Eye", "Blaster", "Charged Whisker", "Frenzied Rage", 0}},
    {"Mosquito Familiar", "Vermin", {"Infected Leech", "Gloom Spray", 0}},
    {"P. Crab Familiar", "Aquan", {"Bubble Curtain", "Scissor Guard", "Metallic Body", "Venom Shower", "Mega Scissors", 0}},
    {"Pondering Peter", "Beast", {"Foot Kick", "Dust Cloud", "Whirl Claws", "Wild Carrot", 0}},
    {"Redolent Candi", "Plantoid", {"Tickling Tendrils", "Stink Bomb", "Nectarous Deluge", "Nepenthic Plunge", 0}},
    {"Rhyming Shizuna", "Beast", {"Lamb Chop", "Rage", "Sheep Charge", "Sheep Song", 0}},
    {"Scissorleg Xerin", "Vermin", {"Sensilla Blades", "Tegmina Buffet", 0}},
    {"Sharpwit Hermes", "Plantoid", {"Head Butt", "Wild Oats", "Leaf Dagger", "Scream", 0}},
    {"Slime Familiar", "Amorph", {"Fluid Toss", "Fluid Spread", "Digest", 0}},
    {"Spider Familiar", "Vermin", {"Sickle Slash", "Acid Spray", "Spider Web", 0}},
    {"Stalwart Angelina", "Vermin", {"Disembowel", "Extirpating Salvo", 0}},
    {"Submerged Iyo", "Bird", {"Wing Slap", "Beak Lunge", 0}},
    {"Sultry Patrice", "Amorph", {"Fluid Toss", "Fluid Spread", "Digest", 0}},
    {"Sunburst Malfik", "Aquan", {"Bubble Shower", "Bubble Curtain", "Big Scissors", "Scissor Guard", "Metallic Body", 0}},
    {"Surging Storm", "Bird", {"Wing Slap", "Beak Lunge", 0}},
    {"Suspicious Alice", "Lizard", {"Geist Wall", "Numbing Noise", "Nimble Snap", "Cyclotail", "Toxic Spit", 0}},
    {"Swooping Zhivago", "Bird", {"Molting Plumage", "Swooping Frenzy", "Pentapeck", 0}},
    {"Threestar Lynn", "Vermin", {"Sudden Lunge", "Spiral Spin", "Noisome Powder", 0}},
    {"Vivacious Gaston", "Beast", {"Chaotic Eye", "Blaster", "Charged Whisker", "Frenzied Rage", 0}},
    {"Vivacious Vickie", "Beast", {"Sweeping Gouge", "Zealous Snort", 0}},
    {"Warlike Patrick", "Lizard", {"Tail Blow", "Fireball", "Blockhead", "Brain Crush", "Infrasonics", "Secretion", 0}},
    {"Weevil Familiar", "Vermin", {"Disembowel", "Extirpating Salvo", 0}},
    {"Y. Beetle Familiar", "Vermin", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", "Rhinowrecker", 0}},
};
static const int BST_PETS_N = 48;

} // namespace aio
