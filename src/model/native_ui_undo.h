// native_ui_undo.h -- the Game UI's undo files : where each native window record really belongs, one file per client.
//
// native_ui moves the client's window records (their X / Y in game memory). The undo file names every record it moved
// and its true place, so a session that ended without a clean shutdown (a plugin crash, a reload that skipped it) can
// put them back at the next load. ONE file per process (data\native_ui_undo_<pid>.txt) : with one shared file, the
// second client of a multibox loaded, consumed or deleted the first one's file, and the first client's windows stayed
// off-screen (audit R4, 2026-11-23 ; the "the AH no longer popped" family).
//
// Pure file logic, no game memory : the caller hands in how to write a record and how to tell a live client, so the
// whole recovery runs offline (tests/t_undo.cpp).
#pragma once

namespace aio {
namespace undo {

// A line is "name|x|y" or "name|x|y|tries" ; a record still missing after this many recoveries is dropped (logged).
constexpr int MAX_TRIES = 5;

struct Hooks {
    // a client with that pid is running now (the caller compares the exe too : a pid reused by another program is dead)
    bool (*alive)(unsigned pid, void* u);
    // put the record `name` back at (x, y) ; false when it is not found or not writable
    bool (*apply)(const char* name, int x, int y, void* u);
    // one log line (rule 10 : every step says what happened)
    void (*log)(const char* line, void* u);
    void* u;
};

struct Result {
    int files;      // files read (own + dead clients ; the legacy shared file counts as dead)
    int put;        // records put back
    int missed;     // records not put back, kept for the next load
    int dropped;    // records given up after MAX_TRIES
    int cleaned;    // files deleted (every line put back or dropped)
    int skipped;    // files of a live OTHER client, left alone
};

// data\native_ui_undo_<pid>.txt under `dir` (no trailing backslash)
void path_for(char* out, int n, const char* dir, unsigned pid);

// The pid in a file name : "native_ui_undo_<pid>.txt" -> pid, the legacy "native_ui_undo.txt" -> 0 (a dead client).
// false for any other name.
bool pid_of(const char* fileName, unsigned& pid);

// Every undo file under `dir` this client should recover : its own (a reload without shutdown) and those of clients
// that no longer run. A live OTHER client's file is never touched. Missed lines are rewritten (tries + 1), the file
// deleted once empty.
Result recover_all(const char* dir, unsigned selfPid, const Hooks& h);

// true when recover_all would have something to do (an own or dead-client file exists)
bool pending(const char* dir, unsigned selfPid, const Hooks& h);

}  // namespace undo
}  // namespace aio
