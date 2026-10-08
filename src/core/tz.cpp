// tz.cpp - Windows dynamic time zone database, DST aware conversion and
// VTIMEZONE generation. Contract: src/core/ics.h (frozen).
//
// Everything comes from the live Windows database (EnumDynamicTimeZoneInformation
// and GetTimeZoneInformationForYear); no transition offsets are hardcoded. The
// only literal table is the IANA -> Windows key alias list below, which exists so
// that calendars written by other tools (TZID=Asia/Shanghai) can be resolved.
#include "ics.h"

// ------------------------------------------------------- IANA alias table ---

struct IanaAlias {
    const wchar_t* iana;
    const wchar_t* windows;
};

static const IanaAlias kIanaAliases[] = {
    { L"UTC",                        L"UTC" },
    { L"Etc/UTC",                    L"UTC" },
    { L"Etc/GMT",                    L"UTC" },
    { L"GMT",                        L"GMT Standard Time" },
    { L"Asia/Shanghai",              L"China Standard Time" },
    { L"Asia/Chongqing",             L"China Standard Time" },
    { L"Asia/Harbin",                L"China Standard Time" },
    { L"Asia/Urumqi",                L"Central Asia Standard Time" },
    { L"Asia/Hong_Kong",             L"Hong Kong Standard Time" },
    { L"Asia/Macau",                 L"China Standard Time" },
    { L"Asia/Taipei",                L"Taipei Standard Time" },
    { L"Asia/Tokyo",                 L"Tokyo Standard Time" },
    { L"Asia/Seoul",                 L"Korea Standard Time" },
    { L"Asia/Pyongyang",             L"Korea Standard Time" },
    { L"Asia/Singapore",             L"Singapore Standard Time" },
    { L"Asia/Manila",                L"Singapore Standard Time" },
    { L"Asia/Kuala_Lumpur",          L"Singapore Standard Time" },
    { L"Asia/Kolkata",               L"India Standard Time" },
    { L"Asia/Calcutta",              L"India Standard Time" },
    { L"Asia/Colombo",               L"Sri Lanka Standard Time" },
    { L"Asia/Kathmandu",             L"Nepal Standard Time" },
    { L"Asia/Dhaka",                 L"Bangladesh Standard Time" },
    { L"Asia/Karachi",               L"Pakistan Standard Time" },
    { L"Asia/Bangkok",               L"SE Asia Standard Time" },
    { L"Asia/Jakarta",               L"SE Asia Standard Time" },
    { L"Asia/Ho_Chi_Minh",           L"SE Asia Standard Time" },
    { L"Asia/Dubai",                 L"Arabian Standard Time" },
    { L"Asia/Riyadh",                L"Arab Standard Time" },
    { L"Asia/Tehran",                L"Iran Standard Time" },
    { L"Asia/Jerusalem",             L"Israel Standard Time" },
    { L"Asia/Beirut",                L"Middle East Standard Time" },
    { L"Asia/Baghdad",               L"Arabic Standard Time" },
    { L"Asia/Istanbul",              L"Turkey Standard Time" },
    { L"Asia/Almaty",                L"Central Asia Standard Time" },
    { L"Asia/Tashkent",              L"West Asia Standard Time" },
    { L"Asia/Yekaterinburg",         L"Ekaterinburg Standard Time" },
    { L"Asia/Novosibirsk",           L"N. Central Asia Standard Time" },
    { L"Asia/Vladivostok",           L"Vladivostok Standard Time" },
    { L"Asia/Yakutsk",               L"Yakutsk Standard Time" },
    { L"Asia/Krasnoyarsk",           L"North Asia Standard Time" },
    { L"Asia/Irkutsk",               L"North Asia East Standard Time" },
    { L"Asia/Magadan",               L"Magadan Standard Time" },
    { L"Asia/Kamchatka",             L"Kamchatka Standard Time" },
    { L"Europe/London",              L"GMT Standard Time" },
    { L"Europe/Dublin",              L"GMT Standard Time" },
    { L"Europe/Lisbon",              L"GMT Standard Time" },
    { L"Europe/Paris",               L"Romance Standard Time" },
    { L"Europe/Madrid",              L"Romance Standard Time" },
    { L"Europe/Brussels",            L"Romance Standard Time" },
    { L"Europe/Copenhagen",          L"Romance Standard Time" },
    { L"Europe/Berlin",              L"W. Europe Standard Time" },
    { L"Europe/Amsterdam",           L"W. Europe Standard Time" },
    { L"Europe/Rome",                L"W. Europe Standard Time" },
    { L"Europe/Vienna",              L"W. Europe Standard Time" },
    { L"Europe/Zurich",              L"W. Europe Standard Time" },
    { L"Europe/Stockholm",           L"W. Europe Standard Time" },
    { L"Europe/Oslo",                L"W. Europe Standard Time" },
    { L"Europe/Prague",              L"Central Europe Standard Time" },
    { L"Europe/Budapest",            L"Central Europe Standard Time" },
    { L"Europe/Belgrade",            L"Central Europe Standard Time" },
    { L"Europe/Warsaw",              L"Central European Standard Time" },
    { L"Europe/Athens",              L"GTB Standard Time" },
    { L"Europe/Bucharest",           L"GTB Standard Time" },
    { L"Europe/Helsinki",            L"FLE Standard Time" },
    { L"Europe/Kiev",                L"FLE Standard Time" },
    { L"Europe/Kyiv",                L"FLE Standard Time" },
    { L"Europe/Riga",                L"FLE Standard Time" },
    { L"Europe/Sofia",               L"FLE Standard Time" },
    { L"Europe/Moscow",              L"Russian Standard Time" },
    { L"Europe/Minsk",               L"Minsk Standard Time" },
    { L"Atlantic/Reykjavik",         L"UTC" },
    { L"Atlantic/Azores",            L"Azores Standard Time" },
    { L"Atlantic/Canary",            L"GMT Standard Time" },
    { L"Africa/Cairo",               L"Egypt Standard Time" },
    { L"Africa/Johannesburg",        L"South Africa Standard Time" },
    { L"Africa/Lagos",               L"W. Central Africa Standard Time" },
    { L"Africa/Nairobi",             L"E. Africa Standard Time" },
    { L"Africa/Casablanca",          L"Morocco Standard Time" },
    { L"Africa/Algiers",             L"W. Central Africa Standard Time" },
    { L"America/New_York",           L"Eastern Standard Time" },
    { L"America/Toronto",            L"Eastern Standard Time" },
    { L"America/Chicago",            L"Central Standard Time" },
    { L"America/Winnipeg",           L"Central Standard Time" },
    { L"America/Denver",             L"Mountain Standard Time" },
    { L"America/Edmonton",           L"Mountain Standard Time" },
    { L"America/Phoenix",            L"US Mountain Standard Time" },
    { L"America/Los_Angeles",        L"Pacific Standard Time" },
    { L"America/Vancouver",          L"Pacific Standard Time" },
    { L"America/Anchorage",          L"Alaskan Standard Time" },
    { L"America/Adak",               L"Aleutian Standard Time" },
    { L"America/Honolulu",           L"Hawaiian Standard Time" },
    { L"America/Mexico_City",        L"Central Standard Time (Mexico)" },
    { L"America/Tijuana",            L"Pacific Standard Time (Mexico)" },
    { L"America/Sao_Paulo",          L"E. South America Standard Time" },
    { L"America/Argentina/Buenos_Aires", L"Argentina Standard Time" },
    { L"America/Bogota",             L"SA Pacific Standard Time" },
    { L"America/Lima",               L"SA Pacific Standard Time" },
    { L"America/Santiago",           L"Pacific SA Standard Time" },
    { L"America/Caracas",            L"Venezuela Standard Time" },
    { L"America/Havana",             L"Cuba Standard Time" },
    { L"America/Panama",             L"SA Pacific Standard Time" },
    { L"America/Godthab",            L"Greenland Standard Time" },
    { L"Pacific/Auckland",           L"New Zealand Standard Time" },
    { L"Pacific/Fiji",               L"Fiji Standard Time" },
    { L"Pacific/Honolulu",           L"Hawaiian Standard Time" },
    { L"Pacific/Guam",               L"West Pacific Standard Time" },
    { L"Pacific/Port_Moresby",       L"West Pacific Standard Time" },
    { L"Australia/Sydney",           L"AUS Eastern Standard Time" },
    { L"Australia/Melbourne",        L"AUS Eastern Standard Time" },
    { L"Australia/Brisbane",         L"E. Australia Standard Time" },
    { L"Australia/Perth",            L"W. Australia Standard Time" },
    { L"Australia/Adelaide",         L"Cen. Australia Standard Time" },
    { L"Australia/Darwin",           L"AUS Central Standard Time" },
    { L"Australia/Hobart",           L"Tasmania Standard Time" },
};

static const wchar_t* tz_windows_key_for(const wchar_t* name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < ICSG_ARRAY_COUNT(kIanaAliases); ++i) {
        if (weq_ci(name, kIanaAliases[i].iana)) return kIanaAliases[i].windows;
    }
    return name;   // assume it already is a Windows key (or an unknown TZID)
}

// --------------------------------------------------------------- helpers ----

static void tz_append_utc_offset(WStr* out, int utcMinutes) {
    int off = utcMinutes;
    wchar_t sign = L'+';
    if (off < 0) { sign = L'-'; off = -off; }
    wstr_append(out, L"UTC");
    wstr_append_ch(out, sign);
    wstr_append_num(out, off / 60, 2);
    wstr_append_ch(out, L':');
    wstr_append_num(out, off % 60, 2);
}

static void tz_append_ics_offset(WStr* out, int utcMinutes) {
    int off = utcMinutes;
    wchar_t sign = L'+';
    if (off < 0) { sign = L'-'; off = -off; }
    wstr_append_ch(out, sign);
    wstr_append_num(out, off / 60, 2);
    wstr_append_num(out, off % 60, 2);
}

static void tz_build_display(WStr* out, int bias, const wchar_t* standardName, const wchar_t* keyName) {
    wstr_clear(out);
    wstr_append_ch(out, L'(');
    tz_append_utc_offset(out, -bias);      // Windows bias = UTC - local
    wstr_append(out, L") ");
    const wchar_t* label = (standardName && *standardName) ? standardName : keyName;
    wstr_append(out, label ? label : L"");
}

static int tz_cmp_ci(const WStr* a, const WStr* b) {
    const wchar_t* pa = wstr_c(a);
    const wchar_t* pb = wstr_c(b);
    int i = 0;
    for (;;) {
        wchar_t ca = pa[i];
        wchar_t cb = pb[i];
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca + 32);
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb + 32);
        if (ca != cb) return (ca < cb) ? -1 : 1;
        if (!ca) return 0;
        ++i;
    }
}

// Finds the dynamic time zone record for a Windows key or a known IANA alias.
static bool tz_find_record(const wchar_t* keyName, DYNAMIC_TIME_ZONE_INFORMATION* out) {
    const wchar_t* want = tz_windows_key_for(keyName);
    if (!want || !*want) return false;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    for (DWORD i = 0; ; ++i) {
        xmemzero(&dtzi, sizeof(dtzi));
        DWORD r = EnumDynamicTimeZoneInformation(i, &dtzi);
        if (r != ERROR_SUCCESS) break;
        if (weq_ci(dtzi.TimeZoneKeyName, want)) {
            if (out) *out = dtzi;
            return true;
        }
    }
    return false;
}

// nth (1..5, 5 = last) weekday of a month - the offset comes from Windows.
static bool tz_rule_date(const SYSTEMTIME* rule, int year, int* py, int* pm, int* pd) {
    int month = (int)rule->wMonth;
    int nth = (int)rule->wDay;
    int dow = (int)rule->wDayOfWeek;
    if (month < 1 || month > 12 || nth < 1 || dow > 6) return false;
    DateTime first;
    dt_zero(&first);
    first.year = (WORD)year;
    first.month = (WORD)month;
    first.day = 1;
    int firstDow = dt_day_of_week(&first);
    int day = 1 + ((dow - firstDow + 7) % 7) + (nth - 1) * 7;
    int limit = dt_days_in_month(year, month);
    while (day > limit && day > 7) day -= 7;
    if (day < 1 || day > limit) return false;
    *py = year;
    *pm = month;
    *pd = day;
    return true;
}

static const wchar_t* const kDowNames[7] = { L"SU", L"MO", L"TU", L"WE", L"TH", L"FR", L"SA" };

// ------------------------------------------------------------------ API ----

int ics_zone_enumerate(IcsZoneInfo* out, int cap) {
    if (cap < 0) cap = 0;
    IcsZoneInfo* list = NULL;
    if (out && cap > 0) {
        list = (IcsZoneInfo*)xmalloc(sizeof(IcsZoneInfo) * (size_t)cap);
        xmemzero(list, sizeof(IcsZoneInfo) * (size_t)cap);
    }
    int stored = 0;
    int total = 0;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    for (DWORD i = 0; ; ++i) {
        xmemzero(&dtzi, sizeof(dtzi));
        if (EnumDynamicTimeZoneInformation(i, &dtzi) != ERROR_SUCCESS) break;
        ++total;
        if (list && stored < cap) {
            IcsZoneInfo* info = &list[stored];
            wstr_init(&info->keyName);
            wstr_init(&info->display);
            wstr_set(&info->keyName, dtzi.TimeZoneKeyName);
            tz_build_display(&info->display, (int)dtzi.Bias, dtzi.StandardName, dtzi.TimeZoneKeyName);
            info->biasMinutes = (int)dtzi.Bias;
            ++stored;
        }
    }
    // Insertion sort: bias ascending, then display name (case-insensitive).
    for (int i = 1; i < stored; ++i) {
        IcsZoneInfo key = list[i];
        int j = i - 1;
        while (j >= 0 &&
               (list[j].biasMinutes > key.biasMinutes ||
                (list[j].biasMinutes == key.biasMinutes && tz_cmp_ci(&list[j].display, &key.display) > 0))) {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = key;
    }
    if (list) {
        for (int i = 0; i < stored; ++i) out[i] = list[i];
        xfree(list);
    }
    return total;
}

void ics_zone_info_free(IcsZoneInfo* info) {
    if (!info) return;
    wstr_free(&info->keyName);
    wstr_free(&info->display);
    info->biasMinutes = 0;
}

bool ics_zone_find(const wchar_t* keyName, IcsZoneInfo* out) {
    if (!keyName || !*keyName || !out) return false;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    if (!tz_find_record(keyName, &dtzi)) return false;
    wstr_set(&out->keyName, dtzi.TimeZoneKeyName);
    tz_build_display(&out->display, (int)dtzi.Bias, dtzi.StandardName, dtzi.TimeZoneKeyName);
    out->biasMinutes = (int)dtzi.Bias;
    return true;
}

bool ics_zone_local_to_utc(const wchar_t* keyName, DateTime* d) {
    if (!d) return false;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    if (!tz_find_record(keyName, &dtzi)) return false;
    SYSTEMTIME local, utc;
    xmemzero(&utc, sizeof(utc));
    dt_to_systemtime(d, &local);
    if (!TzSpecificLocalTimeToSystemTimeEx(&dtzi, &local, &utc)) return false;
    dt_from_systemtime(d, &utc);
    d->utc = true;
    d->dateOnly = false;
    return true;
}

bool ics_zone_utc_to_local(const wchar_t* keyName, DateTime* d) {
    if (!d) return false;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    if (!tz_find_record(keyName, &dtzi)) return false;
    SYSTEMTIME utc, local;
    xmemzero(&local, sizeof(local));
    dt_to_systemtime(d, &utc);
    if (!SystemTimeToTzSpecificLocalTimeEx(&dtzi, &utc, &local)) return false;
    dt_from_systemtime(d, &local);
    d->utc = false;
    d->dateOnly = false;
    return true;
}

static void tz_line(WStr* out, const wchar_t* text) {
    wstr_append(out, text);
    wstr_append(out, L"\r\n");
}

static void tz_component(WStr* out, const wchar_t* name,
                         int year, int month, int day,
                         int hour, int minute, int second,
                         int fromOffset, int toOffset,
                         const wchar_t* rrule) {
    WStr line;
    wstr_init(&line);
    wstr_set(&line, L"BEGIN:");
    wstr_append(&line, name);
    tz_line(out, wstr_c(&line));

    DateTime d;
    dt_zero(&d);
    d.year = (WORD)year;
    d.month = (WORD)month;
    d.day = (WORD)day;
    d.hour = (WORD)hour;
    d.minute = (WORD)minute;
    d.second = (WORD)second;
    wchar_t stamp[24];
    stamp[0] = 0;
    dt_to_ics(&d, stamp, ICSG_ARRAY_COUNT(stamp));
    wstr_set(&line, L"DTSTART:");
    wstr_append(&line, stamp);
    tz_line(out, wstr_c(&line));

    wstr_set(&line, L"TZOFFSETFROM:");
    tz_append_ics_offset(&line, fromOffset);
    tz_line(out, wstr_c(&line));

    wstr_set(&line, L"TZOFFSETTO:");
    tz_append_ics_offset(&line, toOffset);
    tz_line(out, wstr_c(&line));

    if (rrule && *rrule) tz_line(out, rrule);

    wstr_set(&line, L"END:");
    wstr_append(&line, name);
    tz_line(out, wstr_c(&line));
    wstr_free(&line);
}

static void tz_rule_rrule(WStr* out, const SYSTEMTIME* rule) {
    // RRULE:FREQ=YEARLY;BYMONTH=m;BYDAY=<n><DOW>  (n = -1 for the last weekday)
    wstr_set(out, L"RRULE:FREQ=YEARLY;BYMONTH=");
    wstr_append_num(out, (long long)rule->wMonth, 1);
    wstr_append(out, L";BYDAY=");
    if (rule->wDay == 5) wstr_append(out, L"-1");
    else wstr_append_num(out, (long long)rule->wDay, 1);
    int dow = (int)rule->wDayOfWeek;
    if (dow < 0 || dow > 6) dow = 0;
    wstr_append(out, kDowNames[dow]);
}

bool ics_zone_vtimezone(const wchar_t* keyName, int year, WStr* outText) {
    if (!keyName || !*keyName || !outText) return false;
    if (year < 1601 || year > 9999) return false;

    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    if (!tz_find_record(keyName, &dtzi)) return false;

    TIME_ZONE_INFORMATION tzi;
    xmemzero(&tzi, sizeof(tzi));
    if (!GetTimeZoneInformationForYear((USHORT)year, &dtzi, &tzi)) return false;

    int stdOffset = -(int)(tzi.Bias + tzi.StandardBias);
    int dstOffset = -(int)(tzi.Bias + tzi.DaylightBias);
    bool hasDst = (tzi.DaylightDate.wMonth != 0) && (dstOffset != stdOffset);

    wstr_clear(outText);
    tz_line(outText, L"BEGIN:VTIMEZONE");

    WStr line;
    wstr_init(&line);
    wstr_set(&line, L"TZID:");
    wstr_append(&line, keyName);          // verbatim, so an IANA TZID stays valid
    tz_line(outText, wstr_c(&line));

    WStr rrule;
    wstr_init(&rrule);

    if (hasDst) {
        int sy = 0, sm = 0, sd = 0;
        int dy = 0, dm = 0, dd = 0;
        bool stdOk = tz_rule_date(&tzi.StandardDate, year, &sy, &sm, &sd);
        bool dstOk = tz_rule_date(&tzi.DaylightDate, year, &dy, &dm, &dd);
        if (stdOk) tz_rule_rrule(&rrule, &tzi.StandardDate);
        tz_component(outText, L"STANDARD",
                     stdOk ? sy : year, stdOk ? sm : 1, stdOk ? sd : 1,
                     stdOk ? (int)tzi.StandardDate.wHour : 0,
                     stdOk ? (int)tzi.StandardDate.wMinute : 0,
                     stdOk ? (int)tzi.StandardDate.wSecond : 0,
                     dstOffset, stdOffset,
                     stdOk ? wstr_c(&rrule) : L"RRULE:FREQ=YEARLY");
        if (dstOk) tz_rule_rrule(&rrule, &tzi.DaylightDate);
        tz_component(outText, L"DAYLIGHT",
                     dstOk ? dy : year, dstOk ? dm : 1, dstOk ? dd : 1,
                     dstOk ? (int)tzi.DaylightDate.wHour : 0,
                     dstOk ? (int)tzi.DaylightDate.wMinute : 0,
                     dstOk ? (int)tzi.DaylightDate.wSecond : 0,
                     stdOffset, dstOffset,
                     dstOk ? wstr_c(&rrule) : L"RRULE:FREQ=YEARLY");
    } else {
        // Fixed offset zone: one STANDARD component that repeats every year.
        tz_component(outText, L"STANDARD", year, 1, 1, 0, 0, 0,
                     stdOffset, stdOffset, L"RRULE:FREQ=YEARLY");
    }

    tz_line(outText, L"END:VTIMEZONE");
    wstr_free(&rrule);
    wstr_free(&line);
    return true;
}
