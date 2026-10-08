// ics_parse.cpp - tolerant RFC 5545 reader.
// Contract: src/core/ics.h (frozen). Accepts CRLF/LF/CR, BOM, folded lines,
// quoted parameters, unknown properties/components. CRT-free.
#include "ics.h"

// ------------------------------------------------------- line model ---------

struct IcsParam {
    const wchar_t* name;
    int            nameLen;
    const wchar_t* val;
    int            valLen;
};

struct IcsLine {
    const wchar_t* name;
    int            nameLen;
    const wchar_t* value;
    int            valueLen;
    IcsParam       params[8];
    int            paramCount;
};

// Splits NAME[;PARAM=VALUE]...:VALUE respecting double-quoted parameter values.
static bool icspl_split(const wchar_t* s, int len, IcsLine* out) {
    xmemzero(out, sizeof(IcsLine));
    if (!s || len <= 0) return false;
    int i = 0;
    int nameStart = 0;
    while (i < len && s[i] != L':' && s[i] != L';') ++i;
    out->name = s + nameStart;
    out->nameLen = i - nameStart;
    if (out->nameLen <= 0) return false;
    bool valueDone = false;
    while (i < len && !valueDone) {
        if (s[i] == L':') {
            out->value = s + i + 1;
            out->valueLen = len - (i + 1);
            valueDone = true;
            break;
        }
        // ';' begins a parameter
        ++i;
        int pStart = i;
        while (i < len && s[i] != L'=' && s[i] != L';' && s[i] != L':') ++i;
        if (i < len && s[i] == L':') {   // malformed: no '='; treat as value
            out->value = s + pStart;
            out->valueLen = len - pStart;
            valueDone = true;
            break;
        }
        if (out->paramCount < ICSG_ARRAY_COUNT(out->params)) {
            IcsParam* p = &out->params[out->paramCount++];
            p->name = s + pStart;
            p->nameLen = i - pStart;
            p->val = L"";
            p->valLen = 0;
            if (i < len && s[i] == L'=') {
                ++i;
                if (i < len && s[i] == L'"') {
                    ++i;
                    p->val = s + i;
                    while (i < len && s[i] != L'"') ++i;
                    p->valLen = i - (int)((p->val - s));
                    if (i < len) ++i;   // closing quote
                } else {
                    p->val = s + i;
                    while (i < len && s[i] != L';' && s[i] != L':') ++i;
                    p->valLen = i - (int)((p->val - s));
                }
            }
        } else {
            // parameter table full: skip to value start
            while (i < len && s[i] != L':') {
                if (s[i] == L'"') { ++i; while (i < len && s[i] != L'"') ++i; }
                ++i;
            }
        }
    }
    return true;
}

static bool icspl_param(const IcsLine* l, const wchar_t* name, const wchar_t** val, int* valLen) {
    for (int i = 0; i < l->paramCount; ++i) {
        if (l->params[i].nameLen > 0 &&
            wstr_starts_ci(l->params[i].name, l->params[i].nameLen, name) &&
            wlen(name) == l->params[i].nameLen) {
            *val = l->params[i].val;
            *valLen = l->params[i].valLen;
            return true;
        }
    }
    return false;
}

static bool icspl_value_param(const IcsLine* l, const wchar_t* want) {
    const wchar_t* v = NULL;
    int vl = 0;
    if (!icspl_param(l, L"VALUE", &v, &vl)) return false;
    return vl == wlen(want) && wstr_starts_ci(v, vl, want);
}

static int icspl_atoi(const wchar_t* s, int len) {
    if (!s || len <= 0) return 0;
    int i = 0;
    bool neg = false;
    while (i < len && (s[i] == L' ' || s[i] == L'\t')) ++i;
    if (i < len && (s[i] == L'-' || s[i] == L'+')) { neg = (s[i] == L'-'); ++i; }
    int v = 0;
    while (i < len && s[i] >= L'0' && s[i] <= L'9') {
        v = v * 10 + (int)(s[i] - L'0');
        ++i;
    }
    return neg ? -v : v;
}

// unescapes a value/param into dst
static void icspl_set_escaped(WStr* dst, const wchar_t* s, int len) {
    int need = ics_unescape_text(s, len, NULL, 0);
    if (!wstr_reserve(dst, need + 1)) return;
    int n = ics_unescape_text(s, len, dst->data, need);
    if (n < 0) n = 0;
    dst->len = n;
    dst->data[n] = 0;
}

static bool icspl_name_is(const IcsLine* l, const wchar_t* name) {
    return l->nameLen == wlen(name) && wstr_starts_ci(l->name, l->nameLen, name);
}

// ------------------------------------------------------- event property map --

static void icspl_add_zone_date(DateTime* arr, int* count, const wchar_t* s, int len) {
    if (*count >= ICSG_MAX_DATES) return;
    DateTime d;
    dt_zero(&d);
    if (!dt_from_ics(s, len, &d)) return;
    arr[*count] = d;
    ++(*count);
}

static void icspl_datetime(IcsEvent* ev, const IcsLine* l, DateTime* field, bool isStart) {
    DateTime d;
    dt_zero(&d);
    if (!dt_from_ics(l->value, l->valueLen, &d)) return;
    const wchar_t* tz = NULL;
    int tzLen = 0;
    bool hasTz = icspl_param(l, L"TZID", &tz, &tzLen);
    if (isStart) {
        ev->allDay = d.dateOnly || icspl_value_param(l, L"DATE");
        if (hasTz && !ev->allDay) {
            ev->timeMode = ICS_TIME_TZID;
            icspl_set_escaped(&ev->tzid, tz, tzLen);
        } else if (d.utc && !ev->allDay) {
            ev->timeMode = ICS_TIME_UTC;
        } else if (!ev->allDay) {
            ev->timeMode = ICS_TIME_FLOAT;
        }
    }
    *field = d;
}

static void icspl_trigger(IcsEvent* ev, const wchar_t* s, int len) {
    if (ev->alarmCount <= 0 || ev->alarmCount > ICSG_MAX_ALARMS) return;
    IcsAlarm* alarm = &ev->alarms[ev->alarmCount - 1];
    int i = 0;
    int sign = -1;                       // alarms default to before the start
    if (i < len && (s[i] == L'-' || s[i] == L'+')) { sign = (s[i] == L'-') ? -1 : 1; ++i; }
    if (i >= len || (s[i] != L'P' && s[i] != L'p')) {
        // absolute date-time trigger: store as-is best effort (0 minutes)
        alarm->minutesBefore = 0;
        return;
    }
    ++i;
    int weeks = 0, days = 0, hours = 0, minutes = 0;
    while (i < len) {
        wchar_t c = s[i];
        if (c == L'T' || c == L't') { ++i; continue; }
        if (c < L'0' || c > L'9') break;
        int v = 0;
        while (i < len && s[i] >= L'0' && s[i] <= L'9') { v = v * 10 + (int)(s[i] - L'0'); ++i; }
        if (i >= len) break;
        wchar_t unit = s[i];
        if (unit == L'W' || unit == L'w') weeks = v;
        else if (unit == L'D' || unit == L'd') days = v;
        else if (unit == L'H' || unit == L'h') hours = v;
        else if (unit == L'M' || unit == L'm') minutes = v;
        else if (unit == L'S' || unit == L's') { /* seconds ignored */ }
        ++i;
    }
    alarm->minutesBefore = sign * (weeks * 10080 + days * 1440 + hours * 60 + minutes);
}

static void icspl_byday(IcsEvent* ev, const wchar_t* s, int len) {
    int i = 0;
    while (i < len) {
        // find token end
        int start = i;
        while (i < len && s[i] != L',') ++i;
        int tEnd = i;
        if (tEnd - start >= 2) {
            const wchar_t* p = s + tEnd - 2;
            int bit = -1;
            if (p[0] == L'S' || p[0] == L's') {
                if (p[1] == L'U' || p[1] == L'u') bit = 0;
                else if (p[1] == L'A' || p[1] == L'a') bit = 6;
            } else if (p[0] == L'M' || p[0] == L'm') {
                if (p[1] == L'O' || p[1] == L'o') bit = 1;
            } else if (p[0] == L'T' || p[0] == L't') {
                if (p[1] == L'U' || p[1] == L'u') bit = 2;
                else if (p[1] == L'H' || p[1] == L'h') bit = 4;
            } else if (p[0] == L'W' || p[0] == L'w') {
                if (p[1] == L'E' || p[1] == L'e') bit = 3;
            } else if (p[0] == L'F' || p[0] == L'f') {
                if (p[1] == L'R' || p[1] == L'r') bit = 5;
            }
            if (bit >= 0) ev->bydayMask |= (1 << bit);
        }
        ++i;   // skip comma
    }
}

static void icspl_rrule(IcsEvent* ev, const wchar_t* value, int len) {
    // value may carry a "RRULE:" prefix defensively
    const wchar_t* s = value;
    int sl = len;
    if (sl > 6 && wstr_starts_ci(s, sl, L"RRULE:")) { s += 6; sl -= 6; }
    int i = 0;
    while (i < sl) {
        int start = i;
        while (i < sl && s[i] != L';') ++i;
        int partEnd = i;
        int eq = start;
        while (eq < partEnd && s[eq] != L'=') ++eq;
        const wchar_t* key = s + start;
        int keyLen = eq - start;
        const wchar_t* val = s + ((eq < partEnd) ? eq + 1 : partEnd);
        int valLen = partEnd - ((eq < partEnd) ? eq + 1 : partEnd);
        if (keyLen == 4 && wstr_starts_ci(key, keyLen, L"FREQ")) {
            wchar_t buf[16];
            int n = valLen < 15 ? valLen : 15;
            for (int k = 0; k < n; ++k) buf[k] = val[k];
            buf[n] = 0;
            ev->freq = ics_freq_parse(buf);
            if (ev->interval < 1) ev->interval = 1;
        } else if (keyLen == 8 && wstr_starts_ci(key, keyLen, L"INTERVAL")) {
            ev->interval = icspl_atoi(val, valLen);
        } else if (keyLen == 5 && wstr_starts_ci(key, keyLen, L"COUNT")) {
            ev->count = icspl_atoi(val, valLen);
        } else if (keyLen == 5 && wstr_starts_ci(key, keyLen, L"UNTIL")) {
            DateTime d;
            dt_zero(&d);
            if (dt_from_ics(val, valLen, &d)) { ev->until = d; ev->hasUntil = true; }
        } else if (keyLen == 5 && wstr_starts_ci(key, keyLen, L"BYDAY")) {
            icspl_byday(ev, val, valLen);
        } else if (keyLen == 10 && wstr_starts_ci(key, keyLen, L"BYMONTHDAY")) {
            ev->bymonthday = icspl_atoi(val, valLen);
        }
        ++i;
    }
}

static void icspl_prop_event(IcsEvent* ev, const IcsLine* l) {
    if (icspl_name_is(l, L"UID")) {
        icspl_set_escaped(&ev->uid, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"SUMMARY")) {
        icspl_set_escaped(&ev->summary, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"DESCRIPTION")) {
        icspl_set_escaped(&ev->description, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"LOCATION")) {
        icspl_set_escaped(&ev->location, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"CATEGORIES")) {
        icspl_set_escaped(&ev->categories, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"URL")) {
        icspl_set_escaped(&ev->url, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"DTSTART")) {
        icspl_datetime(ev, l, &ev->start, true);
    } else if (icspl_name_is(l, L"DTEND")) {
        icspl_datetime(ev, l, &ev->end, false);
    } else if (icspl_name_is(l, L"STATUS")) {
        wchar_t buf[16];
        int n = l->valueLen < 15 ? l->valueLen : 15;
        for (int k = 0; k < n; ++k) buf[k] = l->value[k];
        buf[n] = 0;
        ev->status = ics_status_parse(buf);
    } else if (icspl_name_is(l, L"TRANSP")) {
        wchar_t buf[16];
        int n = l->valueLen < 15 ? l->valueLen : 15;
        for (int k = 0; k < n; ++k) buf[k] = l->value[k];
        buf[n] = 0;
        ev->transp = ics_transp_parse(buf);
    } else if (icspl_name_is(l, L"CLASS")) {
        wchar_t buf[16];
        int n = l->valueLen < 15 ? l->valueLen : 15;
        for (int k = 0; k < n; ++k) buf[k] = l->value[k];
        buf[n] = 0;
        ev->klass = ics_class_parse(buf);
    } else if (icspl_name_is(l, L"PRIORITY")) {
        ev->priority = icspl_atoi(l->value, l->valueLen);
    } else if (icspl_name_is(l, L"SEQUENCE")) {
        ev->sequence = icspl_atoi(l->value, l->valueLen);
    } else if (icspl_name_is(l, L"DTSTAMP")) {
        DateTime d;
        dt_zero(&d);
        if (dt_from_ics(l->value, l->valueLen, &d)) ev->dtstamp = d;
    } else if (icspl_name_is(l, L"CREATED")) {
        DateTime d;
        dt_zero(&d);
        if (dt_from_ics(l->value, l->valueLen, &d)) ev->created = d;
    } else if (icspl_name_is(l, L"LAST-MODIFIED")) {
        DateTime d;
        dt_zero(&d);
        if (dt_from_ics(l->value, l->valueLen, &d)) ev->lastModified = d;
    } else if (icspl_name_is(l, L"RRULE")) {
        icspl_rrule(ev, l->value, l->valueLen);
    } else if (icspl_name_is(l, L"EXDATE")) {
        // comma separated multi value, escaped commas already unescaped below
        int i = 0;
        while (i < l->valueLen) {
            int start = i;
            while (i < l->valueLen && l->value[i] != L',') ++i;
            icspl_add_zone_date(ev->exdates, &ev->exdateCount, l->value + start, i - start);
            ++i;
        }
    } else if (icspl_name_is(l, L"RDATE")) {
        int i = 0;
        while (i < l->valueLen) {
            int start = i;
            while (i < l->valueLen && l->value[i] != L',') ++i;
            icspl_add_zone_date(ev->rdates, &ev->rdateCount, l->value + start, i - start);
            ++i;
        }
    } else if (icspl_name_is(l, L"ORGANIZER")) {
        const wchar_t* mail = l->value;
        int ml = l->valueLen;
        if (ml > 7 && wstr_starts_ci(mail, ml, L"mailto:")) { mail += 7; ml -= 7; }
        icspl_set_escaped(&ev->organizer, mail, ml);
        const wchar_t* cn = NULL;
        int cnLen = 0;
        if (icspl_param(l, L"CN", &cn, &cnLen)) icspl_set_escaped(&ev->organizerName, cn, cnLen);
    } else if (icspl_name_is(l, L"ATTENDEE")) {
        if (ev->attendeeCount < ICSG_MAX_ATTENDEES) {
            IcsAttendee* a = &ev->attendees[ev->attendeeCount];
            const wchar_t* mail = l->value;
            int ml = l->valueLen;
            if (ml > 7 && wstr_starts_ci(mail, ml, L"mailto:")) { mail += 7; ml -= 7; }
            icspl_set_escaped(&a->email, mail, ml);
            const wchar_t* cn = NULL;
            int cnLen = 0;
            if (icspl_param(l, L"CN", &cn, &cnLen)) icspl_set_escaped(&a->name, cn, cnLen);
            const wchar_t* rsvp = NULL;
            int rl = 0;
            a->rsvp = false;
            if (icspl_param(l, L"RSVP", &rsvp, &rl)) {
                a->rsvp = (rl == 4 && wstr_starts_ci(rsvp, rl, L"TRUE"));
            }
            ++ev->attendeeCount;
        }
    }
}

// EXDATE/RDATE values may contain escaped commas inside a date? No - RFC dates
// never contain commas, so splitting on raw commas is correct.

// ------------------------------------------------------------ main reader ---

static bool icspl_is_eol(wchar_t c) { return c == L'\r' || c == L'\n'; }

// Returns the length of the physical line at *pos and advances *pos past its
// terminator. Returns 0 at end of input (terminator of a trailing empty line
// is consumed).
static int icspl_next_line(const wchar_t* text, int len, int* pos, const wchar_t** lineOut) {
    if (*pos >= len) return 0;
    int start = *pos;
    int i = start;
    while (i < len && !icspl_is_eol(text[i])) ++i;
    *lineOut = text + start;
    int lineLen = i - start;
    if (i < len) {
        ++i;
        if (i < len && text[i] == L'\n' && text[i - 1] == L'\r') ++i;
    }
    *pos = i;
    return lineLen;
}

bool ics_parse(const char* utf8, int size, IcsCalendar* cal) {
    if (!cal) return false;
    ics_calendar_clear(cal);
    if (!utf8 || size <= 0) return false;

    // tolerant UTF-8 -> UTF-16 (invalid bytes become U+FFFD)
    int srcPos = 0;
    if (utf8_has_bom(utf8, size)) srcPos = 3;
    WStr text;
    wstr_init(&text);
    while (srcPos < size) {
        int before = srcPos;
        wchar_t cp = utf8_next(utf8, size, &srcPos);
        if (srcPos <= before) srcPos = before + 1;
        if (!wstr_reserve(&text, text.len + 2)) { wstr_free(&text); return false; }
        text.data[text.len++] = cp;
        text.data[text.len] = 0;
    }
    if (text.len == 0) { wstr_free(&text); return false; }

    enum Comp { C_NONE = 0, C_CAL, C_EVENT, C_ALARM, C_SKIP };
    Comp stack[8];
    for (int i = 0; i < 8; ++i) stack[i] = C_NONE;
    int depth = 0;

    IcsEvent cur;
    ics_event_init(&cur);
    bool haveEvent = false;
    bool sawAnything = false;

    WStr logical;
    wstr_init(&logical);

    int pos = 0;
    for (;;) {
        // build the next logical line (with unfolding)
        const wchar_t* phys = NULL;
        int physLen = icspl_next_line(text.data, text.len, &pos, &phys);
        if (physLen <= 0 && pos >= text.len) {
            if (logical.len == 0) break;
        }
        wstr_append_n(&logical, phys, physLen);
        // peek: continuation lines start with space or tab
        for (;;) {
            int save = pos;
            const wchar_t* nxt = NULL;
            int nxtLen = icspl_next_line(text.data, text.len, &pos, &nxt);
            if (nxtLen > 0 && (nxt[0] == L' ' || nxt[0] == L'\t')) {
                wstr_append_n(&logical, nxt + 1, nxtLen - 1);
            } else {
                pos = save;
                break;
            }
        }

        // process the logical line
        IcsLine line;
        if (icspl_split(wstr_c(&logical), logical.len, &line)) {
            sawAnything = true;
            if (icspl_name_is(&line, L"BEGIN")) {
                wchar_t name[24];
                int n = line.valueLen < 23 ? line.valueLen : 23;
                for (int k = 0; k < n; ++k) name[k] = line.value[k];
                name[n] = 0;
                Comp push = C_SKIP;
                if (depth < 7) {
                    if (weq_ci(name, L"VCALENDAR")) push = C_CAL;
                    else if (weq_ci(name, L"VEVENT") && (depth == 0 || stack[depth - 1] == C_CAL)) push = C_EVENT;
                    else if (weq_ci(name, L"VALARM")) push = C_ALARM;
                    if (push == C_EVENT) {
                        ics_event_free(&cur);
                        ics_event_init(&cur);
                        haveEvent = true;
                    } else if (push == C_ALARM && haveEvent) {
                        if (cur.alarmCount < ICSG_MAX_ALARMS) {
                            IcsAlarm* a = &cur.alarms[cur.alarmCount];
                            a->enabled = true;
                            a->minutesBefore = 15;
                            ++cur.alarmCount;
                        }
                    }
                    stack[depth++] = push;
                }
            } else if (icspl_name_is(&line, L"END")) {
                wchar_t name[24];
                int n = line.valueLen < 23 ? line.valueLen : 23;
                for (int k = 0; k < n; ++k) name[k] = line.value[k];
                name[n] = 0;
                bool isEvent = weq_ci(name, L"VEVENT");
                bool isAlarm = weq_ci(name, L"VALARM");
                bool isCal = weq_ci(name, L"VCALENDAR");
                if (depth > 0) {
                    Comp popped = stack[depth - 1];
                    bool matches = (popped == C_CAL && isCal) ||
                                   (popped == C_EVENT && isEvent) ||
                                   (popped == C_ALARM && isAlarm) ||
                                   popped == C_SKIP;
                    if (matches) {
                        --depth;
                        if (popped == C_EVENT && haveEvent) {
                            if (ics_calendar_duplicate(cal, &cur, false)) {
                                // appended
                            }
                            ics_event_free(&cur);
                            ics_event_init(&cur);
                            haveEvent = false;
                        }
                    }
                }
            } else if (depth > 0 && stack[depth - 1] == C_EVENT) {
                icspl_prop_event(&cur, &line);
            } else if (depth > 0 && stack[depth - 1] == C_ALARM) {
                if (haveEvent && cur.alarmCount > 0) {
                    IcsAlarm* a = &cur.alarms[cur.alarmCount - 1];
                    if (icspl_name_is(&line, L"TRIGGER")) {
                        icspl_trigger(&cur, line.value, line.valueLen);
                    } else if (icspl_name_is(&line, L"DESCRIPTION")) {
                        icspl_set_escaped(&a->description, line.value, line.valueLen);
                    }
                }
            } else if (depth == 0 || stack[depth - 1] == C_CAL) {
                // calendar level properties
                if (icspl_name_is(&line, L"PRODID")) {
                    icspl_set_escaped(&cal->prodid, line.value, line.valueLen);
                } else if (icspl_name_is(&line, L"VERSION")) {
                    icspl_set_escaped(&cal->version, line.value, line.valueLen);
                } else if (icspl_name_is(&line, L"CALSCALE")) {
                    icspl_set_escaped(&cal->calscale, line.value, line.valueLen);
                } else if (icspl_name_is(&line, L"METHOD")) {
                    icspl_set_escaped(&cal->method, line.value, line.valueLen);
                } else if (icspl_name_is(&line, L"X-WR-CALNAME")) {
                    icspl_set_escaped(&cal->name, line.value, line.valueLen);
                } else if (icspl_name_is(&line, L"X-WR-TIMEZONE")) {
                    icspl_set_escaped(&cal->timezone, line.value, line.valueLen);
                }
            }
        }

        wstr_clear(&logical);
        if (pos >= text.len) break;
    }

    // tolerate a missing END:VEVENT at EOF
    if (haveEvent) {
        ics_calendar_duplicate(cal, &cur, false);
        ics_event_free(&cur);
    }

    wstr_free(&logical);
    wstr_free(&text);
    return sawAnything;
}

bool ics_load_file(IcsCalendar* cal, const wchar_t* path) {
    if (!cal || !path) return false;
    char* data = NULL;
    int size = 0;
    if (!file_read_all(path, &data, &size)) return false;
    bool ok = ics_parse(data, size, cal);
    xfree(data);
    return ok;
}
