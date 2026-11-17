// chat_log.h -- the game's chat, as AioHUD's own Chat / Battle boxes read it.
//
// Every line the client writes to its log reaches Windower's text_in callback with a MODE (the line's colour /
// channel : say 9, tell 12, party 13, linkshell 14, ... -- Windower res/chat.lua). text_in runs on its OWN thread
// (measured 2026-07-20), so a line is only COPIED into a fixed ring there (chat_queue) ; the main thread drains it
// (chat_drain), decodes the client's control bytes, classifies the channel and appends it to the history the boxes
// draw. Fixed capacity, no allocation, nothing synchronised but a single-producer / single-consumer ring.
//
// Decoding (the bytes FFXI puts in a log line) :
//   0x1E n      colour by palette index / key (1 = back to the line's colour) -> per-char code 0x100 | n
//   0x1F n      colour of chat MODE n (item names, emphasis)                  -> per-char code 0x200 | n
//   0x7F c ..   a marker : 2 bytes for c = 1 2 3 7 FB FC, 3 for 4 5 6, 4 for 8, else the 7F alone (FUN_05469997)
//   0xEF 0x27 / 0xEF 0x28   auto-translate brackets                           -> '{' / '}'
//   0xFD .. .. .. .. 0xFD   an auto-translate phrase id                       -> resolved by the caller later ; "{...}"
//   0x07, \n, \r  a line break inside one message (NPC dialogue)             -> separate history lines
//   Shift-JIS lead bytes 0x81..0x9F / 0xE0..0xFC + trail                    -> a few known glyphs ('->' ...), else '?'
//   85 9F..DE   the client's accented letters                               -> Latin-1 0xC0..0xFF (one byte)
#pragma once

namespace aio {

enum ChatChan {
    CH_SAY = 0, CH_SHOUT, CH_YELL, CH_TELL, CH_PARTY, CH_LS1, CH_LS2, CH_UNITY, CH_EMOTE, CH_ASSIST,
    CH_SYSTEM, CH_NPC, CH_BATTLE, CH_OTHER, CH_COUNT
};

static const int CHAT_TEXT = 240;   // chars kept per history line (a longer message wraps into several lines)
static const int CHAT_HIST = 4096;  // history lines kept (power of two) ; battle spam shares it -- 1024 left the talk only a few
                                     // minutes deep in the expanded log (2026-11-16, "je remonte, au plus loin divine emble") ; ~2.3 MB

struct ChatLine {
    unsigned       seq;              // 1, 2, 3 ... ; 0 = empty slot
    unsigned short mode;             // the raw Windower mode (masked 0x1FF)
    unsigned char  chan;             // ChatChan
    unsigned char  cont;             // 1 = continues the previous line's message (after a 0x07 break)
    unsigned short hhmm;             // local time hour*100 + minute when it arrived
    unsigned       arrivedTick;      // GetTickCount when it arrived (the box's collapse ages each line from it)
    // the SPEAKER, read from the client's own line format (first line of a message only ; see chat_parse_speaker) :
    char           chSpeaker[20];    // "" = no speaker (system, battle, emote, a continuation)
    unsigned char  chBodyAt;         // index in text where the MESSAGE starts (after "(Name) ", "[1]<Name> ", "Name>> " ...)
    unsigned char  chLsIdx;          // 1 / 2 = which linkshell ; 0 = not a linkshell line
    unsigned char  chTellOut;        // 1 = a tell YOU sent (">>Name ...")
    unsigned char  len;              // chars in text
    char           text[CHAT_TEXT];  // decoded : printable ASCII + Latin-1 letters 0xC0..0xFF (the client's accents)
    unsigned char  col[CHAT_TEXT];   // per char : 0 = the line's colour, k = pal[k - 1]
    unsigned       pal[8];           // the line's colour runs, as the CLIENT colours them (ARGB, full intensity ; 0 = base)
    unsigned       modeArgb;         // the client's colour of this line's MODE (ARGB ; 0 = unknown -> the channel palette). NOT "base" : the
                                     // replay's field map types members BY NAME across model headers -- a 2nd "base" broke sheet_.base
};

// TEXT thread : copy one incoming line (never blocks ; a full ring drops the line and counts it)
void chat_queue(const char* s, int mode);
// MAIN thread, once per frame : decode + append everything queued since the last call
void chat_drain();
// The CLIENT's own log, replayed once at load (chat_backlog.cpp) so a //reload does not start the boxes empty.
// chat_drain holds the live lines back until chat_seed_end : the backlog lands first, in order. chat_seed_line =
// one whole message as the client stored it (raw bytes, its mode, its hh*100+mm or -1). chat_seed_end(true) drops
// the lines queued meanwhile -- the backlog already carries them.
void chat_seed_line(const char* raw, int mode, int hhmm);
void chat_seed_end(bool tookQueued);
// MAIN thread, before chat_drain, once per frame : replays the client's log once (bounded wait for a character)
void chat_backlog_tick(const char* charName);

// history, oldest .. newest : i in [0, chat_count()) ; chat_newest_seq() = the seq of the last line (0 = none)
int             chat_count();
const ChatLine* chat_line(int i);
unsigned        chat_newest_seq();

// the channel a Windower mode belongs to (CH_BATTLE for the combat log)
int  chat_classify(int mode);
bool chat_is_battle(int chan);

// Fill chSpeaker / chBodyAt / chLsIdx / chTellOut from the client's line format for the line's channel :
//   party "(Name) msg"   linkshell "[1]<Name> msg" / "<Name> msg"   tell in "Name>> msg", out ">>Name : msg"
//   unity / assist "{Name} msg"   say / shout / NPC "Name : msg"   yell "Name[Zone]: msg"
// Anything else (or a format it does not recognise) leaves no speaker and the whole text as the message.
void chat_parse_speaker(ChatLine& h);

// //aio chatmodes (dev) : log every mode seen since load with its count, channel and one sample line
void chat_mode_report();

// decode ONE raw client line into printable text + per-char colour codes (pure ; exposed for the offline tests).
// Writes up to `cap` chars per output line ; returns the number of lines produced (split on 0x07 / CR / LF).
// col : 0 = the line's colour, 0x100 | n = "1E n" (palette / key n), 0x200 | n = "1F n" (the colour of chat mode n).
int  chat_decode(const char* raw, char out[][CHAT_TEXT], unsigned short col[][CHAT_TEXT], int lens[], int maxLines);

// The CLIENT's own colour for a code (0x100|n = "1E n", 0x200|n = mode n), read from its tables (FUN_054A3710 draws a
// log line : 1E n<17 -> a fixed palette, 1E n>=17 and 1F (mode -> key) -> a key->colour table). ARGB at full
// intensity (the client stores half : 0x80 = 1.0). 0 = unknown (tables not found -> the caller keeps its colour).
unsigned chat_game_color(unsigned short code);

} // namespace aio
