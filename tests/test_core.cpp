// test_core.cpp - CRT console test runner for the icsg core.
// Tests MAY use the CRT (they are not linked into the app).
#include "../src/core/base.h"
#include "../src/core/dt.h"
#include "../src/core/ics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, cond)                                              \
    do {                                                               \
        if (cond) { ++g_pass; printf("PASS %s\n", name); }             \
        else { ++g_fail; printf("FAIL %s  (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_MSG(name, cond, msg)                                     \
    do {                                                               \
        if (cond) { ++g_pass; printf("PASS %s\n", name); }             \
        else { ++g_fail; printf("FAIL %s  (%s:%d) %s\n", name, __FILE__, __LINE__, msg); } \
    } while (0)

static bool weqx(const WStr* s, const wchar_t* lit) { return wstr_equals(s, lit); }

// ------------------------------------------------------------- base tests ---

static void test_memory(void) {
    unsigned char* p = (unsigned char*)xmalloc(64);
    bool ok = p != NULL;
    for (int i = 0; i < 64; ++i) p[i] = (unsigned char)i;
    p = (unsigned char*)xrealloc(p, 256);
    ok = ok && p != NULL;
    bool preserved = true;
    for (int i = 0; i < 64; ++i) if (p[i] != (unsigned char)i) preserved = false;
    ok = ok && preserved;
    xfree(p);
    xfree(NULL);   // must be a no-op
    CHECK("xmalloc/xrealloc/xfree", ok);

    char buf[32];
    xmemzero(buf, sizeof(buf));
    bool zeroed = true;
    for (int i = 0; i < 32; ++i) if (buf[i] != 0) zeroed = false;
    xmemcopy(buf, "abc", 4);
    xmemmove(buf + 1, buf, 3);          // overlapping forward
    zeroed = zeroed && memcmp(buf, "aabc", 4) == 0;
    xmemmove(buf, buf + 1, 3);          // overlapping backward
    zeroed = zeroed && memcmp(buf, "abc", 4) == 0;
    zeroed = zeroed && xmemeq("abc", "abc", 3) && !xmemeq("abc", "abd", 3);
    CHECK("xmemzero/xmemcopy/xmemmove/xmemeq", zeroed);
}

static void test_wstr(void) {
    WStr s;
    wstr_init(&s);
    bool ok = wstr_is_empty(&s) && weq(wstr_c(&s), L"");
    wchar_t big[400];
    for (int i = 0; i < 399; ++i) big[i] = (wchar_t)(L'a' + (i % 26));
    big[399] = 0;
    for (int i = 0; i < 40; ++i) wstr_append(&s, big);   // forces several growths
    ok = ok && s.len == 40 * 399;
    ok = ok && s.data[s.len] == 0;
    wstr_clear(&s);
    ok = ok && s.len == 0 && s.cap > 0;

    wstr_set(&s, L"7");
    wstr_append_num(&s, 7, 2);
    wstr_append_num(&s, 0, 1);
    wstr_append_num(&s, -123, 0);
    wstr_append_num(&s, 1234567890123LL, 2);
    ok = ok && weqx(&s, L"7070-1231234567890123");

    wstr_set(&s, L"ab");
    wstr_append(&s, wstr_c(&s));        // self-append
    ok = ok && weqx(&s, L"abab");
    wstr_set_n(&s, L"abcdef", 3);
    ok = ok && weqx(&s, L"abc");
    ok = ok && wstr_equals_ci(&s, L"ABC") && wstr_starts_ci(L"Hello", 5, L"he");
    wstr_append_ch(&s, L'd');
    ok = ok && weqx(&s, L"abcd");
    wstr_free(&s);
    CHECK("WStr growth/append/num/copy", ok);
}

// ------------------------------------------------------------- utf8 tests ---

static void test_utf8(void) {
    // 4-byte code point (emoji U+1F600 as surrogate pair)
    wchar_t emoji[2] = { 0xD83D, 0xDE00 };
    char bytes[8];
    int n = utf8_encode(emoji, 2, bytes, 8);
    bool ok = n == 4;
    ok = ok && (unsigned char)bytes[0] == 0xF0 && (unsigned char)bytes[1] == 0x9F &&
               (unsigned char)bytes[2] == 0x98 && (unsigned char)bytes[3] == 0x80;
    wchar_t back[8];
    int m = utf8_decode(bytes, 4, back, 8);
    ok = ok && m == 2 && back[0] == 0xD83D && back[1] == 0xDE00;
    ok = ok && utf8_encoded_len(emoji, 2) == 4;

    // ASCII + multibyte mix
    wchar_t mix[4] = { L'A', 0x20AC, 0x4E2D, L'B' };   // A, euro, 中, B
    n = utf8_encode(mix, 4, bytes, 8);
    ok = ok && n == 8;   // 1 + 3 + 3 + 1
    m = utf8_decode(bytes, 8, back, 8);
    ok = ok && m == 4 && back[1] == 0x20AC && back[2] == 0x4E2D;

    // lone surrogate rejected
    wchar_t lone[1] = { 0xD83D };
    ok = ok && utf8_encode(lone, 1, bytes, 8) == -1;
    ok = ok && utf8_encoded_len(lone, 1) == -1;
    char loneEnc[3] = { (char)0xED, (char)0xA0, (char)0xBD };
    ok = ok && utf8_decode(loneEnc, 3, back, 8) == -1;

    // surrogate pair in decode input rejected (encoded CESU-8)
    char cesu[6] = { (char)0xED, (char)0xA0, (char)0xBD, (char)0xED, (char)0xB8, (char)0x80 };
    ok = ok && utf8_decode(cesu, 6, back, 8) == -1;

    // overlong rejected
    char over[2] = { (char)0xC0, (char)0x80 };
    ok = ok && utf8_decode(over, 2, back, 8) == -1;
    char over3[3] = { (char)0xE0, (char)0x80, (char)0x80 };
    ok = ok && utf8_decode(over3, 3, back, 8) == -1;

    // truncated rejected
    char trunc[2] = { (char)0xE4, (char)0xB8 };
    ok = ok && utf8_decode(trunc, 2, back, 8) == -1;

    // > U+10FFFF rejected
    char big5[4] = { (char)0xF4, (char)0x90, (char)0x80, (char)0x80 };
    ok = ok && utf8_decode(big5, 4, back, 8) == -1;

    // capacity too small
    ok = ok && utf8_encode(emoji, 2, bytes, 3) == -1;
    ok = ok && utf8_decode(bytes, 4, back, 1) == -1;

    // utf8_next walks code points, advances on malformed
    char walk[8] = { 'a', (char)0xE4, (char)0xB8, (char)0xAD, (char)0xFF, 'z', 0, 0 };
    int pos = 0;
    ok = ok && utf8_next(walk, 6, &pos) == L'a' && pos == 1;
    wchar_t cp = utf8_next(walk, 6, &pos);
    ok = ok && cp == 0x4E2D && pos == 4;
    cp = utf8_next(walk, 6, &pos);
    ok = ok && cp == 0xFFFD && pos == 5;
    cp = utf8_next(walk, 6, &pos);
    ok = ok && cp == L'z' && pos == 6;

    // BOM detection
    char bom[4] = { (char)0xEF, (char)0xBB, (char)0xBF, 'x' };
    ok = ok && utf8_has_bom(bom, 4) && !utf8_has_bom(bom + 1, 3);
    CHECK("UTF-8 codec edge cases", ok);
}

// ------------------------------------------------------------- file tests ---

static void test_files(void) {
    CreateDirectoryA("build-core", NULL);
    const char* text = "hello\r\nics\r\n";
    bool ok = file_write_all(L"build-core/probe.txt", text, (int)strlen(text));
    char* data = NULL;
    int size = 0;
    ok = ok && file_read_all(L"build-core/probe.txt", &data, &size);
    ok = ok && size == (int)strlen(text) && memcmp(data, text, (size_t)size) == 0;
    ok = ok && data[size] == 0;                       // trailing NUL guarantee
    xfree(data);
    ok = ok && file_exists(L"build-core/probe.txt");
    ok = ok && !file_exists(L"build-core/definitely-missing.txt");
    ok = ok && !file_read_all(L"build-core/definitely-missing.txt", &data, &size);
    DeleteFileA("build-core/probe.txt");
    CHECK("file round trip", ok);
}

// ---------------------------------------------------------------- dt tests --

static void test_dt(void) {
    DateTime a, b;
    dt_zero(&a); dt_zero(&b);
    a.year = 2024; a.month = 1; a.day = 2; a.hour = 3; a.minute = 4; a.second = 5;
    bool ok = dt_compare(&a, &a) == 0;
    b = a; b.minute = 5;
    ok = ok && dt_compare(&a, &b) < 0 && dt_compare(&b, &a) > 0;
    b = a; b.year = 2023;
    ok = ok && dt_compare(&a, &b) > 0;
    // utc/dateOnly ignored for ordering
    b = a; b.utc = true; b.dateOnly = true;
    ok = ok && dt_compare(&a, &b) == 0;

    ok = ok && dt_days_in_month(2024, 2) == 29 && dt_days_in_month(2023, 2) == 28;
    ok = ok && dt_days_in_month(2000, 2) == 29 && dt_days_in_month(1900, 2) == 28;
    ok = ok && dt_days_in_month(2024, 4) == 30 && dt_days_in_month(2024, 12) == 31;
    ok = ok && dt_days_in_month(2024, 13) == 0 && dt_days_in_month(2024, 0) == 0;

    DateTime d = a;
    dt_add_days(&d, 30);     // cross the month
    ok = ok && d.month == 2 && d.day == 1;
    dt_add_days(&d, -1);
    ok = ok && d.month == 1 && d.day == 31;
    d = a; dt_add_days(&d, 365);
    ok = ok && d.year == 2025 && d.month == 1 && d.day == 1;
    d = a; dt_add_months(&d, 1);
    ok = ok && d.month == 2 && d.day == 2;
    d.year = 2024; d.month = 1; d.day = 31; d.month = 1;
    dt_add_months(&d, 1);    // Jan 31 -> Feb 29 (clamped)
    ok = ok && d.month == 2 && d.day == 29;
    d = a; dt_add_minutes(&d, 90);
    ok = ok && d.hour == 4 && d.minute == 34;
    d = a; dt_add_minutes(&d, 24 * 60);
    ok = ok && d.day == 3 && d.hour == 3 && d.minute == 4;
    d = a; dt_add_minutes(&d, -5);
    ok = ok && d.day == 2 && d.hour == 2 && d.minute == 59;
    d = a; d.hour = 0; d.minute = 3; dt_add_minutes(&d, -5);
    ok = ok && d.day == 1 && d.hour == 23 && d.minute == 58;

    ok = ok && dt_day_of_week(&a) == 2;   // 2024-01-02 was a Tuesday
    d = a; d.year = 2024; d.month = 1; d.day = 1;
    ok = ok && dt_day_of_week(&d) == 1;   // Monday

    // leap second tolerated on parse, invalid values rejected
    ok = ok && dt_is_valid(&a) && !dt_is_zero(&a);
    d = a; d.day = 31; ok = ok && !dt_is_valid(&d);
    d = a; d.month = 0; ok = ok && !dt_is_valid(&d);
    d = a; d.second = 60; ok = ok && dt_is_valid(&d);
    DateTime z; dt_zero(&z); ok = ok && dt_is_zero(&z);
    CHECK("DateTime compare/add/leap/days_in_month", ok);
}

static void test_dt_ics_forms(void) {
    DateTime d;
    dt_zero(&d);
    d.year = 2024; d.month = 1; d.day = 2; d.hour = 3; d.minute = 4; d.second = 5;
    wchar_t out[32];
    DateTime x = d; x.utc = true;
    int n = dt_to_ics(&x, out, 32);
    bool ok = n == 16 && wcscmp(out, L"20240102T030405Z") == 0;
    x = d; x.utc = false;
    n = dt_to_ics(&x, out, 32);
    ok = ok && n == 15 && wcscmp(out, L"20240102T030405") == 0;
    x.dateOnly = true; x.utc = false;
    n = dt_to_ics(&x, out, 32);
    ok = ok && n == 8 && wcscmp(out, L"20240102") == 0;
    ok = ok && dt_to_ics(&x, out, 8) == -1;   // too small

    DateTime p;
    ok = ok && dt_from_ics(L"20240102T030405Z", 16, &p) && p.utc && p.year == 2024 &&
         p.hour == 3;
    ok = ok && dt_from_ics(L"20240102T030405", 15, &p) && !p.utc && !p.dateOnly;
    ok = ok && dt_from_ics(L"20240102", 8, &p) && p.dateOnly && !p.utc;
    ok = ok && dt_from_ics(L"2024-01-02T03:04:05", 19, &p) && p.month == 1 && p.hour == 3;
    ok = ok && dt_from_ics(L"2024.01.02T03:04:05", 19, &p) && p.day == 2;
    ok = ok && !dt_from_ics(L"2024010", 7, &p);
    ok = ok && !dt_from_ics(L"20241302T030405", 15, &p);
    ok = ok && !dt_from_ics(L"garbage", 7, &p);
    ok = ok && dt_from_ics(L"20240102T030405EXTRA", 15, &p);  // stops at len

    n = dt_to_display(&x, out, 32);   // dateOnly
    ok = ok && n == 10 && wcscmp(out, L"2024-01-02") == 0;
    x.dateOnly = false; x.utc = false;
    n = dt_to_display(&x, out, 32);
    ok = ok && n == 16 && wcscmp(out, L"2024-01-02 03:04") == 0;

    SYSTEMTIME st;
    dt_to_systemtime(&d, &st);
    ok = ok && st.wYear == 2024 && st.wMinute == 4 && st.wMilliseconds == 0;
    DateTime r;
    dt_from_systemtime(&r, &st);
    ok = ok && r.year == 2024 && !r.utc && !r.dateOnly;
    dt_from_systemtime(&r, NULL);
    ok = ok && dt_is_zero(&r);
    CHECK("dt_to_ics/dt_from_ics forms", ok);
}

// ------------------------------------------------------------ escape tests --

static void test_escape(void) {
    wchar_t buf[128];
    const wchar_t* src = L"a;b,c\\d";
    int n = ics_escape_text(src, 7, buf, 128);
    buf[n] = 0;
    bool ok = n == 10 && wcscmp(buf, L"a\\;b\\,c\\\\d") == 0;
    int m = ics_unescape_text(buf, n, buf + 64, 64);
    buf[64 + m] = 0;
    ok = ok && m == 7 && wcscmp(buf + 64, src) == 0;

    const wchar_t* nl = L"x\ny";
    n = ics_escape_text(nl, 3, buf, 128);
    buf[n] = 0;
    ok = ok && n == 4 && wcscmp(buf, L"x\\ny") == 0;
    m = ics_unescape_text(buf, n, buf + 64, 64);
    ok = ok && m == 3 && buf[64] == L'x' && buf[65] == L'\n' && buf[66] == L'y';

    // \N uppercase form; unknown escapes drop the backslash only
    const wchar_t* messy = L"p\\Nq\\rr\\tz";
    m = ics_unescape_text(messy, 10, buf, 128);
    ok = ok && m == 7 && wcsncmp(buf, L"p\nqrrtz", 7) == 0;

    // capacity: return required length, do not write
    const wchar_t* s2 = L"a,b";
    n = ics_escape_text(s2, 3, NULL, 0);
    ok = ok && n == 4;
    ok = ok && ics_escape_text(s2, 3, buf, 3) == 4;
    ok = ok && ics_unescape_text(s2, 3, NULL, 0) == 3;
    CHECK("escape/unescape round trip", ok);
}

// ------------------------------------------------------ shared prop tests ---

static void test_trigger_rrule(void) {
    wchar_t buf[32];
    ics_trigger_from_minutes(15, buf, 32);
    bool ok = wcscmp(buf, L"-PT15M") == 0;
    ics_trigger_from_minutes(1440, buf, 32);
    ok = ok && wcscmp(buf, L"-P1D") == 0;
    ics_trigger_from_minutes(10080, buf, 32);
    ok = ok && wcscmp(buf, L"-P1W") == 0;
    ics_trigger_from_minutes(2880, buf, 32);
    ok = ok && wcscmp(buf, L"-P2D") == 0;
    ics_trigger_from_minutes(0, buf, 32);
    ok = ok && wcscmp(buf, L"PT0M") == 0;
    ics_trigger_from_minutes(90, buf, 32);
    ok = ok && wcscmp(buf, L"-PT90M") == 0;
    ics_trigger_from_minutes(5, buf, 32);
    ok = ok && wcscmp(buf, L"PT5M") == 0;

    IcsEvent ev;
    ics_event_init(&ev);
    ev.freq = ICS_FREQ_WEEKLY;
    ev.count = 6;
    ev.bydayMask = ICSG_BYDAY_MO | ICSG_BYDAY_WE;
    WStr r;
    wstr_init(&r);
    ics_rrule_text(&ev, &r);
    ok = ok && weqx(&r, L"RRULE:FREQ=WEEKLY;COUNT=6;BYDAY=MO,WE");
    ev.count = 0;
    ev.interval = 2;
    ics_rrule_text(&ev, &r);
    ok = ok && weqx(&r, L"RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE");
    ev.freq = ICS_FREQ_DAILY;
    ev.bydayMask = 0;
    ev.interval = 1;
    DateTime until;
    dt_zero(&until);
    until.year = 2024; until.month = 12; until.day = 31; until.utc = true;
    ev.until = until;
    ev.hasUntil = true;
    ics_rrule_text(&ev, &r);
    ok = ok && weqx(&r, L"RRULE:FREQ=DAILY;UNTIL=20241231T000000Z");
    ev.freq = ICS_FREQ_NONE;
    ics_rrule_text(&ev, &r);
    ok = ok && wstr_is_empty(&r);

    // display strings
    ev.freq = ICS_FREQ_WEEKLY;
    ev.interval = 1;
    ev.count = 0;
    ev.hasUntil = false;
    dt_zero(&ev.until);
    ev.bydayMask = ICSG_BYDAY_MO | ICSG_BYDAY_WE;
    ics_rrule_display(&ev, &r);
    ok = ok && weqx(&r, L"\u6BCF\u5468(\u5468\u4E00,\u5468\u4E09)");
    ev.freq = ICS_FREQ_DAILY;
    ev.bydayMask = 0;
    ics_rrule_display(&ev, &r);
    ok = ok && weqx(&r, L"\u6BCF\u65E5");
    ev.freq = ICS_FREQ_NONE;
    ics_rrule_display(&ev, &r);
    ok = ok && wstr_is_empty(&r);

    IcsAlarm alarm;
    alarm.enabled = true;
    alarm.minutesBefore = 15;
    wstr_init(&alarm.description);
    ev.alarms[0] = alarm;
    ev.alarmCount = 1;
    ics_alarm_display(&ev, &r);
    ok = ok && weqx(&r, L"\u63D0\u524D15\u5206\u949F");
    ev.alarmCount = 0;
    ics_alarm_display(&ev, &r);
    ok = ok && wstr_is_empty(&r);
    wstr_free(&r);

    // enum round trips
    ok = ok && weq(ics_freq_name(ICS_FREQ_MONTHLY), L"MONTHLY");
    ok = ok && ics_freq_parse(L"weekly") == ICS_FREQ_WEEKLY;
    ok = ok && ics_freq_parse(L"nope") == ICS_FREQ_NONE;
    ok = ok && weq(ics_status_name(ICS_STATUS_CONFIRMED), L"CONFIRMED");
    ok = ok && ics_status_parse(L"tentative") == ICS_STATUS_TENTATIVE;
    ok = ok && weq(ics_transp_name(ICS_TRANSP_OPAQUE), L"OPAQUE");
    ok = ok && ics_transp_parse(L"transparent") == ICS_TRANSP_TRANSPARENT;
    ok = ok && weq(ics_class_name(ICS_CLASS_PRIVATE), L"PRIVATE");
    ok = ok && ics_class_parse(L"confidential") == ICS_CLASS_CONFIDENTIAL;
    ics_event_free(&ev);
    CHECK("trigger/rrule/displays/enums", ok);
}

// --------------------------------------------------------- serialise tests --

// model with fixed stamps so the output is deterministic
static void make_all_day_event(IcsEvent* ev) {
    ics_event_init(ev);
    wstr_set(&ev->uid, L"u1");
    dt_zero(&ev->dtstamp);
    ev->dtstamp.year = 2024; ev->dtstamp.month = 1; ev->dtstamp.day = 1;
    ev->dtstamp.utc = true;
    ev->allDay = true;
    ev->start.year = 2024; ev->start.month = 1; ev->start.day = 2;
    ev->end.year = 2024; ev->end.month = 1; ev->end.day = 3;
    wstr_set(&ev->summary, L"hi");
}

static void test_serialize_exact(void) {
    static const char expected[] =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//x//y\r\n"
        "CALSCALE:GREGORIAN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:u1\r\n"
        "DTSTAMP:20240101T000000Z\r\n"
        "DTSTART;VALUE=DATE:20240102\r\n"
        "DTEND;VALUE=DATE:20240103\r\n"
        "SUMMARY:hi\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsCalendar cal;
    ics_calendar_init(&cal);
    wstr_set(&cal.prodid, L"-//x//y");
    IcsEvent ev;
    make_all_day_event(&ev);
    bool appended = ics_calendar_duplicate(&cal, &ev, false) != NULL;

    int need = ics_serialize(&cal, NULL, 0);
    bool ok = appended && need == (int)strlen(expected);
    char* buf = (char*)malloc((size_t)need + 2);
    int got = ics_serialize(&cal, buf, need);
    ok = ok && got == need;                       // cap too small: nothing written
    got = ics_serialize(&cal, buf, need + 1);
    ok = ok && got == need && buf[need] == 0;
    ok = ok && memcmp(buf, expected, strlen(expected)) == 0;

    // timed events with end <= start omit DTEND
    ics_calendar_clear(&cal);
    IcsEvent t;
    ics_event_init(&t);
    wstr_set(&t.uid, L"u2");
    t.dtstamp = ev.dtstamp;
    t.start.year = 2024; t.start.month = 5; t.start.day = 6;
    t.start.hour = 8; t.start.minute = 0;
    t.end = t.start;
    ics_calendar_duplicate(&cal, &t, false);
    need = ics_serialize(&cal, NULL, 0);
    ics_serialize(&cal, buf, need + 1);
    ok = ok && strstr(buf, "DTSTART:20240506T080000\r\n") != NULL;
    ok = ok && strstr(buf, "DTEND") == NULL;
    xfree(buf);

    // malformed models
    IcsCalendar bad;
    ok = ok && ics_serialize(NULL, NULL, 0) == -1;
    ics_calendar_init(&bad);
    bad.count = 2;                                 // count without array
    ok = ok && ics_serialize(&bad, NULL, 0) == -1;
    ics_calendar_free(&bad);
    bad.count = 0;
    ok = ok && ics_serialize(&bad, NULL, 0) >= 0;  // empty calendar is valid
    ics_calendar_free(&bad);

    ics_event_free(&ev);
    ics_event_free(&t);
    ics_calendar_free(&cal);
    CHECK("serialisation byte-exact (all-day)", ok);
}

// independent reference folder operating on an unfolded wide text with '\n'
// separators; encodes each code point itself and folds at 75 octets.
struct BBuf { char* p; int len; int cap; };

static void bb_init(BBuf* b) { b->p = NULL; b->len = 0; b->cap = 0; }
static void bb_push(BBuf* b, const char* s, int n) {
    if (b->len + n + 1 > b->cap) {
        int nc = b->cap ? b->cap * 2 : 512;
        while (nc < b->len + n + 1) nc *= 2;
        b->p = (char*)realloc(b->p, (size_t)nc);
        b->cap = nc;
    }
    memcpy(b->p + b->len, s, (size_t)n);
    b->len += n;
    b->p[b->len] = 0;
}

static int bb_cp_bytes(wchar_t c, wchar_t c2, bool havePair, char out[4]) {
    unsigned int cp = (unsigned int)c;
    if (havePair) cp = 0x10000u + (((unsigned int)c - 0xD800u) << 10) + ((unsigned int)c2 - 0xDC00u);
    int n;
    if (cp < 0x80) { out[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); n = 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    return n;
}

static void bb_fold_line(BBuf* out, const wchar_t* line, int len) {
    int octets = 0;
    int i = 0;
    while (i < len) {
        wchar_t c = line[i];
        bool pair = (c >= 0xD800 && c <= 0xDBFF && i + 1 < len &&
                     line[i + 1] >= 0xDC00 && line[i + 1] <= 0xDFFF);
        char bytes[4];
        int n = bb_cp_bytes(c, pair ? line[i + 1] : 0, pair, bytes);
        if (octets > 0 && octets + n > 75) {
            bb_push(out, "\r\n ", 3);
            octets = 1;
        }
        bb_push(out, bytes, n);
        octets += n;
        i += pair ? 2 : 1;
    }
    bb_push(out, "\r\n", 2);
}

// builds the expected unfolded wide document for the "full" calendar
static void test_serialize_full(void) {
    IcsCalendar cal;
    ics_calendar_init(&cal);
    wstr_set(&cal.prodid, L"-//x//y");

    IcsEvent ev;
    ics_event_init(&ev);
    wstr_set(&ev.uid, L"uid-full");
    dt_zero(&ev.dtstamp);
    ev.dtstamp.year = 2024; ev.dtstamp.month = 1; ev.dtstamp.day = 1; ev.dtstamp.utc = true;
    ev.created = ev.dtstamp;
    ev.sequence = 2;
    ev.timeMode = ICS_TIME_TZID;
    wstr_set(&ev.tzid, L"China Standard Time");
    ev.start.year = 2024; ev.start.month = 1; ev.start.day = 2;
    ev.start.hour = 3; ev.start.minute = 4; ev.start.second = 5;
    ev.end = ev.start; ev.end.hour = 4;
    WStr summary;
    wstr_init(&summary);
    for (int i = 0; i < 40; ++i) wstr_append(&summary, L"\u4F1A\u8BAE");   // 会议 x40
    wstr_append(&summary, L"tail");
    wstr_set(&ev.summary, wstr_c(&summary));
    wstr_set(&ev.description, L"a;b,c\\d");
    wstr_set(&ev.location, L"Room 5");
    wstr_set(&ev.categories, L"WORK,HOME");
    wstr_set(&ev.url, L"http://x.example");
    ev.status = ICS_STATUS_CONFIRMED;
    ev.transp = ICS_TRANSP_OPAQUE;
    ev.klass = ICS_CLASS_PUBLIC;
    ev.priority = 5;
    wstr_set(&ev.organizer, L"org@x.com");
    wstr_set(&ev.organizerName, L"\u5F20\u4E09");      // 张三
    wstr_set(&ev.attendees[0].email, L"att@y.com");
    wstr_set(&ev.attendees[0].name, L"\u674E\u56DB");  // 李四
    ev.attendees[0].rsvp = true;
    ev.attendeeCount = 1;
    ev.freq = ICS_FREQ_WEEKLY;
    ev.interval = 1;
    ev.bydayMask = ICSG_BYDAY_MO | ICSG_BYDAY_WE;
    dt_zero(&ev.exdates[0]);
    ev.exdates[0].year = 2024; ev.exdates[0].month = 1; ev.exdates[0].day = 9;
    ev.exdates[0].hour = 3; ev.exdates[0].minute = 4; ev.exdates[0].second = 5;
    ev.exdateCount = 1;
    ev.alarms[0].enabled = true;
    ev.alarms[0].minutesBefore = 15;
    wstr_init(&ev.alarms[0].description);   // falls back to the summary
    ev.alarmCount = 1;
    bool appended = ics_calendar_duplicate(&cal, &ev, false) != NULL;

    // expected document, unfolded, '\n' separated
    WStr exp;
    wstr_init(&exp);
    WStr ln;
    wstr_init(&ln);
#define ADD_LINE(w) (wstr_append(&exp, w), wstr_append(&exp, L"\n"))
    ADD_LINE(L"BEGIN:VCALENDAR");
    ADD_LINE(L"VERSION:2.0");
    ADD_LINE(L"PRODID:-//x//y");
    ADD_LINE(L"CALSCALE:GREGORIAN");
    WStr vt;
    wstr_init(&vt);
    bool haveVt = ics_zone_vtimezone(L"China Standard Time", 2024, &vt);
    if (haveVt) {
        // split the VTIMEZONE text (CRLF separated) into expected lines
        const wchar_t* p = wstr_c(&vt);
        int start = 0;
        for (int i = 0; i <= vt.len; ++i) {
            if (p[i] == L'\r' && p[i + 1] == L'\n') {
                wstr_append_n(&exp, p + start, i - start);
                wstr_append_ch(&exp, L'\n');
                ++i;
                start = i + 1;
            }
        }
    }
    ADD_LINE(L"BEGIN:VEVENT");
    ADD_LINE(L"UID:uid-full");
    ADD_LINE(L"DTSTAMP:20240101T000000Z");
    ADD_LINE(L"CREATED:20240101T000000Z");
    ADD_LINE(L"SEQUENCE:2");
    ADD_LINE(L"DTSTART;TZID=China Standard Time:20240102T030405");
    ADD_LINE(L"DTEND;TZID=China Standard Time:20240102T040405");
    wstr_set(&ln, L"SUMMARY:"); wstr_append(&ln, wstr_c(&summary));
    wstr_append(&exp, wstr_c(&ln)); wstr_append(&exp, L"\n");
    ADD_LINE(L"DESCRIPTION:a\\;b\\,c\\\\d");
    ADD_LINE(L"LOCATION:Room 5");
    ADD_LINE(L"CATEGORIES:WORK\\,HOME");
    ADD_LINE(L"URL:http://x.example");
    ADD_LINE(L"STATUS:CONFIRMED");
    ADD_LINE(L"TRANSP:OPAQUE");
    ADD_LINE(L"CLASS:PUBLIC");
    ADD_LINE(L"PRIORITY:5");
    ADD_LINE(L"ORGANIZER;CN=\u5F20\u4E09:mailto:org@x.com");
    ADD_LINE(L"ATTENDEE;CN=\u674E\u56DB;RSVP=TRUE:mailto:att@y.com");
    ADD_LINE(L"RRULE:FREQ=WEEKLY;BYDAY=MO,WE");
    ADD_LINE(L"EXDATE;TZID=China Standard Time:20240109T030405");
    ADD_LINE(L"BEGIN:VALARM");
    ADD_LINE(L"ACTION:DISPLAY");
    ADD_LINE(L"TRIGGER:-PT15M");
    wstr_set(&ln, L"DESCRIPTION:"); wstr_append(&ln, wstr_c(&summary));
    wstr_append(&exp, wstr_c(&ln)); wstr_append(&exp, L"\n");
    ADD_LINE(L"END:VALARM");
    ADD_LINE(L"END:VEVENT");
    ADD_LINE(L"END:VCALENDAR");
#undef ADD_LINE

    // fold the expected document with the independent folder
    BBuf expected;
    bb_init(&expected);
    {
        const wchar_t* p = wstr_c(&exp);
        int start = 0;
        for (int i = 0; i <= exp.len; ++i) {
            if (p[i] == L'\n') { bb_fold_line(&expected, p + start, i - start); start = i + 1; }
        }
    }

    int need = ics_serialize(&cal, NULL, 0);
    char* got = (char*)malloc((size_t)need + 2);
    int wrote = ics_serialize(&cal, got, need + 1);
    bool ok = appended && haveVt && wrote == need && need == expected.len &&
              memcmp(got, expected.p, (size_t)need) == 0;
    if (!ok) {
        printf("  need=%d expected=%d wrote=%d\n", need, expected.len, wrote);
        int n = need < expected.len ? need : expected.len;
        for (int i = 0; i < (n < 400 ? n : 400); ++i) {
            if (got[i] != expected.p[i]) {
                printf("  first diff at %d: got %.20s expected %.20s\n", i,
                       got + i, expected.p + i);
                break;
            }
        }
    }
    // every continuation exists => no line exceeds 75 octets; verify directly
    bool foldOk = true;
    {
        int lineOct = 0;
        for (int i = 0; i < need; ++i) {
            if (got[i] == '\r') { lineOct = 0; ++i; continue; }
            ++lineOct;
            if (lineOct > 75) foldOk = false;
        }
    }
    ok = ok && foldOk;
    free(got);
    wstr_free(&exp);
    wstr_free(&ln);
    wstr_free(&vt);
    wstr_free(&summary);
    ics_event_free(&ev);
    ics_calendar_free(&cal);
    CHECK("serialisation 2-event/full cross-checked folding", ok);
}

// -------------------------------------------------------------- parse tests --

static void test_parse_messy(void) {
    static const char messy[] =
        "\xEF\xBB\xBF"
        "BEGIN:VCALENDAR\n"
        "PRODID:-//messy//test\n"
        "X-UNKNOWN:whatever\n"
        "BEGIN:XUNKNOWN\n"
        "X-INNER:skip\n"
        "END:XUNKNOWN\n"
        "BEGIN:VEVENT\n"
        "UID:messy-1\n"
        "SUMMARY:hello\n"
        " world\tmore\n"
        "ATTENDEE;CN=\"Zhang; San\";RSVP=TRUE:mailto:a@b.c\n"
        "DTSTART;TZID=Some/Unknown_Zone:20240305T060708\n"
        "DTEND:20240305T070708\n"
        "EXDATE:20240306T060708,20240307T060708\n"
        "BEGIN:VALARM\n"
        "TRIGGER:-P1D\n"
        "DESCRIPTION:wake\n"
        "END:VALARM\n";

    IcsCalendar cal;
    ics_calendar_init(&cal);
    bool ok = ics_parse(messy, (int)strlen(messy), &cal);
    ok = ok && cal.count == 1;
    if (cal.count == 1) {
        IcsEvent* e = &cal.events[0];
        ok = ok && weqx(&e->uid, L"messy-1");
        ok = ok && weqx(&e->summary, L"helloworld\tmore");
        ok = ok && e->attendeeCount == 1;
        ok = ok && weqx(&e->attendees[0].name, L"Zhang; San");
        ok = ok && weqx(&e->attendees[0].email, L"a@b.c");
        ok = ok && e->attendees[0].rsvp;
        ok = ok && e->timeMode == ICS_TIME_TZID;
        ok = ok && weqx(&e->tzid, L"Some/Unknown_Zone");
        ok = ok && e->start.year == 2024 && e->start.hour == 6;
        ok = ok && e->end.hour == 7;
        ok = ok && e->exdateCount == 2 && e->exdates[1].day == 7;
        ok = ok && e->alarmCount == 1 && e->alarms[0].minutesBefore == -1440;
        ok = ok && weqx(&e->alarms[0].description, L"wake");
        ok = ok && weqx(&cal.prodid, L"-//messy//test");
    }
    ics_calendar_free(&cal);

    // empty input fails
    ics_calendar_init(&cal);
    ok = ok && !ics_parse(NULL, 0, &cal);
    ok = ok && !ics_parse("", 0, &cal);
    ics_calendar_free(&cal);
    CHECK("tolerant parse of messy calendar", ok);
}

static void test_save_load_round(void) {
    CreateDirectoryA("build-core", NULL);
    IcsCalendar cal;
    ics_calendar_init(&cal);
    wstr_set(&cal.prodid, L"-//round//trip");
    wstr_set(&cal.name, L"\u6D4B\u8BD5");   // 测试

    IcsEvent a;
    ics_event_init(&a);
    wstr_set(&a.uid, L"round-1");
    dt_zero(&a.dtstamp);
    a.dtstamp.year = 2024; a.dtstamp.month = 6; a.dtstamp.day = 1;
    a.dtstamp.hour = 12; a.dtstamp.utc = true;
    a.timeMode = ICS_TIME_UTC;
    a.start.year = 2024; a.start.month = 6; a.start.day = 2;
    a.start.hour = 8; a.start.minute = 30;
    a.end = a.start; a.end.hour = 10;
    wstr_set(&a.summary, L"UTC event");
    ics_calendar_duplicate(&cal, &a, false);

    IcsEvent b;
    ics_event_init(&b);
    wstr_set(&b.uid, L"round-2");
    b.dtstamp = a.dtstamp;
    b.allDay = true;
    b.start.year = 2024; b.start.month = 6; b.start.day = 10;
    b.end.year = 2024; b.end.month = 6; b.start.day = 11; b.end.month = 6; b.end.day = 11;
    wstr_set(&b.summary, L"All day");
    ics_calendar_duplicate(&cal, &b, false);

    bool ok = ics_save_file(&cal, L"build-core/round.ics");
    IcsCalendar loaded;
    ics_calendar_init(&loaded);
    ok = ok && ics_load_file(&loaded, L"build-core/round.ics");
    ok = ok && loaded.count == 2;

    int need1 = ics_serialize(&cal, NULL, 0);
    int need2 = ics_serialize(&loaded, NULL, 0);
    char* s1 = (char*)malloc((size_t)need1 + 1);
    char* s2 = (char*)malloc((size_t)need2 + 1);
    ics_serialize(&cal, s1, need1 + 1);
    ics_serialize(&loaded, s2, need2 + 1);
    ok = ok && need1 == need2 && memcmp(s1, s2, (size_t)need1) == 0;
    if (need1 != need2) printf("  need1=%d need2=%d\n", need1, need2);
    free(s1); free(s2);

    // single-event export + reload
    ok = ok && ics_save_event_file(&a, L"build-core/single.ics");
    IcsCalendar single;
    ics_calendar_init(&single);
    ok = ok && ics_load_file(&single, L"build-core/single.ics");
    ok = ok && single.count == 1 && weqx(&single.events[0].uid, L"round-1");
    ics_calendar_free(&single);

    ics_event_free(&a);
    ics_event_free(&b);
    ics_calendar_free(&cal);
    ics_calendar_free(&loaded);
    CHECK("save/load/serialise equality", ok);
}

static void test_merge(void) {
    IcsCalendar dst, src;
    ics_calendar_init(&dst);
    ics_calendar_init(&src);
    IcsEvent e;
    ics_event_init(&e);
    wstr_set(&e.uid, L"m1");
    ics_calendar_duplicate(&src, &e, false);
    wstr_set(&e.uid, L"m2");
    ics_calendar_duplicate(&src, &e, false);

    int added = ics_merge(&dst, &src);
    bool ok = added == 2 && dst.count == 2;
    ok = ok && weqx(&dst.events[0].uid, L"m1") && weqx(&dst.events[1].uid, L"m2");
    ok = ok && src.count == 0;   // src cleared

    ics_event_free(&e);
    ics_calendar_free(&dst);
    ics_calendar_free(&src);
    CHECK("ics_merge deep append + clear", ok);
}

// -------------------------------------------------------------- zone tests --

static void test_zones(void) {
    static IcsZoneInfo zones[200];
    int n = ics_zone_enumerate(zones, 200);
    bool ok = n > 0;
    if (n > 200) n = 200;
    // sorted by bias then name
    bool sorted = true;
    for (int i = 1; i < n; ++i) {
        if (zones[i - 1].biasMinutes > zones[i].biasMinutes) sorted = false;
    }
    ok = ok && sorted;
    ok = ok && !wstr_is_empty(&zones[0].keyName) && !wstr_is_empty(&zones[0].display);
    for (int i = 0; i < n; ++i) ics_zone_info_free(&zones[i]);

    IcsZoneInfo info;
    wstr_init(&info.keyName);
    wstr_init(&info.display);
    ok = ok && ics_zone_find(L"China Standard Time", &info);
    ok = ok && info.biasMinutes == -480;
    ics_zone_info_free(&info);

    // local -> UTC for a fixed-offset date (winter, no DST in China)
    DateTime d;
    dt_zero(&d);
    d.year = 2024; d.month = 1; d.day = 2; d.hour = 3; d.minute = 4;
    ok = ok && ics_zone_local_to_utc(L"China Standard Time", &d);
    ok = ok && d.utc && d.day == 1 && d.hour == 19 && d.minute == 4;
    ok = ok && ics_zone_utc_to_local(L"China Standard Time", &d);
    ok = ok && !d.utc && d.day == 2 && d.hour == 3 && d.minute == 4;

    WStr vt;
    wstr_init(&vt);
    ok = ok && ics_zone_vtimezone(L"China Standard Time", 2024, &vt);
    ok = ok && wcsstr(wstr_c(&vt), L"BEGIN:VTIMEZONE") == wstr_c(&vt);
    ok = ok && wcsstr(wstr_c(&vt), L"TZID:China Standard Time") != NULL;
    ok = ok && wcsstr(wstr_c(&vt), L"END:VTIMEZONE") != NULL;
    ok = ok && wcsstr(wstr_c(&vt), L"TZOFFSETTO:+0800") != NULL;
    wstr_free(&vt);
    CHECK("Windows zone DB enumerate/find/convert/vtimezone", ok);
}

static void test_uid(void) {
    static WStr uids[100];
    bool ok = true;
    for (int i = 0; i < 100; ++i) {
        wstr_init(&uids[i]);
        ics_event_make_uid(&uids[i]);
    }
    for (int i = 0; i < 100 && ok; ++i) {
        if (wstr_is_empty(&uids[i])) ok = false;
        for (int j = i + 1; j < 100; ++j) {
            if (weq(wstr_c(&uids[i]), wstr_c(&uids[j]))) ok = false;
        }
    }
    // suffix domain
    for (int i = 0; i < 100; ++i) {
        const wchar_t* s = wstr_c(&uids[i]);
        if (!wcsstr(s, L"@ics-generate")) ok = false;
        wstr_free(&uids[i]);
    }
    CHECK("100 UIDs all distinct", ok);
}

// ---------------------------------------------------------- lifecycle tests --

static void test_lifecycle(void) {
    IcsCalendar cal;
    ics_calendar_init(&cal);
    bool ok = weqx(&cal.version, L"2.0") && !wstr_is_empty(&cal.prodid);
    IcsEvent* a = ics_calendar_add(&cal);
    IcsEvent* b = ics_calendar_add(&cal);
    ok = ok && a && b && cal.count == 2;
    ok = ok && !wstr_is_empty(&a->uid) && !weq(wstr_c(&a->uid), wstr_c(&b->uid));
    ok = ok && !dt_is_zero(&a->dtstamp) && !dt_is_zero(&a->created);
    ok = ok && (a->end.day != a->start.day || a->end.hour != a->start.hour); // +1h
    ok = ok && a->start.minute == 0 && a->start.second == 0;  // rounded up

    wchar_t firstUid[128];
    wcopy(firstUid, 128, wstr_c(&b->uid));
    ok = ok && ics_calendar_move(&cal, 1, -1);
    ok = ok && weq(wstr_c(&cal.events[0].uid), firstUid);
    ok = ok && !ics_calendar_move(&cal, 0, -1);   // out of range

    ok = ok && ics_calendar_remove(&cal, 0) && cal.count == 1;
    ok = ok && !ics_calendar_remove(&cal, 5);

    IcsEvent* dup = ics_calendar_duplicate(&cal, &cal.events[0], true);
    ok = ok && dup && cal.count == 2;
    ok = ok && !weq(wstr_c(&dup->uid), wstr_c(&cal.events[0].uid));
    ok = ok && ics_calendar_duplicate(&cal, &cal.events[0], false) != NULL;

    // deep copy: freeing the source strings must not affect the copy
    wstr_set(&cal.events[0].summary, L"original");
    IcsEvent copy;
    ics_event_init(&copy);
    ics_event_copy(&copy, &cal.events[0]);
    wstr_set(&cal.events[0].summary, L"changed");
    ok = ok && weqx(&copy.summary, L"original");
    ics_event_free(&copy);

    ics_calendar_clear(&cal);
    ok = ok && cal.count == 0 && weqx(&cal.version, L"2.0");
    ics_calendar_free(&cal);
    CHECK("calendar/event lifecycle", ok);
}

int main(void) {
    test_memory();
    test_wstr();
    test_utf8();
    test_files();
    test_dt();
    test_dt_ics_forms();
    test_escape();
    test_trigger_rrule();
    test_serialize_exact();
    test_serialize_full();
    test_parse_messy();
    test_save_load_round();
    test_merge();
    test_zones();
    test_uid();
    test_lifecycle();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
