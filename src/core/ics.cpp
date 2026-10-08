// ics.cpp - RFC 5545 calendar model, serialiser and shared helpers.
// Contract: src/core/ics.h (frozen). CRT-free: Windows headers only.
#include "ics.h"

// --------------------------------------------------------------- uid state --
// POD, zero-initialised: no dynamic initialiser (contract 2).
static volatile LONG icsg_uid_counter = 0;

// ------------------------------------------------------------------ sink ----
// Growable UTF-8 byte buffer. Folds content lines at 75 octets, one code point
// at a time, so multi-byte UTF-8 sequences are never split.

struct IcsSink {
    char* out;
    int   cap;
    int   len;
    int   lineOct;      // octets on the current (continuation) line
};

static void sink_grow(IcsSink* s, int need) {
    if (s->len + need <= s->cap) return;
    int newCap = s->cap ? s->cap : 512;
    while (newCap < s->len + need) newCap *= 2;
    s->out = (char*)xrealloc(s->out, (size_t)newCap);
    s->cap = newCap;
}

static void sink_raw(IcsSink* s, const char* bytes, int n) {
    if (n <= 0) return;
    sink_grow(s, n);
    xmemcopy(s->out + s->len, bytes, (size_t)n);
    s->len += n;
}

// Writes one code point, folding first when it would exceed 75 octets.
static void sink_code_point(IcsSink* s, const wchar_t* src, int srcLen, int* pos) {
    int i = *pos;
    unsigned int cp = (unsigned int)(unsigned short)src[i];
    int adv = 1;
    if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < srcLen) {
        unsigned int lo = (unsigned int)(unsigned short)src[i + 1];
        if (lo >= 0xDC00u && lo <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            adv = 2;
        }
    }
    char tmp[4];
    int n;
    if (cp < 0x80u) { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800u) {
        tmp[0] = (char)(0xC0u | (cp >> 6));
        tmp[1] = (char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        tmp[0] = (char)(0xE0u | (cp >> 12));
        tmp[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        tmp[2] = (char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        tmp[0] = (char)(0xF0u | (cp >> 18));
        tmp[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        tmp[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        tmp[3] = (char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    if (s->lineOct > 0 && s->lineOct + n > 75) {
        sink_raw(s, "\r\n ", 3);
        s->lineOct = 1;
    }
    sink_raw(s, tmp, n);
    s->lineOct += n;
    *pos = i + adv;
}

// Writes one unfolded logical line (CRLF terminated) through the folder.
static void sink_line(IcsSink* s, const WStr* line) {
    const wchar_t* text = wstr_c(line);
    int len = wstr_is_empty(line) ? 0 : line->len;
    s->lineOct = 0;
    int i = 0;
    while (i < len) sink_code_point(s, text, len, &i);
    sink_raw(s, "\r\n", 2);
}

// ------------------------------------------------------------- text escape --

int ics_escape_text(const wchar_t* src, int srcLen, wchar_t* out, int outCap) {
    if (!src || srcLen <= 0) return 0;
    int need = 0;
    for (int i = 0; i < srcLen; ++i) {
        wchar_t c = src[i];
        if (c == L'\\' || c == L';' || c == L',' || c == L'\n') need += 2;
        else if (c == L'\r') need += 0;      // CR is dropped on write
        else need += 1;
    }
    if (!out || outCap < need) return need;
    int n = 0;
    for (int i = 0; i < srcLen; ++i) {
        wchar_t c = src[i];
        if (c == L'\\' || c == L';' || c == L',') {
            out[n++] = L'\\';
            out[n++] = c;
        } else if (c == L'\n') {
            out[n++] = L'\\';
            out[n++] = L'n';
        } else if (c == L'\r') {
            // dropped
        } else {
            out[n++] = c;
        }
    }
    return n;
}

int ics_unescape_text(const wchar_t* src, int srcLen, wchar_t* out, int outCap) {
    if (!src || srcLen <= 0) return 0;
    int need = 0;
    for (int i = 0; i < srcLen; ++i) if (src[i] != L'\r') ++need;  // worst case
    if (!out || outCap < need) return need;
    int n = 0;
    for (int i = 0; i < srcLen; ++i) {
        wchar_t c = src[i];
        if (c == L'\\' && i + 1 < srcLen) {
            wchar_t e = src[i + 1];
            if (e == L'n' || e == L'N') out[n++] = L'\n';
            else if (e != L'\r') out[n++] = e;   // \\ \; \, and pass-through
            ++i;
        } else if (c != L'\r') {
            out[n++] = c;
        }
    }
    return n;
}

// helper: append the escaped form of a text value to a WStr
static void ics_append_escaped(WStr* dst, const WStr* value) {
    if (wstr_is_empty(value)) return;
    int need = ics_escape_text(value->data, value->len, NULL, 0);
    if (!wstr_reserve(dst, dst->len + need + 1)) return;
    ics_escape_text(value->data, value->len, dst->data + dst->len, need);
    dst->len += need;
    dst->data[dst->len] = 0;
}

// ------------------------------------------------------------------- uid ----

static void uid_hex(WStr* out, unsigned long long value, int digits) {
    static const wchar_t hexDigits[] = L"0123456789abcdef";
    wchar_t buf[16];
    if (digits > 16) digits = 16;
    for (int i = digits - 1; i >= 0; --i) {
        buf[i] = hexDigits[value & 0xFULL];
        value >>= 4;
    }
    wstr_append_n(out, buf, digits);
}

void ics_event_make_uid(WStr* outUid) {
    LARGE_INTEGER ft;
    ft.QuadPart = 0;
    GetSystemTimeAsFileTime((FILETIME*)&ft);
    long n = InterlockedIncrement(&icsg_uid_counter);
    wstr_clear(outUid);
    uid_hex(outUid, (unsigned long long)ft.QuadPart, 16);
    wstr_append_ch(outUid, L'-');
    uid_hex(outUid, (unsigned long long)(unsigned int)n, 8);
    wstr_append_ch(outUid, L'-');
    uid_hex(outUid, (unsigned long long)GetCurrentProcessId(), 8);
    wstr_append_ch(outUid, L'@');
    wstr_append(outUid, ICSG_UID_DOMAIN);
}

// ------------------------------------------------------------- lifecycle ----

void ics_event_init(IcsEvent* ev) {
    if (!ev) return;
    xmemzero(ev, sizeof(IcsEvent));
    wstr_init(&ev->uid);
    wstr_init(&ev->summary);
    wstr_init(&ev->description);
    wstr_init(&ev->location);
    wstr_init(&ev->url);
    wstr_init(&ev->categories);
    wstr_init(&ev->organizer);
    wstr_init(&ev->organizerName);
    wstr_init(&ev->tzid);
    for (int i = 0; i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_init(&ev->attendees[i].email);
        wstr_init(&ev->attendees[i].name);
    }
    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) wstr_init(&ev->alarms[i].description);
    ev->timeMode = ICS_TIME_FLOAT;
    ev->freq = ICS_FREQ_NONE;
}

void ics_event_free(IcsEvent* ev) {
    if (!ev) return;
    wstr_free(&ev->uid);
    wstr_free(&ev->summary);
    wstr_free(&ev->description);
    wstr_free(&ev->location);
    wstr_free(&ev->url);
    wstr_free(&ev->categories);
    wstr_free(&ev->organizer);
    wstr_free(&ev->organizerName);
    wstr_free(&ev->tzid);
    for (int i = 0; i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_free(&ev->attendees[i].email);
        wstr_free(&ev->attendees[i].name);
    }
    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) wstr_free(&ev->alarms[i].description);
}

void ics_event_copy(IcsEvent* dst, const IcsEvent* src) {
    if (!dst || !src || dst == src) return;
    ics_event_free(dst);
    ics_event_init(dst);
    wstr_set(&dst->uid, wstr_c(&src->uid));
    wstr_set(&dst->summary, wstr_c(&src->summary));
    wstr_set(&dst->description, wstr_c(&src->description));
    wstr_set(&dst->location, wstr_c(&src->location));
    wstr_set(&dst->url, wstr_c(&src->url));
    wstr_set(&dst->categories, wstr_c(&src->categories));
    wstr_set(&dst->organizer, wstr_c(&src->organizer));
    wstr_set(&dst->organizerName, wstr_c(&src->organizerName));
    wstr_set(&dst->tzid, wstr_c(&src->tzid));
    dst->attendeeCount = src->attendeeCount;
    for (int i = 0; i < src->attendeeCount && i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_set(&dst->attendees[i].email, wstr_c(&src->attendees[i].email));
        wstr_set(&dst->attendees[i].name, wstr_c(&src->attendees[i].name));
        dst->attendees[i].rsvp = src->attendees[i].rsvp;
    }
    dst->start = src->start;
    dst->end = src->end;
    dst->allDay = src->allDay;
    dst->timeMode = src->timeMode;
    dst->freq = src->freq;
    dst->interval = src->interval;
    dst->count = src->count;
    dst->until = src->until;
    dst->hasUntil = src->hasUntil;
    dst->bydayMask = src->bydayMask;
    dst->bymonthday = src->bymonthday;
    dst->exdateCount = src->exdateCount;
    for (int i = 0; i < src->exdateCount && i < ICSG_MAX_DATES; ++i) dst->exdates[i] = src->exdates[i];
    dst->rdateCount = src->rdateCount;
    for (int i = 0; i < src->rdateCount && i < ICSG_MAX_DATES; ++i) dst->rdates[i] = src->rdates[i];
    dst->alarmCount = src->alarmCount;
    for (int i = 0; i < src->alarmCount && i < ICSG_MAX_ALARMS; ++i) {
        dst->alarms[i].enabled = src->alarms[i].enabled;
        dst->alarms[i].minutesBefore = src->alarms[i].minutesBefore;
        wstr_set(&dst->alarms[i].description, wstr_c(&src->alarms[i].description));
    }
    dst->status = src->status;
    dst->transp = src->transp;
    dst->klass = src->klass;
    dst->priority = src->priority;
    dst->sequence = src->sequence;
    dst->created = src->created;
    dst->lastModified = src->lastModified;
    dst->dtstamp = src->dtstamp;
}

void ics_calendar_init(IcsCalendar* cal) {
    if (!cal) return;
    xmemzero(cal, sizeof(IcsCalendar));
    wstr_init(&cal->prodid);
    wstr_init(&cal->version);
    wstr_init(&cal->calscale);
    wstr_init(&cal->method);
    wstr_init(&cal->name);
    wstr_init(&cal->timezone);
    wstr_set(&cal->prodid, L"-//ics-generate//ICS_Generate 1.0//EN");
    wstr_set(&cal->version, L"2.0");
    wstr_set(&cal->calscale, L"GREGORIAN");
}

void ics_calendar_free(IcsCalendar* cal) {
    if (!cal) return;
    wstr_free(&cal->prodid);
    wstr_free(&cal->version);
    wstr_free(&cal->calscale);
    wstr_free(&cal->method);
    wstr_free(&cal->name);
    wstr_free(&cal->timezone);
    if (cal->events) {
        for (int i = 0; i < cal->count; ++i) ics_event_free(&cal->events[i]);
        xfree(cal->events);
    }
    cal->events = NULL;
    cal->count = 0;
    cal->cap = 0;
}

void ics_calendar_clear(IcsCalendar* cal) {
    if (!cal) return;
    ics_calendar_free(cal);
    ics_calendar_init(cal);
}

static IcsEvent* ics_cal_slot(IcsCalendar* cal) {
    if (!cal) return NULL;
    if (cal->count >= ICSG_MAX_EVENTS) return NULL;
    if (cal->count >= cal->cap) {
        int newCap = cal->cap ? cal->cap * 2 : 8;
        if (newCap > ICSG_MAX_EVENTS) newCap = ICSG_MAX_EVENTS;
        IcsEvent* grown = (IcsEvent*)xrealloc(cal->events, sizeof(IcsEvent) * (size_t)newCap);
        cal->events = grown;
        cal->cap = newCap;
    }
    IcsEvent* ev = &cal->events[cal->count];
    ics_event_init(ev);
    ++cal->count;
    return ev;
}

IcsEvent* ics_calendar_add(IcsCalendar* cal) {
    IcsEvent* ev = ics_cal_slot(cal);
    if (!ev) return NULL;
    ics_event_make_uid(&ev->uid);
    dt_now_utc(&ev->dtstamp);
    ev->created = ev->dtstamp;
    dt_now_local(&ev->start);
    if (ev->start.minute != 0 || ev->start.second != 0) {
        dt_add_minutes(&ev->start, 60 - (ev->start.minute * 60 + ev->start.second) / 60 + 1);
        ev->start.minute = 0;
        ev->start.second = 0;
    }
    ev->end = ev->start;
    dt_add_minutes(&ev->end, 60);
    return ev;
}

IcsEvent* ics_calendar_duplicate(IcsCalendar* cal, const IcsEvent* src, bool newUid) {
    if (!cal || !src) return NULL;
    IcsEvent* ev = ics_cal_slot(cal);
    if (!ev) return NULL;
    ics_event_copy(ev, src);
    if (newUid) ics_event_make_uid(&ev->uid);
    return ev;
}

bool ics_calendar_remove(IcsCalendar* cal, int index) {
    if (!cal || index < 0 || index >= cal->count || !cal->events) return false;
    ics_event_free(&cal->events[index]);
    xmemmove(cal->events + index, cal->events + index + 1,
             sizeof(IcsEvent) * (size_t)(cal->count - index - 1));
    --cal->count;
    return true;
}

bool ics_calendar_move(IcsCalendar* cal, int index, int delta) {
    if (!cal || !cal->events) return false;
    int target = index + delta;
    if (index < 0 || index >= cal->count) return false;
    if (target < 0 || target >= cal->count) return false;
    IcsEvent tmp = cal->events[index];
    cal->events[index] = cal->events[target];
    cal->events[target] = tmp;
    return true;
}

// ------------------------------------------------------- shared properties --

// decimal helper
static void ics_put_num(wchar_t* out, int* n, int value) {
    wchar_t num[12];
    int d = 0;
    if (value == 0) num[d++] = L'0';
    int v = value;
    while (v > 0) { num[d++] = (wchar_t)(L'0' + v % 10); v /= 10; }
    while (d > 0) out[(*n)++] = num[--d];
}

static int ics_num_len(int value) {
    int d = 0;
    if (value == 0) return 1;
    int v = value;
    while (v > 0) { ++d; v /= 10; }
    return d;
}

// "-PT15M" | "-P1D" | "-P1W" | "PT0M"; positive minutes give "PT<n>M".
int ics_trigger_from_minutes(int minutes, wchar_t* out, int outCap) {
    if (!out || outCap < 8) return -1;
    bool before = minutes < 0;
    int mag = before ? -minutes : minutes;
    int need = 4 + ics_num_len(mag) + (before ? 1 : 0);
    if (outCap < need + 1) return -1;
    int n = 0;
    if (before) out[n++] = L'-';
    out[n++] = L'P';
    if (mag > 0 && mag % 10080 == 0) {
        ics_put_num(out, &n, mag / 10080);
        out[n++] = L'W';
    } else if (mag > 0 && mag % 1440 == 0) {
        ics_put_num(out, &n, mag / 1440);
        out[n++] = L'D';
    } else {
        out[n++] = L'T';
        ics_put_num(out, &n, mag);
        out[n++] = L'M';
    }
    out[n] = 0;
    return n;
}

// ------------------------------------------------------------- RRULE text ---

static const wchar_t* const icsg_dow_names[7] = {
    L"SU", L"MO", L"TU", L"WE", L"TH", L"FR", L"SA"
};

void ics_rrule_text(const IcsEvent* ev, WStr* out) {
    wstr_clear(out);
    if (!ev || ev->freq == ICS_FREQ_NONE) return;
    wstr_set(out, L"RRULE:FREQ=");
    wstr_append(out, ics_freq_name(ev->freq));
    int interval = ev->interval > 0 ? ev->interval : 1;
    if (interval != 1) {
        wstr_append(out, L";INTERVAL=");
        wstr_append_num(out, interval, 1);
    }
    if (ev->count > 0) {
        wstr_append(out, L";COUNT=");
        wstr_append_num(out, ev->count, 1);
    }
    if (ev->hasUntil && !dt_is_zero(&ev->until)) {
        wchar_t stamp[24];
        if (dt_to_ics(&ev->until, stamp, ICSG_ARRAY_COUNT(stamp)) > 0) {
            wstr_append(out, L";UNTIL=");
            wstr_append(out, stamp);
        }
    }
    if (ev->bydayMask != 0) {
        wstr_append(out, L";BYDAY=");
        bool first = true;
        for (int d = 0; d < 7; ++d) {
            if (ev->bydayMask & (1 << d)) {
                if (!first) wstr_append_ch(out, L',');
                wstr_append(out, icsg_dow_names[d]);
                first = false;
            }
        }
    }
    if (ev->bymonthday != 0) {
        wstr_append(out, L";BYMONTHDAY=");
        wstr_append_num(out, ev->bymonthday, 1);
    }
}

// ------------------------------------------------------- display summaries --

void ics_rrule_display(const IcsEvent* ev, WStr* out) {
    wstr_clear(out);
    if (!ev || ev->freq == ICS_FREQ_NONE) return;
    int interval = ev->interval > 0 ? ev->interval : 1;
    switch (ev->freq) {
        case ICS_FREQ_DAILY:
            wstr_append(out, L"\u6BCF");                       // 每
            if (interval != 1) wstr_append_num(out, interval, 1);
            wstr_append(out, (interval == 1) ? L"\u65E5"       // 日
                                             : L"\u5929");     // 天
            break;
        case ICS_FREQ_WEEKLY:
            wstr_append(out, L"\u6BCF");                       // 每
            if (interval != 1) wstr_append_num(out, interval, 1);
            wstr_append(out, L"\u5468");                       // 周
            if (ev->bydayMask != 0) {
                static const wchar_t* const dayNames[7] = {
                    L"\u5468\u65E5",  // 周日
                    L"\u5468\u4E00",  // 周一
                    L"\u5468\u4E8C",  // 周二
                    L"\u5468\u4E09",  // 周三
                    L"\u5468\u56DB",  // 周四
                    L"\u5468\u4E94",  // 周五
                    L"\u5468\u516D"   // 周六
                };
                wstr_append_ch(out, L'(');
                bool first = true;
                for (int d = 1; d <= 7; ++d) {
                    int bit = d % 7;   // MO..SU display order
                    if (ev->bydayMask & (1 << bit)) {
                        if (!first) wstr_append_ch(out, L',');
                        wstr_append(out, dayNames[bit]);
                        first = false;
                    }
                }
                wstr_append_ch(out, L')');
            }
            break;
        case ICS_FREQ_MONTHLY:
            wstr_append(out, L"\u6BCF");                       // 每
            if (interval != 1) wstr_append_num(out, interval, 1);
            wstr_append(out, L"\u6708");                       // 月
            break;
        case ICS_FREQ_YEARLY:
            wstr_append(out, L"\u6BCF");                       // 每
            if (interval != 1) wstr_append_num(out, interval, 1);
            wstr_append(out, L"\u5E74");                       // 年
            break;
        default:
            break;
    }
    if (ev->count > 0) {
        wstr_append(out, L"\uFF0C\u5171");                     // ，共
        wstr_append_num(out, ev->count, 1);
        wstr_append(out, L"\u6B21");                           // 次
    } else if (ev->hasUntil && !dt_is_zero(&ev->until)) {
        wstr_append(out, L"\uFF0C\u76F4\u5230");               // ，直到
        wstr_append_num(out, ev->until.year, 4);
        wstr_append_ch(out, L'-');
        wstr_append_num(out, ev->until.month, 2);
        wstr_append_ch(out, L'-');
        wstr_append_num(out, ev->until.day, 2);
    }
}

void ics_alarm_display(const IcsEvent* ev, WStr* out) {
    wstr_clear(out);
    if (!ev) return;
    for (int i = 0; i < ev->alarmCount && i < ICSG_MAX_ALARMS; ++i) {
        if (ev->alarms[i].enabled) {
            wstr_append(out, L"\u63D0\u524D");                 // 提前
            wstr_append_num(out, ev->alarms[i].minutesBefore, 1);
            wstr_append(out, L"\u5206\u949F");                 // 分钟
            return;
        }
    }
}

// ------------------------------------------------------------ enum names ----

const wchar_t* ics_freq_name(IcsFreq freq) {
    switch (freq) {
        case ICS_FREQ_DAILY:   return L"DAILY";
        case ICS_FREQ_WEEKLY:  return L"WEEKLY";
        case ICS_FREQ_MONTHLY: return L"MONTHLY";
        case ICS_FREQ_YEARLY:  return L"YEARLY";
        default:               return L"";
    }
}

const wchar_t* ics_status_name(IcsStatus st) {
    switch (st) {
        case ICS_STATUS_TENTATIVE:  return L"TENTATIVE";
        case ICS_STATUS_CONFIRMED:  return L"CONFIRMED";
        case ICS_STATUS_CANCELLED:  return L"CANCELLED";
        default:                    return L"";
    }
}

const wchar_t* ics_transp_name(IcsTransp tp) {
    switch (tp) {
        case ICS_TRANSP_OPAQUE:      return L"OPAQUE";
        case ICS_TRANSP_TRANSPARENT: return L"TRANSPARENT";
        default:                     return L"";
    }
}

const wchar_t* ics_class_name(IcsClass k) {
    switch (k) {
        case ICS_CLASS_PUBLIC:       return L"PUBLIC";
        case ICS_CLASS_PRIVATE:      return L"PRIVATE";
        case ICS_CLASS_CONFIDENTIAL: return L"CONFIDENTIAL";
        default:                     return L"";
    }
}

static bool ics_name_eq(const wchar_t* text, const wchar_t* name) {
    return weq_ci(text, name);
}

IcsFreq ics_freq_parse(const wchar_t* text) {
    if (ics_name_eq(text, L"DAILY"))   return ICS_FREQ_DAILY;
    if (ics_name_eq(text, L"WEEKLY"))  return ICS_FREQ_WEEKLY;
    if (ics_name_eq(text, L"MONTHLY")) return ICS_FREQ_MONTHLY;
    if (ics_name_eq(text, L"YEARLY"))  return ICS_FREQ_YEARLY;
    return ICS_FREQ_NONE;
}

IcsStatus ics_status_parse(const wchar_t* text) {
    if (ics_name_eq(text, L"TENTATIVE"))  return ICS_STATUS_TENTATIVE;
    if (ics_name_eq(text, L"CONFIRMED"))  return ICS_STATUS_CONFIRMED;
    if (ics_name_eq(text, L"CANCELLED"))  return ICS_STATUS_CANCELLED;
    return ICS_STATUS_NONE;
}

IcsTransp ics_transp_parse(const wchar_t* text) {
    if (ics_name_eq(text, L"OPAQUE"))      return ICS_TRANSP_OPAQUE;
    if (ics_name_eq(text, L"TRANSPARENT")) return ICS_TRANSP_TRANSPARENT;
    return ICS_TRANSP_NONE;
}

IcsClass ics_class_parse(const wchar_t* text) {
    if (ics_name_eq(text, L"PUBLIC"))       return ICS_CLASS_PUBLIC;
    if (ics_name_eq(text, L"PRIVATE"))      return ICS_CLASS_PRIVATE;
    if (ics_name_eq(text, L"CONFIDENTIAL")) return ICS_CLASS_CONFIDENTIAL;
    return ICS_CLASS_NONE;
}

// ---------------------------------------------------------- serialisation ----

static void ics_write_dt_value_suffix(WStr* line, const DateTime* d,
                                      const IcsEvent* ev, bool saveAsUtc) {
    DateTime v = *d;
    if (ev->allDay) {
        v.dateOnly = true;
        v.utc = false;
        wstr_append(line, L";VALUE=DATE:");
    } else if (ev->timeMode == ICS_TIME_TZID && !saveAsUtc) {
        wstr_append(line, L";TZID=");
        wstr_append(line, wstr_c(&ev->tzid));
        wstr_append_ch(line, L':');
    } else if (ev->timeMode == ICS_TIME_UTC || saveAsUtc) {
        if (ev->timeMode == ICS_TIME_TZID && !dt_is_zero(&v)) {
            if (!ics_zone_local_to_utc(wstr_c(&ev->tzid), &v)) dt_local_to_utc(&v);
        } else if (ev->timeMode == ICS_TIME_FLOAT && saveAsUtc && !dt_is_zero(&v)) {
            dt_local_to_utc(&v);
        }
        v.utc = true;
        wstr_append_ch(line, L':');
    } else {
        wstr_append_ch(line, L':');
    }
    wchar_t stamp[24];
    if (dt_to_ics(&v, stamp, ICSG_ARRAY_COUNT(stamp)) > 0) wstr_append(line, stamp);
}

static void ics_write_dt(WStr* line, const wchar_t* name,
                         const DateTime* d, const IcsEvent* ev, bool saveAsUtc) {
    wstr_set(line, name);
    ics_write_dt_value_suffix(line, d, ev, saveAsUtc);
}

static void ics_write_person(WStr* line, const wchar_t* prop,
                             const WStr* email, const WStr* cn, bool rsvp) {
    wstr_set(line, prop);
    if (!wstr_is_empty(cn)) {
        wstr_append(line, L";CN=");
        ics_append_escaped(line, cn);
    }
    if (rsvp) wstr_append(line, L";RSVP=TRUE");
    wstr_append(line, L":mailto:");
    // strip an existing mailto: prefix
    const wchar_t* mail = wstr_c(email);
    if (weq_prefix_ci(mail, L"mailto:")) mail += 7;
    wstr_append(line, mail);
}

static void ics_write_alarm(IcsSink* s, WStr* line, const IcsAlarm* alarm, const IcsEvent* ev) {
    wstr_set(line, L"BEGIN:VALARM");
    sink_line(s, line);
    wstr_set(line, L"ACTION:DISPLAY");
    sink_line(s, line);
    wchar_t trig[24];
    if (ics_trigger_from_minutes(alarm->minutesBefore, trig, ICSG_ARRAY_COUNT(trig)) > 0) {
        wstr_set(line, L"TRIGGER:");
        wstr_append(line, trig);
        sink_line(s, line);
    }
    const WStr* desc = wstr_is_empty(&alarm->description) ? &ev->summary : &alarm->description;
    wstr_set(line, L"DESCRIPTION:");
    ics_append_escaped(line, desc);
    sink_line(s, line);
    wstr_set(line, L"END:VALARM");
    sink_line(s, line);
}

static int ics_write_event(IcsSink* s, WStr* line, const IcsEvent* ev, bool saveAsUtc) {
    if (!dt_is_valid(&ev->start) && !dt_is_zero(&ev->start)) return -1;

    wstr_set(line, L"BEGIN:VEVENT");
    sink_line(s, line);

    wstr_set(line, L"UID:");
    wstr_append(line, wstr_c(&ev->uid));
    sink_line(s, line);

    DateTime stamp = ev->dtstamp;
    if (dt_is_zero(&stamp)) dt_now_utc(&stamp);
    stamp.utc = true;
    stamp.dateOnly = false;
    wchar_t text[24];
    if (dt_to_ics(&stamp, text, ICSG_ARRAY_COUNT(text)) > 0) {
        wstr_set(line, L"DTSTAMP:");
        wstr_append(line, text);
        sink_line(s, line);
    }

    if (!dt_is_zero(&ev->created)) {
        DateTime c = ev->created;
        c.utc = true;
        if (dt_to_ics(&c, text, ICSG_ARRAY_COUNT(text)) > 0) {
            wstr_set(line, L"CREATED:");
            wstr_append(line, text);
            sink_line(s, line);
        }
    }
    if (!dt_is_zero(&ev->lastModified)) {
        DateTime m = ev->lastModified;
        m.utc = true;
        if (dt_to_ics(&m, text, ICSG_ARRAY_COUNT(text)) > 0) {
            wstr_set(line, L"LAST-MODIFIED:");
            wstr_append(line, text);
            sink_line(s, line);
        }
    }
    if (ev->sequence != 0) {
        wstr_set(line, L"SEQUENCE:");
        wstr_append_num(line, ev->sequence, 1);
        sink_line(s, line);
    }

    ics_write_dt(line, L"DTSTART", &ev->start, ev, saveAsUtc);
    sink_line(s, line);

    bool haveEnd = dt_compare(&ev->end, &ev->start) > 0;
    if (ev->allDay && !haveEnd) {
        DateTime e = ev->start;
        dt_add_days(&e, 1);
        ics_write_dt(line, L"DTEND", &e, ev, saveAsUtc);
        sink_line(s, line);
    } else if (haveEnd) {
        ics_write_dt(line, L"DTEND", &ev->end, ev, saveAsUtc);
        sink_line(s, line);
    }
    // timed events with end <= start simply omit DTEND.

    if (!wstr_is_empty(&ev->summary)) {
        wstr_set(line, L"SUMMARY:");
        ics_append_escaped(line, &ev->summary);
        sink_line(s, line);
    }
    if (!wstr_is_empty(&ev->description)) {
        wstr_set(line, L"DESCRIPTION:");
        ics_append_escaped(line, &ev->description);
        sink_line(s, line);
    }
    if (!wstr_is_empty(&ev->location)) {
        wstr_set(line, L"LOCATION:");
        ics_append_escaped(line, &ev->location);
        sink_line(s, line);
    }
    if (!wstr_is_empty(&ev->categories)) {
        wstr_set(line, L"CATEGORIES:");
        ics_append_escaped(line, &ev->categories);
        sink_line(s, line);
    }
    if (!wstr_is_empty(&ev->url)) {
        wstr_set(line, L"URL:");
        wstr_append(line, wstr_c(&ev->url));
        sink_line(s, line);
    }

    if (ev->status != ICS_STATUS_NONE) {
        wstr_set(line, L"STATUS:");
        wstr_append(line, ics_status_name(ev->status));
        sink_line(s, line);
    }
    if (ev->transp != ICS_TRANSP_NONE) {
        wstr_set(line, L"TRANSP:");
        wstr_append(line, ics_transp_name(ev->transp));
        sink_line(s, line);
    }
    if (ev->klass != ICS_CLASS_NONE) {
        wstr_set(line, L"CLASS:");
        wstr_append(line, ics_class_name(ev->klass));
        sink_line(s, line);
    }
    if (ev->priority > 0) {
        wstr_set(line, L"PRIORITY:");
        wstr_append_num(line, ev->priority, 1);
        sink_line(s, line);
    }

    if (!wstr_is_empty(&ev->organizer)) {
        ics_write_person(line, L"ORGANIZER", &ev->organizer, &ev->organizerName, false);
        sink_line(s, line);
    }
    for (int i = 0; i < ev->attendeeCount && i < ICSG_MAX_ATTENDEES; ++i) {
        if (wstr_is_empty(&ev->attendees[i].email)) continue;
        ics_write_person(line, L"ATTENDEE", &ev->attendees[i].email,
                         &ev->attendees[i].name, ev->attendees[i].rsvp);
        sink_line(s, line);
    }

    if (ev->freq != ICS_FREQ_NONE) {
        WStr rrule;
        wstr_init(&rrule);
        ics_rrule_text(ev, &rrule);
        if (!wstr_is_empty(&rrule)) sink_line(s, &rrule);
        wstr_free(&rrule);
    }

    for (int i = 0; i < ev->exdateCount && i < ICSG_MAX_DATES; ++i) {
        wstr_set(line, L"EXDATE");
        ics_write_dt_value_suffix(line, &ev->exdates[i], ev, saveAsUtc);
        sink_line(s, line);
    }
    for (int i = 0; i < ev->rdateCount && i < ICSG_MAX_DATES; ++i) {
        wstr_set(line, L"RDATE");
        ics_write_dt_value_suffix(line, &ev->rdates[i], ev, saveAsUtc);
        sink_line(s, line);
    }

    for (int i = 0; i < ev->alarmCount && i < ICSG_MAX_ALARMS; ++i) {
        if (ev->alarms[i].enabled) ics_write_alarm(s, line, &ev->alarms[i], ev);
    }

    wstr_set(line, L"END:VEVENT");
    sink_line(s, line);
    return 0;
}

// collects the distinct TZIDs (in first-use order) that need a VTIMEZONE
static int ics_collect_zones(const IcsCalendar* cal, bool saveAsUtc,
                             const wchar_t** zones, int maxZones) {
    (void)saveAsUtc;
    int n = 0;
    for (int i = 0; i < cal->count && cal->events; ++i) {
        const IcsEvent* ev = &cal->events[i];
        if (ev->allDay || ev->timeMode != ICS_TIME_TZID || saveAsUtc) continue;
        const wchar_t* tz = wstr_c(&ev->tzid);
        if (!tz || !*tz) continue;
        bool found = false;
        for (int j = 0; j < n; ++j) if (weq_ci(zones[j], tz)) { found = true; break; }
        if (!found && n < maxZones) zones[n++] = tz;
    }
    return n;
}

int ics_serialize(const IcsCalendar* cal, char* out, int cap) {
    if (!cal) return -1;
    if (cal->count > 0 && !cal->events) return -1;
    for (int i = 0; i < cal->count; ++i) {
        const DateTime* st = &cal->events[i].start;
        if (!dt_is_zero(st) && !dt_is_valid(st)) return -1;
    }

    IcsSink sink;
    sink.out = NULL;
    sink.cap = 0;
    sink.len = 0;
    sink.lineOct = 0;

    WStr line;
    wstr_init(&line);

    wstr_set(&line, L"BEGIN:VCALENDAR");
    sink_line(&sink, &line);
    wstr_set(&line, L"VERSION:");
    wstr_append(&line, wstr_c(&cal->version));
    sink_line(&sink, &line);
    wstr_set(&line, L"PRODID:");
    wstr_append(&line, wstr_c(&cal->prodid));
    sink_line(&sink, &line);
    if (!wstr_is_empty(&cal->calscale)) {
        wstr_set(&line, L"CALSCALE:");
        wstr_append(&line, wstr_c(&cal->calscale));
        sink_line(&sink, &line);
    }
    if (!wstr_is_empty(&cal->method)) {
        wstr_set(&line, L"METHOD:");
        wstr_append(&line, wstr_c(&cal->method));
        sink_line(&sink, &line);
    }
    if (!wstr_is_empty(&cal->name)) {
        wstr_set(&line, L"X-WR-CALNAME:");
        ics_append_escaped(&line, &cal->name);
        sink_line(&sink, &line);
    }
    if (!wstr_is_empty(&cal->timezone)) {
        wstr_set(&line, L"X-WR-TIMEZONE:");
        wstr_append(&line, wstr_c(&cal->timezone));
        sink_line(&sink, &line);
    }

    // one VTIMEZONE per distinct TZID (none when saving as UTC)
    if (!cal->saveAsUtc) {
        const wchar_t* zones[64];
        int zoneCount = ics_collect_zones(cal, cal->saveAsUtc, zones, ICSG_ARRAY_COUNT(zones));
        for (int i = 0; i < zoneCount; ++i) {
            const IcsEvent* first = NULL;
            for (int j = 0; j < cal->count; ++j) {
                const IcsEvent* ev = &cal->events[j];
                if (!ev->allDay && ev->timeMode == ICS_TIME_TZID &&
                    weq_ci(wstr_c(&ev->tzid), zones[i])) { first = ev; break; }
            }
            int year = first ? (int)first->start.year : 2024;
            WStr vt;
            wstr_init(&vt);
            if (ics_zone_vtimezone(zones[i], year, &vt)) {
                // vtimezone text is CRLF separated unfolded lines
                int vlen = wstr_is_empty(&vt) ? 0 : vt.len;
                sink.lineOct = 0;
                const wchar_t* p = wstr_c(&vt);
                int k = 0;
                while (k < vlen) {
                    if (p[k] == L'\r' && k + 1 < vlen && p[k + 1] == L'\n') {
                        sink_raw(&sink, "\r\n", 2);
                        sink.lineOct = 0;
                        k += 2;
                    } else {
                        sink_code_point(&sink, p, vlen, &k);
                    }
                }
            }
            wstr_free(&vt);
        }
    }

    for (int i = 0; i < cal->count; ++i) {
        if (ics_write_event(&sink, &line, &cal->events[i], cal->saveAsUtc) != 0) {
            wstr_free(&line);
            xfree(sink.out);
            return -1;
        }
    }

    wstr_set(&line, L"END:VCALENDAR");
    sink_line(&sink, &line);
    wstr_free(&line);

    if (out && cap >= sink.len + 1) {
        xmemcopy(out, sink.out, (size_t)sink.len);
        out[sink.len] = 0;
    }
    int required = sink.len;
    xfree(sink.out);
    return required;
}

bool ics_save_file(const IcsCalendar* cal, const wchar_t* path) {
    if (!cal || !path) return false;
    int need = ics_serialize(cal, NULL, 0);
    if (need < 0) return false;
    char* buf = (char*)xmalloc((size_t)need + 1);
    int got = ics_serialize(cal, buf, need);
    if (got != need) { xfree(buf); return false; }
    bool ok = file_write_all(path, buf, need);
    xfree(buf);
    return ok;
}

bool ics_save_event_file(const IcsEvent* ev, const wchar_t* path) {
    if (!ev || !path) return false;
    IcsCalendar cal;
    ics_calendar_init(&cal);
    IcsEvent* copy = ics_calendar_duplicate(&cal, ev, false);
    bool ok = false;
    if (copy) {
        // keep the event's own saveAsUtc behaviour (false: as-modelled)
        ok = ics_save_file(&cal, path);
    }
    ics_calendar_free(&cal);
    return ok;
}

int ics_merge(IcsCalendar* dst, IcsCalendar* src) {
    if (!dst || !src) return 0;
    int added = 0;
    for (int i = 0; i < src->count && src->events; ++i) {
        if (ics_calendar_duplicate(dst, &src->events[i], false)) ++added;
    }
    ics_calendar_clear(src);
    return added;
}
