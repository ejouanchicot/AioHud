// check.h -- the whole assertion vocabulary of the test harness. Deliberately three macros : this suite exists
// to lock down behaviour that has already broken in production, not to grow a framework.
#pragma once
#include <stdio.h>
#include <string.h>

extern int g_run, g_fail;

#define CHECK(expr) do { ++g_run; if (!(expr)) { ++g_fail; \
    printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define CHECK_EQ(a, b) do { ++g_run; const long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { ++g_fail; printf("  FAIL  %s:%d  %s == %s   (%lld vs %lld)\n", \
                                     __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_STR(a, b) do { ++g_run; const char* _a = (a); const char* _b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { ++g_fail; \
        printf("  FAIL  %s:%d  %s == %s   (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a, #b, \
               _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)

#define SECTION(name) printf("-- %s\n", name)
