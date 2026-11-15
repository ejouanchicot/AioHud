// t_chat.cpp -- the Chat module's model : the client's control bytes decoded into printable text + colour codes,
// the channel of each Windower mode, and the text-thread ring -> history path (model/chat_log.h).
#include "check.h"
#include "model/chat_log.h"
#include <cstring>

using namespace aio;

namespace {
char          T[8][CHAT_TEXT];
unsigned short C[8][CHAT_TEXT];
int           L[8];
int dec(const char* s) { memset(T, 0, sizeof(T)); memset(C, 0, sizeof(C)); memset(L, 0, sizeof(L)); return chat_decode(s, T, C, L, 8); }
}  // namespace

void test_chat() {
    SECTION("chat : plain text passes through, one line, no colour codes");
    CHECK_EQ(dec("PlayerName1 : Hello there."), 1);
    CHECK_STR(T[0], "PlayerName1 : Hello there.");
    CHECK_EQ(L[0], 26); CHECK_EQ(C[0][0], 0);

    SECTION("chat : 0x1E n colours the following chars ; 0x1E 0x01 goes back to the line's colour");
    CHECK_EQ(dec("You obtain \x1E\x02Riftcinder\x1E\x01."), 1);
    CHECK_STR(T[0], "You obtain Riftcinder.");
    CHECK_EQ(C[0][0], 0); CHECK_EQ(C[0][11], 0x102); CHECK_EQ(C[0][20], 0x102); CHECK_EQ(C[0][21], 0);

    SECTION("chat : 0x1F n = the colour of chat mode n, kept as the code (the box asks the client's tables)");
    CHECK_EQ(dec("\x1F\x0D" "abc"), 1);
    CHECK_STR(T[0], "abc"); CHECK_EQ(C[0][0], 0x20D);

    SECTION("chat : 7F markers take the client's own lengths (FUN_05469997) -- 7F '4' = 3 bytes, 7F '8' = 4, unknown = the 7F alone");
    CHECK_EQ(dec("a\x7F" "4xyb"), 1); CHECK_STR(T[0], "ayb");
    CHECK_EQ(dec("a\x7F" "8xyzb"), 1); CHECK_STR(T[0], "azb");
    CHECK_EQ(dec("a\x7F" "Zb"), 1); CHECK_STR(T[0], "aZb");

    SECTION("chat : 0x07 splits NPC dialogue into lines ; an empty piece is not kept");
    CHECK_EQ(dec("First line.\x07Second line.\x07\x07Third."), 3);
    CHECK_STR(T[0], "First line."); CHECK_STR(T[1], "Second line."); CHECK_STR(T[2], "Third.");
    CHECK_EQ(dec("\x07\x07"), 0);

    SECTION("chat : 0x7F + its code byte are dropped (Omen lines end 0x7F 0x31)");
    CHECK_EQ(dec("Objective complete!\x7F\x31"), 1);
    CHECK_STR(T[0], "Objective complete!");

    SECTION("chat : auto-translate brackets and phrases");
    CHECK_EQ(dec("\xEF\x27Thank you.\xEF\x28"), 1);
    CHECK_STR(T[0], "{Thank you.}");
    CHECK_EQ(dec("Hi \xFD\x02\x02\x01\x0A\xFD!"), 1);
    CHECK_STR(T[0], "Hi {...}!");

    SECTION("chat : Shift-JIS glyphs of the English log (the battle arrow), unknown ones as '?'");
    CHECK_EQ(dec("PlayerName1 \x81\xA8 Apex Bat"), 1);
    CHECK_STR(T[0], "PlayerName1 -> Apex Bat");
    CHECK_EQ(dec("a\x88\x9F" "b"), 1);
    CHECK_STR(T[0], "a?b");

    SECTION("chat : battlemod's lines as captured live 2026-11-15 -- the arrow (two U+FFFD) and the space in a mob name (81 3F)");
    CHECK_EQ(dec("\x1E\xFC[17:49:53]\x1E\x01 [\x1E\xF7Tetsouo\x1E\x01] \x1E\x01" "559\x1E\x01 hit \xEF\xBF\xBD\xEF\xBF\xBD \x1F\x45" "Vampire\x81\x3F" "Leech\x1E\x01"), 1);
    CHECK_STR(T[0], "[17:49:53] [Tetsouo] 559 hit -> Vampire Leech");
    CHECK_EQ(dec("a\xEF\xBF\xBD" "b"), 1);
    CHECK_STR(T[0], "ab");

    SECTION("chat : the client's accented letters (85 xx = Latin-1 - 0x21) -- 'envoye' with an acute, captured live");
    CHECK_EQ(dec("envoy\x85\xC8 un tell"), 1);
    CHECK_EQ((unsigned char)T[0][5], 0xE9); CHECK_EQ(L[0], 14);

    SECTION("chat : a message longer than a history line is cut, never overruns");
    {   static char big[600]; memset(big, 'x', 599); big[599] = 0;
        CHECK_EQ(dec(big), 1); CHECK_EQ(L[0], CHAT_TEXT - 1); CHECK_EQ((int)strlen(T[0]), CHAT_TEXT - 1); }

    SECTION("chat : channels from Windower res/chat.lua (incoming and outgoing ids)");
    CHECK_EQ(chat_classify(9), CH_SAY);    CHECK_EQ(chat_classify(1), CH_SAY);
    CHECK_EQ(chat_classify(12), CH_TELL);  CHECK_EQ(chat_classify(4), CH_TELL);
    CHECK_EQ(chat_classify(13), CH_PARTY); CHECK_EQ(chat_classify(14), CH_LS1);
    CHECK_EQ(chat_classify(214), CH_LS2);  CHECK_EQ(chat_classify(212), CH_UNITY);
    CHECK_EQ(chat_classify(11), CH_YELL);  CHECK_EQ(chat_classify(15), CH_EMOTE);
    CHECK(chat_is_battle(chat_classify(20)));
    CHECK(!chat_is_battle(chat_classify(121)));

    SECTION("chat : text thread -> ring -> drain -> history, one history line per decoded line, in order");
    {   const int n0 = chat_count(); const unsigned s0 = chat_newest_seq();
        chat_queue("(PlayerName1) Ready.", 13);
        chat_queue("Line A.\x07Line B.", 150);
        chat_queue("\x07", 121);                       // nothing printable : no history line
        chat_drain();
        CHECK_EQ(chat_count(), n0 + 3);
        CHECK_EQ(chat_newest_seq(), s0 + 3);
        const ChatLine* a = chat_line(chat_count() - 3); const ChatLine* b = chat_line(chat_count() - 2); const ChatLine* c = chat_line(chat_count() - 1);
        CHECK(a && b && c);
        if (a && b && c) {
            CHECK_STR(a->text, "(PlayerName1) Ready."); CHECK_EQ(a->chan, CH_PARTY); CHECK_EQ(a->cont, 0);
            CHECK_STR(b->text, "Line A."); CHECK_EQ(b->chan, CH_NPC); CHECK_EQ(b->cont, 0);
            CHECK_STR(c->text, "Line B."); CHECK_EQ(c->cont, 1);
        }
    }
}
