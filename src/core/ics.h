// ics.h - RFC 5545 calendar model, serializer and tolerant parser.
// FROZEN CONTRACT (owner: Lead). Implemented by the core workstream.
#pragma once
#ifndef ICSG_ICS_H
#define ICSG_ICS_H

#include "base.h"
#include "dt.h"

#define ICSG_MAX_EVENTS    1024
#define ICSG_MAX_ATTENDEES 8
#define ICSG_MAX_ALARMS    4
#define ICSG_MAX_DATES     8
#define ICSG_MAX_ZONES     160
#define ICSG_UID_DOMAIN    L"ics-generate"

enum IcsTimeMode {
    ICS_TIME_FLOAT = 0,   // local wall clock, no Z, no TZID  (default, most portable)
    ICS_TIME_UTC   = 1,   // 'Z' suffix, value is UTC
    ICS_TIME_TZID  = 2    // ';TZID=<windows zone key>' wall clock + generated VTIMEZONE
};

enum IcsFreq {
    ICS_FREQ_NONE = 0, ICS_FREQ_DAILY, ICS_FREQ_WEEKLY, ICS_FREQ_MONTHLY, ICS_FREQ_YEARLY
};

enum IcsStatus { ICS_STATUS_NONE = 0, ICS_STATUS_TENTATIVE, ICS_STATUS_CONFIRMED, ICS_STATUS_CANCELLED };
enum IcsTransp { ICS_TRANSP_NONE = 0, ICS_TRANSP_OPAQUE, ICS_TRANSP_TRANSPARENT };
enum IcsClass  { ICS_CLASS_NONE  = 0, ICS_CLASS_PUBLIC, ICS_CLASS_PRIVATE, ICS_CLASS_CONFIDENTIAL };

// RRULE BYDAY bit mask: bit 0 = SU, 1 = MO, 2 = TU, 3 = WE, 4 = TH, 5 = FR, 6 = SA.
#define ICSG_BYDAY_SU 0x01
#define ICSG_BYDAY_MO 0x02
#define ICSG_BYDAY_TU 0x04
#define ICSG_BYDAY_WE 0x08
#define ICSG_BYDAY_TH 0x10
#define ICSG_BYDAY_FR 0x20
#define ICSG_BYDAY_SA 0x40

struct IcsAttendee {
    WStr email;     // "user@example.com"; the mailto: prefix is added on write
    WStr name;      // CN parameter, optional
    bool rsvp;
};

struct IcsAlarm {
    bool enabled;
    int  minutesBefore;   // 0 = at start, 15 = 15 minutes before; max 525600 (1 year)
    WStr description;     // empty -> the event summary is used
};

struct IcsEvent {
    WStr uid;
    WStr summary;
    WStr description;
    WStr location;
    WStr url;
    WStr categories;        // comma separated, written as a single CATEGORIES property
    WStr organizer;         // "user@example.com"; the mailto: prefix is added on write
    WStr organizerName;     // CN parameter, optional
    WStr tzid;              // Windows time zone key when timeMode == ICS_TIME_TZID
    IcsAttendee attendees[ICSG_MAX_ATTENDEES];
    int  attendeeCount;

    DateTime start;
    DateTime end;           // exclusive end for timed events. All-day events with
                            // end <= start are written as start + 1 day.
    bool allDay;
    IcsTimeMode timeMode;

    IcsFreq freq;
    int  interval;          // forced to >= 1 when freq != NONE
    int  count;             // 0 = unset
    DateTime until;         // used when hasUntil
    bool hasUntil;
    int  bydayMask;
    int  bymonthday;        // 0 = unset, else 1..31
    DateTime exdates[ICSG_MAX_DATES];   // EXDATE, written with the event's time mode
    int  exdateCount;
    DateTime rdates[ICSG_MAX_DATES];    // RDATE
    int  rdateCount;

    IcsAlarm alarms[ICSG_MAX_ALARMS];
    int  alarmCount;

    IcsStatus status;
    IcsTransp transp;
    IcsClass  klass;
    int  priority;          // 0 = unset, else 1..9
    int  sequence;

    DateTime created;       // omitted when zero
    DateTime lastModified;  // omitted when zero
    DateTime dtstamp;
};

struct IcsCalendar {
    WStr prodid;
    WStr version;
    WStr calscale;
    WStr method;            // optional METHOD, empty = omitted
    WStr name;              // X-WR-CALNAME, optional
    WStr timezone;          // X-WR-TIMEZONE, optional
    bool saveAsUtc;         // serializer converts every event to UTC (uses each
                            // event's zone, falls back to the machine zone)
    IcsEvent* events;
    int count;
    int cap;
};

// ------------------------------------------------------------- lifecycle ----
void ics_calendar_init(IcsCalendar* cal);      // PRODID/VERSION/CALSCALE defaults
void ics_calendar_free(IcsCalendar* cal);
void ics_calendar_clear(IcsCalendar* cal);     // frees and re-initialises
void ics_event_init(IcsEvent* ev);             // zero + defaults (no UID)
void ics_event_free(IcsEvent* ev);             // frees the WStr fields only
// Appends a fully initialised event (fresh UID, DTSTAMP = now, CREATED = now,
// start = now rounded up to the next full hour, end = start + 1 hour) and returns
// it, or NULL at the ICSG_MAX_EVENTS limit.
IcsEvent* ics_calendar_add(IcsCalendar* cal);
// Appends a deep copy of src (fresh UID when newUid is true) and returns it.
IcsEvent* ics_calendar_duplicate(IcsCalendar* cal, const IcsEvent* src, bool newUid);
bool ics_calendar_remove(IcsCalendar* cal, int index);
void ics_event_copy(IcsEvent* dst, const IcsEvent* src);   // deep copy
// Fills a fresh UID: "<utc-stamp>-<counter>-<pid>@ics-generate".
void ics_event_make_uid(WStr* outUid);
// Moves events[index] by 'delta' (-1 / +1), keeping the selection target valid.
bool ics_calendar_move(IcsCalendar* cal, int index, int delta);

// ---------------------------------------------------------- serialisation ----
// Renders the calendar as RFC 5545 UTF-8 text with CRLF line endings and 75 octet
// folding. Returns the byte count required (excluding a terminating NUL), or -1 on
// a malformed model. When out != NULL, 'cap' bytes are available and the text plus a
// NUL terminator is written only if cap >= required + 1.
// VTIMEZONE components are emitted once per distinct TZID used by an event.
int ics_serialize(const IcsCalendar* cal, char* out, int cap);
bool ics_save_file(const IcsCalendar* cal, const wchar_t* path);
// Serialises a single event as a standalone VCALENDAR ("export selection").
bool ics_save_event_file(const IcsEvent* ev, const wchar_t* path);

// ---------------------------------------------------------------- parsing ----
// Tolerant RFC 5545 reader: unfolding, CRLF/LF/CR, BOM, quoted parameters, unknown
// properties/components skipped, missing VCALENDAR wrapper tolerated. Any previous
// content of 'cal' is freed first. Returns false when the text is neither a calendar
// nor parseable at all.
bool ics_parse(const char* utf8, int size, IcsCalendar* cal);
bool ics_load_file(IcsCalendar* cal, const wchar_t* path);
// Appends every VEVENT of 'src' to 'dst' (deep copy) and frees 'src'.
int  ics_merge(IcsCalendar* dst, IcsCalendar* src);

// ---------------------------------------------------------------- timezone ----
struct IcsZoneInfo {
    WStr keyName;      // Windows key, e.g. "China Standard Time" (used as TZID)
    WStr display;      // "(UTC+08:00) 北京，重庆，香港特别行政区，乌鲁木齐"
    int  biasMinutes;  // current UTC bias in minutes (informational)
};
// Fills up to 'cap' entries from the Windows dynamic time zone database, sorted by
// bias then name, and returns the number available (may exceed cap).
int  ics_zone_enumerate(IcsZoneInfo* out, int cap);
void ics_zone_info_free(IcsZoneInfo* info);
bool ics_zone_find(const wchar_t* keyName, IcsZoneInfo* out);
// DST-aware conversion between the machine's wall clock and UTC.
bool ics_zone_local_to_utc(const wchar_t* keyName, DateTime* d);
bool ics_zone_utc_to_local(const wchar_t* keyName, DateTime* d);
// Renders the VTIMEZONE component for 'year' as unfolded ICS lines (CRLF separated).
bool ics_zone_vtimezone(const wchar_t* keyName, int year, WStr* outText);

// ------------------------------------------------------ shared properties ----
int  ics_escape_text(const wchar_t* src, int srcLen, wchar_t* out, int outCap);
int  ics_unescape_text(const wchar_t* src, int srcLen, wchar_t* out, int outCap);
int  ics_trigger_from_minutes(int minutes, wchar_t* out, int outCap);
void ics_rrule_text(const IcsEvent* ev, WStr* out);
// Human readable repetition summary, e.g. "每周(周一,周三)" or "". Used by the UI.
void ics_rrule_display(const IcsEvent* ev, WStr* out);
// Human readable reminder, e.g. "提前15分钟". Empty when no alarm is enabled.
void ics_alarm_display(const IcsEvent* ev, WStr* out);
const wchar_t* ics_freq_name(IcsFreq freq);
const wchar_t* ics_status_name(IcsStatus st);
const wchar_t* ics_transp_name(IcsTransp tp);
const wchar_t* ics_class_name(IcsClass k);
IcsFreq    ics_freq_parse(const wchar_t* text);
IcsStatus  ics_status_parse(const wchar_t* text);
IcsTransp  ics_transp_parse(const wchar_t* text);
IcsClass   ics_class_parse(const wchar_t* text);

#endif // ICSG_ICS_H
