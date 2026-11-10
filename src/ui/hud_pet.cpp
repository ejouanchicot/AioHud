// hud_pet.cpp -- the Pet module : ONE box for the pet jobs, its content the main job's -- the Beastmaster's
// (hud_bst.cpp draw_bst), the Geomancer's (hud_geo.cpp draw_geo) or the Summoner's (hud_smn.cpp draw_smn) ; PUP gets its
// own content later. One
// place, one size, one frame for all of them (UiConfig petX / petY / petScale / petBox, GB_PET, EDITBOX_PET), so a job
// change changes what the box shows, never where it is. SPEC docs/design/pet-module-spec.md.
//
// Which content : in play the main job's (GameState bst / geo / smn .visible are set on that main job only : game_mem.cpp
// compute_bst / compute_geo / compute_smn) ; in the config preview, the tab chosen there (UiConfig::petTab) ; in //aio edit, the main
// job's, or the tab's on a job without a pet, so the box can be placed from any job.
#include "ui/hud.h"
#include "model/ui_config.h"
#include "model/gamestate.h"

namespace aio {

void Hud::draw_pet(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.petShow) return;
    const bool editing = C.editLayout && !preview;
    enum { NONE, BST, GEO, SMN } job = NONE;
    if (!preview && f.game) job = f.game->bst.visible ? BST : (f.game->geo.visible ? GEO : (f.game->smn.visible ? SMN : NONE));
    if (job == NONE && (preview || editing)) job = C.petTab == 2 ? SMN : (C.petTab == 1 ? GEO : BST);
    if (job == BST) draw_bst(f, preview, ovX, ovY, ovS);
    else if (job == GEO) draw_geo(f, preview, ovX, ovY, ovS);
    else if (job == SMN) draw_smn(f, preview, ovX, ovY, ovS);
}

} // namespace aio
