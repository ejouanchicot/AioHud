// doctor_html.h -- //aio doctor, as a page you can look at instead of a log you have to read.
//
// The text report (model/selftest.cpp) answers "what is wrong". It cannot answer the question right behind it --
// "is what AioHUD reads about my character even right ?" -- because that needs the numbers themselves, side by
// side, with where each one came from. This writes them as one HTML file next to AioHud.dll, openable by
// double-click, no server and no internet: the gear icons are the game's own, referenced from the runtime
// folder the plugin already ships.
//
// EVERY VALUE CARRIES ITS SOURCE, and a value that has not arrived is written as "pas encore reçu" -- never as a
// zero. Those three packets only come on a login, a job change or a zone, so an empty sheet is a normal state
// and must read as one.
#pragma once

namespace aio {

struct GameState;

// Writes the page and returns the file name it used (a static buffer, valid until the next call), or 0 on
// failure. `lines` / `nd` are the doctor's own findings, so the page carries them too.
const char* write_doctor_html(const GameState& gs, const char* const* lines, int nd);

} // namespace aio
