// bst_pets_gen.h -- AUTO-GENERATED from addons/BST_JUG_PETS_DATABASE.md (do not edit).
// Regenerate: python scripts/gen_bst_pets.py -- the BST box PREVIEW only (each jug pet and its Ready moves).
#pragma once

namespace aio {

struct BstPetRow { const char* name; const char* moves[8]; };   // moves : 0-terminated
static const BstPetRow BST_PETS[] = {
    {"Acuex Familiar", {"Foul Waters", "Pestilent Plume", 0}},
    {"Aged Angus", {"Bubble Shower", "Bubble Curtain", "Big Scissors", "Scissor Guard", "Metallic Body", 0}},
    {"Alluring Honey", {"Tickling Tendrils", "Stink Bomb", "Nectarous Deluge", "Nepenthic Plunge", 0}},
    {"Amiable Roche", {"Intimidate", "Recoil Dive", "Water Wall", 0}},
    {"Anklebiter Jedd", {"Double Claw", "Grapple", "Spinning Top", "Filamented Hold", 0}},
    {"Attentive Ibuki", {"Molting Plumage", "Swooping Frenzy", "Pentapeck", 0}},
    {"Blackbeard Randy", {"Roar", "Razor Fang", "Claw Cyclone", "Crossthrash", "Predatory Glare", 0}},
    {"Bouncing Bertha", {"Sensilla Blades", "Tegmina Buffet", 0}},
    {"Brainy Waluis", {"Frogkick", "Spore", "Queasyshroom", "Numbshroom", "Shakeshroom", "Silence Gas", "Dark Spore", 0}},
    {"Caring Kiyomaro", {"Sweeping Gouge", "Zealous Snort", 0}},
    {"Choral Leera", {"Pecking Flurry", 0}},
    {"Colibri Familiar", {"Pecking Flurry", 0}},
    {"Cursed Annabelle", {"Sandblast", "Sandpit", "Venom Spray", "Mandibular Bite", 0}},
    {"Daring Roland", {"Back Heel", "Jettatura", "Choke Breath", "Fantod", "Hoof Volley", "Nihility Song", 0}},
    {"Droopy Dortwin", {"Foot Kick", "Dust Cloud", "Whirl Claws", "Wild Carrot", 0}},
    {"Energized Sefina", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", "Rhinowrecker", 0}},
    {"Fleet Reinhard", {"Scythe Tail", "Ripper Fang", "Chomp Rush", 0}},
    {"Fluffy Bredo", {"Foul Waters", "Pestilent Plume", 0}},
    {"Generous Arthur", {"Purulent Ooze", "Corrosive Ooze", 0}},
    {"Gussy Hachirobe", {"Sickle Slash", "Acid Spray", "Spider Web", 0}},
    {"Headbreaker Ken", {"Cursed Sphere", "Venom", "Somersault", 0}},
    {"Hip. Familiar", {"Back Heel", "Jettatura", "Choke Breath", "Fantod", "Hoof Volley", "Nihility Song", 0}},
    {"Hurler Percival", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", 0}},
    {"Jovial Edwin", {"Bubble Curtain", "Scissor Guard", "Metallic Body", "Venom Shower", "Mega Scissors", 0}},
    {"Left-Handed Yoko", {"Infected Leech", "Gloom Spray", 0}},
    {"Lynx Familiar", {"Chaotic Eye", "Blaster", "Charged Whisker", "Frenzied Rage", 0}},
    {"Mosquito Familiar", {"Infected Leech", "Gloom Spray", 0}},
    {"P. Crab Familiar", {"Bubble Curtain", "Scissor Guard", "Metallic Body", "Venom Shower", "Mega Scissors", 0}},
    {"Pondering Peter", {"Foot Kick", "Dust Cloud", "Whirl Claws", "Wild Carrot", 0}},
    {"Redolent Candi", {"Tickling Tendrils", "Stink Bomb", "Nectarous Deluge", "Nepenthic Plunge", 0}},
    {"Rhyming Shizuna", {"Lamb Chop", "Rage", "Sheep Charge", "Sheep Song", 0}},
    {"Scissorleg Xerin", {"Sensilla Blades", "Tegmina Buffet", 0}},
    {"Sharpwit Hermes", {"Head Butt", "Wild Oats", "Leaf Dagger", "Scream", 0}},
    {"Slime Familiar", {"Fluid Toss", "Fluid Spread", "Digest", 0}},
    {"Spider Familiar", {"Sickle Slash", "Acid Spray", "Spider Web", 0}},
    {"Stalwart Angelina", {"Disembowel", "Extirpating Salvo", 0}},
    {"Submerged Iyo", {"Wing Slap", "Beak Lunge", 0}},
    {"Sultry Patrice", {"Fluid Toss", "Fluid Spread", "Digest", 0}},
    {"Sunburst Malfik", {"Bubble Shower", "Bubble Curtain", "Big Scissors", "Scissor Guard", "Metallic Body", 0}},
    {"Surging Storm", {"Wing Slap", "Beak Lunge", 0}},
    {"Suspicious Alice", {"Geist Wall", "Numbing Noise", "Nimble Snap", "Cyclotail", "Toxic Spit", 0}},
    {"Swooping Zhivago", {"Molting Plumage", "Swooping Frenzy", "Pentapeck", 0}},
    {"Threestar Lynn", {"Sudden Lunge", "Spiral Spin", "Noisome Powder", 0}},
    {"Vivacious Gaston", {"Chaotic Eye", "Blaster", "Charged Whisker", "Frenzied Rage", 0}},
    {"Vivacious Vickie", {"Sweeping Gouge", "Zealous Snort", 0}},
    {"Warlike Patrick", {"Tail Blow", "Fireball", "Blockhead", "Brain Crush", "Infrasonics", "Secretion", 0}},
    {"Weevil Familiar", {"Disembowel", "Extirpating Salvo", 0}},
    {"Y. Beetle Familiar", {"Power Attack", "Hi-Freq Field", "Rhino Attack", "Rhino Guard", "Spoil", "Rhinowrecker", 0}},
};
static const int BST_PETS_N = 48;

} // namespace aio
