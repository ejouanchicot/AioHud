// limbus_week.h -- when the Limbus weekly allowance resets. Pure integer arithmetic, no game, no clock : the
// caller passes the time in, so the offline suite can test a Sunday afternoon without waiting for one.
//
// THE RESET IS A SINGLE UTC INSTANT. Players describe it as "Sunday 17:00" in French summer time and "Sunday
// 16:00" in winter, which sounds like two rules and a daylight-saving problem. It is neither: both are
// SUNDAY 15:00 UTC, the same moment seen from CEST (UTC+2) and CET (UTC+1). It is also Monday 00:00 JST, the
// usual Japanese-midnight weekly rollover the game uses. Working in UTC means there is no DST logic to get
// wrong -- and no way for the count to reset an hour early or late twice a year.
#pragma once

namespace aio {

const int LIMBUS_WEEK_RUNS = 5;   // data collections granted per week (both areas share the allowance)

// The most recent reset instant at or before `nowUtc` (both in UTC epoch seconds).
inline long long limbus_week_start(long long nowUtc) {
    const long long DAY = 86400;
    const long long days = nowUtc / DAY;                 // whole days since the epoch
    const long long sod  = nowUtc - days * DAY;          // seconds into the current UTC day
    // 1970-01-01 was a THURSDAY, so epoch day 0 has weekday 4 when Sunday is 0.
    const int dow = (int)((days + 4) % 7);
    const long long sunday00  = nowUtc - (long long)dow * DAY - sod;
    long long boundary = sunday00 + 15 * 3600;           // Sunday 15:00 UTC
    if (nowUtc < boundary) boundary -= 7 * DAY;          // earlier on Sunday than 15:00 -> last week's reset
    return boundary;
}

// Was `stampUtc` observed in an EARLIER week than `nowUtc`? Then the allowance is back to full and a stored
// count is stale. A missing / zero stamp counts as rolled : we have nothing to trust.
inline bool limbus_week_rolled(long long stampUtc, long long nowUtc) {
    return stampUtc <= 0 || stampUtc < limbus_week_start(nowUtc);
}

} // namespace aio
