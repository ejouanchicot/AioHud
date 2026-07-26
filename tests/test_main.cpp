// test_main.cpp -- runner for the AioHUD offline test suite.
//
// Scope, on purpose : only what can be decided WITHOUT the game running. That rules out rendering (a live
// D3D8 device is a parameter of every draw) and the memory offsets themselves (no test can prove that
// *(g+0x40)+0x02 is the zone). What it DOES cover is the pure logic that has repeatedly shipped broken --
// the layout parser, skillchain resolution, the duration model -- plus the contract that a failed game-memory
// read degrades instead of lying.
//
// Build + run :  tests.bat        (exit code = number of failed checks)
#include "check.h"

int g_run = 0, g_fail = 0;

void test_json();
void test_skillchain();
void test_durations();
void test_config();

int main() {
    test_json();
    test_skillchain();
    test_durations();
    test_config();
    printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail;
}
