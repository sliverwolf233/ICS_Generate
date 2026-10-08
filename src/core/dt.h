// dt.h - calendar date/time value without the CRT. FROZEN CONTRACT (owner: Lead).
#pragma once
#ifndef ICSG_DT_H
#define ICSG_DT_H

#include "base.h"

// Proleptic Gregorian wall-clock value. No timezone conversion happens here;
// 'utc' only records whether the ICS form carries the trailing 'Z'.
struct DateTime {
    WORD year;      // 1601..9999
    WORD month;     // 1..12
    WORD day;       // 1..31
    WORD hour;      // 0..23
    WORD minute;    // 0..59
    WORD second;    // 0..60 (leap second tolerated on parse)
    bool utc;       // ICS value carried 'Z'
    bool dateOnly;  // ICS VALUE=DATE (all-day)
};

void dt_zero(DateTime* d);
void dt_now_utc(DateTime* d);
void dt_now_local(DateTime* d);
bool dt_is_valid(const DateTime* d);
bool dt_is_zero(const DateTime* d);

// Returns <0, 0, >0. 'utc' and 'dateOnly' are ignored for ordering.
int  dt_compare(const DateTime* a, const DateTime* b);

// Calendar arithmetic on wall-clock fields (no timezone involved).
void dt_add_days(DateTime* d, int days);
void dt_add_months(DateTime* d, int months);
void dt_add_minutes(DateTime* d, int minutes);
int  dt_day_of_week(const DateTime* d);            // 0 = Sunday .. 6 = Saturday
int  dt_days_in_month(int year, int month);
// Converts local wall clock of the machine's current timezone to UTC using the
// Win32 timezone API (DST aware for the given date). Returns false when Windows
// has no timezone information.
bool dt_local_to_utc(DateTime* d);
// Converts UTC to the machine's local wall clock (DST aware).
bool dt_utc_to_local(DateTime* d);

// ICS forms: 20240102T030405Z | 20240102T030405 | 20240102.
// Writes at most outCap wchar_t plus the terminator; returns the length written,
// or -1 when outCap is too small.
int  dt_to_ics(const DateTime* d, wchar_t* out, int outCap);
// Parses the forms above (a trailing 'Z' sets utc, 8 digits sets dateOnly).
// Accepts '.', '-' and ':' separators defensively. Never reads past len.
bool dt_from_ics(const wchar_t* text, int len, DateTime* out);
// "2024-01-02 03:04" (all-day: "2024-01-02"), length returned as above.
int  dt_to_display(const DateTime* d, wchar_t* out, int outCap);

void dt_to_systemtime(const DateTime* d, SYSTEMTIME* st);
void dt_from_systemtime(DateTime* d, const SYSTEMTIME* st);   // utc=false, dateOnly=false

#endif // ICSG_DT_H
