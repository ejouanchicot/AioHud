// ui_config.cpp -- see ui_config.h.
#include "model/ui_config.h"
#include <cstdio>

namespace aio {

UiConfig& ui_config() { static UiConfig c; return c; }

static const char* CONFIG_PATH = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aio_config.txt";

void save_ui_config() {
    FILE* f = fopen(CONFIG_PATH, "w"); if (!f) return;
    UiConfig& c = ui_config();
    fprintf(f, "skinTheme=%d\n", c.skinTheme);
    fprintf(f, "fontFace=%d\n", c.fontFace);
    fprintf(f, "buffScale=%.4f\n", c.buffScale);
    for (int i = 0; i < 3; ++i)
        fprintf(f, "box%d=%d,%.5f,%.5f,%.4f\n", i, c.box[i].posSet ? 1 : 0, c.box[i].x, c.box[i].y, c.box[i].scale);
    fclose(f);
}

void load_ui_config() {
    FILE* f = fopen(CONFIG_PATH, "r"); if (!f) return;
    UiConfig& c = ui_config();
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        int v, ps, idx; float x, y, s, fv;
        if      (sscanf(line, "skinTheme=%d", &v) == 1) c.skinTheme = v;
        else if (sscanf(line, "fontFace=%d", &v) == 1)  c.fontFace = v;
        else if (sscanf(line, "buffScale=%f", &fv) == 1) c.buffScale = fv;
        else if (sscanf(line, "box%d=%d,%f,%f,%f", &idx, &ps, &x, &y, &s) == 5 && idx >= 0 && idx < 3) {
            c.box[idx].posSet = (ps != 0); c.box[idx].x = x; c.box[idx].y = y; c.box[idx].scale = s;
        }
    }
    fclose(f);
}

void reset_boxes() {   // edit-mode Default : positions + sizes only
    UiConfig& c = ui_config();
    for (int i = 0; i < 3; ++i) c.box[i] = BoxLayout();   // posSet=false, x=y=0, scale=1
    save_ui_config();
}

void reset_ui_config() {   // general Default : everything
    UiConfig& c = ui_config();
    c.skinTheme = 0; c.fontFace = 0; c.buffScale = 0.92f;
    reset_boxes();   // (also saves)
}

// index 0 = Default (keep the layout face). The rest are common Windows GDI faces.
static const char* FACE_LABEL[] = {
    "Default", "Segoe UI", "Arial", "Tahoma", "Verdana", "Calibri", "Trebuchet MS", "Consolas", "Georgia",
};
static const char* FACE_NAME[] = {
    "",        "Segoe UI", "Arial", "Tahoma", "Verdana", "Calibri", "Trebuchet MS", "Consolas", "Georgia",
};
static const int NFACE = (int)(sizeof(FACE_LABEL) / sizeof(FACE_LABEL[0]));

int         ui_font_count()        { return NFACE; }
const char* ui_font_label(int i)   { return (i >= 0 && i < NFACE) ? FACE_LABEL[i] : ""; }
const char* ui_font_face(int i)    { return (i >= 0 && i < NFACE) ? FACE_NAME[i]  : ""; }

} // namespace aio
