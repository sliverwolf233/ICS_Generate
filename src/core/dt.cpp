// dt.cpp - proleptic Gregorian calendar arithmetic without the CRT.
// Contract: src/core/dt.h (frozen).
#include "dt.h"

// ------------------------------------------------------ civil calendar ------
// Howard Hinnant's days_from_civil / civil_from_days, valid for the whole
// proleptic Gregorian calendar (year 1..9999 is all we are ever handed).

static long long dt_days_from_civil(int y, int m, int d) {
    long long yy = (long long)y - ((m <= 2) ? 1 : 0);
    long long era = (yy >= 0 ? yy : yy - 399) / 400;
    long long yoe = yy - era * 400;                                       // [0,399]
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;       // [0,365]
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                // [0,146096]
    return era * 146097 + doe - 719468;
}

static void dt_civil_from_days(long long z, int* py, int* pm, int* pd) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long dd = doy - (153 * mp + 2) / 5 + 1;
    long long mm = mp + (mp < 10 ? 3 : -9);
    *py = (int)(y + ((mm <= 2) ? 1 : 0));
    *pm = (int)mm;
    *pd = (int)dd;
}

int dt_days_in_month(int year, int month) {
    static const int table[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return table[month - 1];
}

// ----------------------------------------------------------------- basics ---

void dt_zero(DateTime* d) {
    if (!d) return;
    xmemzero(d, sizeof(DateTime));
}

void dt_now_utc(DateTime* d) {
    if (!d) return;
    SYSTEMTIME st;
    xmemzero(&st, sizeof(st));
    GetSystemTime(&st);
    dt_from_systemtime(d, &st);
    d->utc = true;
}

void dt_now_local(DateTime* d) {
    if (!d) return;
    SYSTEMTIME st;
    xmemzero(&st, sizeof(st));
    GetLocalTime(&st);
    dt_from_systemtime(d, &st);
    d->utc = false;
}

bool dt_is_valid(const DateTime* d) {
    if (!d) return false;
    if (d->year < 1601 || d->year > 9999) return false;
    if (d->month < 1 || d->month > 12) return false;
    if (d->day < 1 || d->day > (WORD)dt_days_in_month(d->year, d->month)) return false;
    if (d->hour > 23 || d->minute > 59 || d->second > 60) return false;
    return true;
}

bool dt_is_zero(const DateTime* d) {
    if (!d) return true;
    return d->year == 0 && d->month == 0 && d->day == 0 &&
           d->hour == 0 && d->minute == 0 && d->second == 0;
}

int dt_compare(const DateTime* a, const DateTime* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    if (a->year != b->year) return (a->year < b->year) ? -1 : 1;
    if (a->month != b->month) return (a->month < b->month) ? -1 : 1;
    if (a->day != b->day) return (a->day < b->day) ? -1 : 1;
    if (a->hour != b->hour) return (a->hour < b->hour) ? -1 : 1;
    if (a->minute != b->minute) return (a->minute < b->minute) ? -1 : 1;
    if (a->second != b->second) return (a->second < b->second) ? -1 : 1;
    return 0;
}

int dt_day_of_week(const DateTime* d) {
    if (!d) return 0;
    long long z = dt_days_from_civil((int)d->year, (int)d->month, (int)d->day);
    // 1970-01-01 was a Thursday (index 4 with 0 = Sunday).
    long long dow = (z + 4) % 7;
    if (dow < 0) dow += 7;
    return (int)dow;
}

// -------------------------------------------------------------- arithmetic --

void dt_add_days(DateTime* d, int days) {
    if (!d || days == 0) return;
    long long z = dt_days_from_civil((int)d->year, (int)d->month, (int)d->day) + (long long)days;
    int y = 0, m = 0, dd = 0;
    dt_civil_from_days(z, &y, &m, &dd);
    if (y < 1601) { y = 1601; m = 1; dd = 1; }
    else if (y > 9999) { y = 9999; m = 12; dd = 31; }
    d->year = (WORD)y;
    d->month = (WORD)m;
    d->day = (WORD)dd;
}

void dt_add_months(DateTime* d, int months) {
    if (!d || months == 0) return;
    long long total = (long long)d->year * 12 + (long long)(d->month - 1) + (long long)months;
    if (total < 0) total = 0;
    int year = (int)(total / 12);
    int month = (int)(total % 12) + 1;
    if (year < 1601) { year = 1601; month = 1; }
    if (year > 9999) { year = 9999; month = 12; }
    int dim = dt_days_in_month(year, month);
    int day = (int)d->day;
    if (day > dim) day = dim;
    if (day < 1) day = 1;
    d->year = (WORD)year;
    d->month = (WORD)month;
    d->day = (WORD)day;
}

void dt_add_minutes(DateTime* d, int minutes) {
    if (!d || minutes == 0) return;
    long long total = dt_days_from_civil((int)d->year, (int)d->month, (int)d->day) * 1440LL
                    + (long long)d->hour * 60LL + (long long)d->minute
                    + (long long)minutes;
    long long z = total / 1440;
    int rem = (int)(total % 1440);
    if (rem < 0) { rem += 1440; z -= 1; }
    int y = 0, m = 0, dd = 0;
    dt_civil_from_days(z, &y, &m, &dd);
    if (y < 1601) { y = 1601; m = 1; dd = 1; rem = 0; }
    else if (y > 9999) { y = 9999; m = 12; dd = 31; rem = 23 * 60 + 59; }
    d->year = (WORD)y;
    d->month = (WORD)m;
    d->day = (WORD)dd;
    d->hour = (WORD)(rem / 60);
    d->minute = (WORD)(rem % 60);
}

// -------------------------------------------------------------- timezones ---
// The non-Ex Win32 helpers use the machine's current time zone.

bool dt_local_to_utc(DateTime* d) {
    if (!d) return false;
    SYSTEMTIME local, utc;
    dt_to_systemtime(d, &local);
    xmemzero(&utc, sizeof(utc));
    if (!TzSpecificLocalTimeToSystemTime(NULL, &local, &utc)) return false;
    dt_from_systemtime(d, &utc);
    d->utc = true;
    return true;
}

bool dt_utc_to_local(DateTime* d) {
    if (!d) return false;
    SYSTEMTIME utc, local;
    dt_to_systemtime(d, &utc);
    xmemzero(&local, sizeof(local));
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) return false;
    dt_from_systemtime(d, &local);
    d->utc = false;
    return true;
}

// ------------------------------------------------------------ ICS forms -----

static int dt_put_num(wchar_t* out, int value, int digits) {
    for (int i = digits - 1; i >= 0; --i) {
        out[i] = (wchar_t)(L'0' + value % 10);
        value /= 10;
    }
    return digits;
}

int dt_to_ics(const DateTime* d, wchar_t* out, int outCap) {
    if (!d || !out) return -1;
    int len = d->dateOnly ? 8 : (14 + (d->utc ? 1 : 0));
    if (outCap < len + 1) return -1;
    int n = 0;
    n += dt_put_num(out + n, (int)d->year, 4);
    n += dt_put_num(out + n, (int)d->month, 2);
    n += dt_put_num(out + n, (int)d->day, 2);
    if (!d->dateOnly) {
        out[n++] = L'T';
        n += dt_put_num(out + n, (int)d->hour, 2);
        n += dt_put_num(out + n, (int)d->minute, 2);
        n += dt_put_num(out + n, (int)d->second, 2);
        if (d->utc) out[n++] = L'Z';
    }
    out[n] = 0;
    return n;
}

bool dt_from_ics(const wchar_t* text, int len, DateTime* out) {
    if (!text || !out || len <= 0) return false;
    wchar_t digits[16];
    int nd = 0;
    bool utc = false;
    int i = 0;
    while (i < len) {
        wchar_t c = text[i];
        if (c >= L'0' && c <= L'9') {
            if (nd < 14) digits[nd++] = c;
            ++i;
            continue;
        }
        if (c == L'-' || c == L'.' || c == L':') { ++i; continue; }
        if (c == L'T' || c == L't' || c == L' ') { ++i; continue; }   // date-time separator
        if ((c == L'Z' || c == L'z') && nd >= 8) { utc = true; ++i; break; }
        break;
    }
    bool dateOnly = (nd == 8);
    if (!dateOnly && nd < 14) return false;
    DateTime d;
    dt_zero(&d);
    d.year = (WORD)((digits[0] - L'0') * 1000 + (digits[1] - L'0') * 100 +
                    (digits[2] - L'0') * 10 + (digits[3] - L'0'));
    d.month = (WORD)((digits[4] - L'0') * 10 + (digits[5] - L'0'));
    d.day = (WORD)((digits[6] - L'0') * 10 + (digits[7] - L'0'));
    if (!dateOnly) {
        d.hour = (WORD)((digits[8] - L'0') * 10 + (digits[9] - L'0'));
        d.minute = (WORD)((digits[10] - L'0') * 10 + (digits[11] - L'0'));
        d.second = (WORD)((digits[12] - L'0') * 10 + (digits[13] - L'0'));
    }
    d.utc = dateOnly ? false : utc;
    d.dateOnly = dateOnly;
    if (!dt_is_valid(&d)) return false;
    *out = d;
    return true;
}

int dt_to_display(const DateTime* d, wchar_t* out, int outCap) {
    if (!d || !out) return -1;
    int len = d->dateOnly ? 10 : 16;
    if (outCap < len + 1) return -1;
    int n = 0;
    n += dt_put_num(out + n, (int)d->year, 4);
    out[n++] = L'-';
    n += dt_put_num(out + n, (int)d->month, 2);
    out[n++] = L'-';
    n += dt_put_num(out + n, (int)d->day, 2);
    if (!d->dateOnly) {
        out[n++] = L' ';
        n += dt_put_num(out + n, (int)d->hour, 2);
        out[n++] = L':';
        n += dt_put_num(out + n, (int)d->minute, 2);
    }
    out[n] = 0;
    return n;
}

// ------------------------------------------------------------ SYSTEMTIME ----

void dt_to_systemtime(const DateTime* d, SYSTEMTIME* st) {
    if (!st) return;
    xmemzero(st, sizeof(SYSTEMTIME));
    if (!d) return;
    st->wYear = d->year;
    st->wMonth = d->month;
    st->wDay = d->day;
    st->wHour = d->hour;
    st->wMinute = d->minute;
    st->wSecond = d->second;
    st->wMilliseconds = 0;
    st->wDayOfWeek = (WORD)dt_day_of_week(d);
}

void dt_from_systemtime(DateTime* d, const SYSTEMTIME* st) {
    if (!d) return;
    dt_zero(d);
    if (!st) return;
    d->year = st->wYear;
    d->month = st->wMonth;
    d->day = st->wDay;
    d->hour = st->wHour;
    d->minute = st->wMinute;
    d->second = st->wSecond;
    d->utc = false;
    d->dateOnly = false;
}
