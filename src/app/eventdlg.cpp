// eventdlg.cpp - fully programmatic modal event editor (no DIALOG resource).
//
// Created per CONTRACT.md 2.3: every control is a child of a WS_POPUP frame,
// all Chinese strings are wide literals written as escape sequences so MSVC
// /utf-8 and MinGW compile them identically. One HFONT per dialog, freed on
// destroy; every scratch list is mirrored from POD global arrays.
#include "ui.h"

// ----------------------------------------------------------------- state --
static const wchar_t kDlgClass[] = L"IcsGenerateEventDlg";

static HWND g_dlg;
static IcsEvent* g_ev;
static bool     g_isNew;
static bool     g_ok;
static bool     g_running;
static HFONT    g_font;
static UINT     g_dpi;

// Time zone combo data, refreshed for every dialog instance.
static IcsZoneInfo g_zones[ICSG_MAX_ZONES];
static int         g_zoneCount;

// Attendee scratch pad, mirrored in the list box.
static WStr g_attMail[ICSG_MAX_ATTENDEES];
static WStr g_attName[ICSG_MAX_ATTENDEES];
static int  g_attCount;

// EXDATE / RDATE scratch pad, mirrored in the list box.
struct DateEntry { DateTime dt; bool isRdate; };
static DateEntry g_dates[ICSG_MAX_DATES * 2];
static int       g_dateCount;

// ------------------------------------------------------------- controls --
static HWND c_summary, c_location, c_desc, c_url, c_categories;
static HWND c_orgName, c_orgMail;
static HWND c_attList, c_attMail, c_attName, c_attAdd, c_attRemove;
static HWND c_dStart, c_tStart, c_dEnd, c_tEnd, c_allDay;
static HWND c_mode, c_tz, c_localTz, c_toUtc;
static HWND c_status, c_priority;
static HWND c_alarmChk[ICSG_MAX_ALARMS], c_alarmEd[ICSG_MAX_ALARMS],
            c_alarmSpn[ICSG_MAX_ALARMS];
static HWND c_freq, c_intervalEd, c_intervalSpn, c_endMode, c_countEd,
            c_countSpn, c_until, c_monthEd, c_monthSpn;
static HWND c_byday[7];
static HWND c_dateList, c_datePick, c_exAdd, c_rdAdd, c_dateRemove;
static HWND c_ok, c_cancel;

enum {
    LB_SUMMARY, LB_LOCATION, LB_DESC, LB_URL, LB_CATEGORIES,
    LB_ORG, LB_ORGMAIL, LB_STATUS, LB_PRIORITY, LB_START, LB_END,
    LB_MODE, LB_ZONE, LB_ATT, LB_ATTMAIL, LB_ATTNAME, LB_ALARM,
    LB_FREQ, LB_INTERVAL, LB_ENDMODE, LB_COUNT, LB_UNTIL, LB_MONTHDAY,
    LB_DATES,
    LB_COUNT_MAX
};
static HWND c_lbl[LB_COUNT_MAX];

// -------------------------------------------------------------- helpers --
static int dlg_scale(int value) {
    return MulDiv(value, (int)(g_dpi ? g_dpi : 96), 96);
}

static void place(HWND ctl, int x, int y, int w, int h) {
    SetWindowPos(ctl, NULL, dlg_scale(x), dlg_scale(y), dlg_scale(w),
                 dlg_scale(h), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void place_combo(HWND ctl, int x, int y, int w) {
    // combos need a tall window so the drop list stays visible
    SetWindowPos(ctl, NULL, dlg_scale(x), dlg_scale(y), dlg_scale(w),
                 dlg_scale(200), SWP_NOZORDER | SWP_NOACTIVATE);
}

static HWND mk_ctl(const wchar_t* cls, const wchar_t* text, DWORD style,
                   DWORD exStyle, int id) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | style,
                             0, 0, 10, 10, g_dlg, (HMENU)(INT_PTR)id,
                             g_app.inst, NULL);
    if (h && g_font) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static HWND mk_label(const wchar_t* text) {
    return mk_ctl(L"STATIC", text, WS_VISIBLE | SS_LEFT, 0, 0);
}
static HWND mk_edit(int id, bool multiline) {
    DWORD style = WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
    if (multiline) style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
    return mk_ctl(L"EDIT", L"", style, WS_EX_CLIENTEDGE, id);
}
static HWND mk_button(const wchar_t* text, int id, bool def) {
    return mk_ctl(L"BUTTON", text,
                  WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON |
                      (def ? BS_DEFPUSHBUTTON : 0),
                  0, id);
}
static HWND mk_check(const wchar_t* text, int id) {
    return mk_ctl(L"BUTTON", text, WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                  0, id);
}
static HWND mk_combo(int id, bool editable) {
    return mk_ctl(L"COMBOBOX", L"",
                  WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                      (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST),
                  0, id);
}
static HWND mk_dtp(int id, bool timePicker) {
    DWORD style = WS_VISIBLE | WS_TABSTOP |
        (timePicker ? (DWORD)DTS_TIMEFORMAT : (DWORD)DTS_SHORTDATECENTURYFORMAT);
    return mk_ctl(DATETIMEPICK_CLASSW, L"", style, 0, id);
}
static HWND mk_updown(HWND buddy, int id) {
    HWND h = mk_ctl(UPDOWN_CLASSW, L"", WS_VISIBLE | UDS_SETBUDDYINT |
                    UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 0, id);
    SendMessageW(h, UDM_SETBUDDY, (WPARAM)buddy, 0);
    return h;
}

static void get_ctl_wstr(HWND ctl, WStr* out) {
    int n = GetWindowTextLengthW(ctl);
    wchar_t* buf = (wchar_t*)xmalloc(((size_t)n + 1) * sizeof(wchar_t));
    if (n > 0) GetWindowTextW(ctl, buf, n + 1);
    else buf[0] = 0;
    wstr_set(out, buf);
    xfree(buf);
}

// Combines the date picker and the time picker of one timestamp.
static void read_timestamp(HWND dDate, HWND dTime, DateTime* out) {
    SYSTEMTIME sd;
    xmemzero(&sd, sizeof(sd));
    DateTime_GetSystemtime(dDate, &sd);
    dt_from_systemtime(out, &sd);
    if (dTime) {
        SYSTEMTIME st;
        xmemzero(&st, sizeof(st));
        DateTime_GetSystemtime(dTime, &st);
        out->hour   = st.wHour;
        out->minute = st.wMinute;
        out->second = st.wSecond;
    }
}

static void write_timestamp(HWND dDate, HWND dTime, const DateTime* d) {
    SYSTEMTIME sd;
    dt_to_systemtime(d, &sd);
    DateTime_SetSystemtime(dDate, GDT_VALID, &sd);
    if (dTime) DateTime_SetSystemtime(dTime, GDT_VALID, &sd);
}

// ------------------------------------------------------- combo contents --
static const wchar_t* const kModeItems[3] = {
    L"\u6d6e\u52a8\u5f53\u5730\u65f6\u95f4", L"UTC", L"\u6307\u5b9a\u65f6\u533a"
};
static const wchar_t* const kFreqItems[5] = {
    L"\u65e0", L"\u6bcf\u5929", L"\u6bcf\u5468", L"\u6bcf\u6708",
    L"\u6bcf\u5e74"
};
static const wchar_t* const kEndItems[3] = {
    L"\u4ece\u4e0d", L"\u6309\u6b21\u6570", L"\u76f4\u5230\u65e5\u671f"
};
static const wchar_t* const kBydayItems[7] = {   // SU..SA, single glyphs
    L"\u65e5", L"\u4e00", L"\u4e8c", L"\u4e09", L"\u56db", L"\u4e94",
    L"\u516d"
};
static const wchar_t* const kLabelText[LB_COUNT_MAX] = {
    L"\u6458\u8981", L"\u5730\u70b9", L"\u63cf\u8ff0", L"URL",
    L"\u5206\u7c7b",
    L"\u7ec4\u7ec7\u8005", L"\u90ae\u7bb1", L"\u72b6\u6001",
    L"\u4f18\u5148\u7ea7", L"\u5f00\u59cb", L"\u7ed3\u675f",
    L"\u65f6\u95f4\u6a21\u5f0f", L"\u65f6\u533a", L"\u53c2\u4e0e\u8005",
    L"\u90ae\u7bb1", L"\u59d3\u540d", L"\u63d0\u9192",
    L"\u9891\u7387", L"\u95f4\u9694", L"\u7ed3\u675f\u65b9\u5f0f",
    L"\u6b21\u6570", L"\u76f4\u5230", L"\u51e0\u53f7",
    L"\u4f8b\u5916/\u91cd\u590d\u65e5\u671f"
};

static void fill_combos(void) {
    for (int i = 0; i < 3; ++i)
        SendMessageW(c_mode, CB_ADDSTRING, 0, (LPARAM)kModeItems[i]);
    for (int i = 0; i < 5; ++i)
        SendMessageW(c_freq, CB_ADDSTRING, 0, (LPARAM)kFreqItems[i]);
    for (int i = 0; i < 3; ++i)
        SendMessageW(c_endMode, CB_ADDSTRING, 0, (LPARAM)kEndItems[i]);
    SendMessageW(c_status, CB_ADDSTRING, 0, (LPARAM)L"\u672a\u8bbe\u7f6e");
    SendMessageW(c_status, CB_ADDSTRING, 0, (LPARAM)L"\u6682\u5b9a");
    SendMessageW(c_status, CB_ADDSTRING, 0, (LPARAM)L"\u5df2\u786e\u8ba4");
    SendMessageW(c_status, CB_ADDSTRING, 0, (LPARAM)L"\u5df2\u53d6\u6d88");
    SendMessageW(c_priority, CB_ADDSTRING, 0, (LPARAM)L"\u672a\u8bbe\u7f6e");
    for (int i = 1; i <= 9; ++i) {
        wchar_t num[8];
        num[0] = 0;
        WStr t;
        wstr_init(&t);
        wstr_append_num(&t, i, 0);
        wcopy(num, ICSG_ARRAY_COUNT(num), wstr_c(&t));
        wstr_free(&t);
        SendMessageW(c_priority, CB_ADDSTRING, 0, (LPARAM)num);
    }
    // editable zone combo; typed text is preserved verbatim on OK
    for (int i = 0; i < g_zoneCount; ++i) {
        int at = (int)SendMessageW(c_tz, CB_ADDSTRING, 0,
                                   (LPARAM)wstr_c(&g_zones[i].display));
        SendMessageW(c_tz, CB_SETITEMDATA, at, (LPARAM)i);
    }
}

// --------------------------------------------------------- enable state --
static void update_mode_ui(void) {
    int mode = (int)SendMessageW(c_mode, CB_GETCURSEL, 0, 0);
    bool tzid = (mode == (int)ICS_TIME_TZID);
    bool utc  = (mode == (int)ICS_TIME_UTC);
    EnableWindow(c_tz, tzid ? TRUE : FALSE);
    EnableWindow(c_localTz, tzid ? TRUE : FALSE);
    EnableWindow(c_toUtc, utc ? FALSE : TRUE);
}

static void update_allday_ui(void) {
    bool allDay =
        (SendMessageW(c_allDay, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ShowWindow(c_tStart, allDay ? SW_HIDE : SW_SHOW);
    ShowWindow(c_tEnd, allDay ? SW_HIDE : SW_SHOW);
}

static void update_repeat_ui(void) {
    int freq = (int)SendMessageW(c_freq, CB_GETCURSEL, 0, 0);
    int endMode = (int)SendMessageW(c_endMode, CB_GETCURSEL, 0, 0);
    bool weekly  = (freq == (int)ICS_FREQ_WEEKLY);
    bool monthly = (freq == (int)ICS_FREQ_MONTHLY);
    for (int i = 0; i < 7; ++i)
        ShowWindow(c_byday[i], weekly ? SW_SHOW : SW_HIDE);
    ShowWindow(c_monthEd, monthly ? SW_SHOW : SW_HIDE);
    ShowWindow(c_monthSpn, monthly ? SW_SHOW : SW_HIDE);
    ShowWindow(c_lbl[LB_COUNT], endMode == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(c_countEd, endMode == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(c_countSpn, endMode == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(c_lbl[LB_UNTIL], endMode == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(c_until, endMode == 2 ? SW_SHOW : SW_HIDE);
}

// ------------------------------------------------------------- building --
static void build_controls(void) {
    for (int i = 0; i < LB_COUNT_MAX; ++i) c_lbl[i] = mk_label(kLabelText[i]);

    c_summary    = mk_edit(IDC_ED_SUMMARY, false);
    c_location   = mk_edit(IDC_ED_LOCATION, false);
    c_desc       = mk_edit(IDC_ED_DESC, true);
    c_url        = mk_edit(IDC_ED_URL, false);
    c_categories = mk_edit(IDC_ED_CATEGORIES, false);
    c_orgName    = mk_edit(IDC_ED_ORGNAME, false);
    c_orgMail    = mk_edit(IDC_ED_ORGMAIL, false);

    c_attList = mk_ctl(L"LISTBOX", L"",
                       WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                           LBS_HASSTRINGS | WS_VSCROLL,
                       WS_EX_CLIENTEDGE, IDC_LB_ATTENDEES);
    c_attMail   = mk_edit(IDC_ED_ATTMAIL, false);
    c_attName   = mk_edit(IDC_ED_ATTNAME, false);
    c_attAdd    = mk_button(L"\u6dfb\u52a0", IDC_BTN_ATTADD, false);
    c_attRemove = mk_button(L"\u5220\u9664", IDC_BTN_ATTREMOVE, false);

    c_dStart = mk_dtp(IDC_DTP_START, false);
    c_tStart = mk_dtp(IDC_DTP_STARTTIME, true);
    c_dEnd   = mk_dtp(IDC_DTP_END, false);
    c_tEnd   = mk_dtp(IDC_DTP_ENDTIME, true);
    c_allDay = mk_check(L"\u5168\u5929\u4e8b\u4ef6", IDC_CHK_ALLDAY);

    c_mode    = mk_combo(IDC_CB_TIMEMODE, false);
    c_tz      = mk_combo(IDC_CB_TZID, true);
    c_localTz = mk_button(L"\u672c\u673a\u65f6\u533a", IDC_BTN_LOCALTZ, false);
    c_toUtc   = mk_button(L"\u8f6c\u4e3aUTC", IDC_BTN_TOUTC, false);

    c_status   = mk_combo(IDC_CB_STATUS, false);
    c_priority = mk_combo(IDC_CB_PRIORITY, false);

    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) {
        wchar_t text[16];
        WStr t;
        wstr_init(&t);
        wstr_append(&t, L"\u63d0\u9192");
        wstr_append_num(&t, i + 1, 0);
        wcopy(text, ICSG_ARRAY_COUNT(text), wstr_c(&t));
        wstr_free(&t);
        c_alarmChk[i] = mk_check(text, IDC_CHK_ALARM1 + i);
        c_alarmEd[i]  = mk_edit(IDC_ED_ALARM1 + i, false);
        c_alarmSpn[i] = mk_updown(c_alarmEd[i], IDC_SPN_ALARM1 + i);
        SendMessageW(c_alarmSpn[i], UDM_SETRANGE32, 0, 525600);
    }

    c_freq        = mk_combo(IDC_CB_FREQ, false);
    c_intervalEd  = mk_edit(IDC_ED_INTERVAL, false);
    c_intervalSpn = mk_updown(c_intervalEd, IDC_SPN_INTERVAL);
    SendMessageW(c_intervalSpn, UDM_SETRANGE32, 1, 999);
    c_endMode  = mk_combo(IDC_CB_ENDMODE, false);
    c_countEd  = mk_edit(IDC_ED_COUNT, false);
    c_countSpn = mk_updown(c_countEd, IDC_SPN_COUNT);
    SendMessageW(c_countSpn, UDM_SETRANGE32, 1, 9999);
    c_until    = mk_dtp(IDC_DTP_UNTIL, false);
    c_monthEd  = mk_edit(IDC_ED_MONTHDAY, false);
    c_monthSpn = mk_updown(c_monthEd, IDC_SPN_MONTHDAY);
    SendMessageW(c_monthSpn, UDM_SETRANGE32, 0, 31);
    static const int kBydayIds[7] = { IDC_CHK_BYDAY_SU, IDC_CHK_BYDAY_MO,
        IDC_CHK_BYDAY_TU, IDC_CHK_BYDAY_WE, IDC_CHK_BYDAY_TH,
        IDC_CHK_BYDAY_FR, IDC_CHK_BYDAY_SA };
    for (int i = 0; i < 7; ++i)
        c_byday[i] = mk_check(kBydayItems[i], kBydayIds[i]);

    c_dateList   = mk_ctl(L"LISTBOX", L"",
                          WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                              LBS_HASSTRINGS | WS_VSCROLL,
                          WS_EX_CLIENTEDGE, IDC_LB_DATES);
    c_datePick   = mk_dtp(IDC_DTP_DATE, false);
    c_exAdd      = mk_button(L"\u6dfb\u52a0\u4f8b\u5916", IDC_BTN_EXADD, false);
    c_rdAdd      = mk_button(L"\u6dfb\u52a0\u91cd\u590d", IDC_BTN_RDADD, false);
    c_dateRemove = mk_button(L"\u5220\u9664\u6240\u9009", IDC_BTN_DATEREMOVE, false);

    c_ok     = mk_button(L"\u786e\u5b9a", IDOK, true);
    c_cancel = mk_button(L"\u53d6\u6d88", IDCANCEL, false);
    fill_combos();
}

// ----------------------------------------------------- scratch list ui --
static void refresh_attendee_list(void) {
    SendMessageW(c_attList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_attCount; ++i) {
        SendMessageW(c_attList, LB_ADDSTRING, 0,
                     (LPARAM)wstr_c(&g_attMail[i]));
    }
}

static void refresh_date_list(void) {
    SendMessageW(c_dateList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_dateCount; ++i) {
        wchar_t buf[64];
        WStr line;
        wstr_init(&line);
        wstr_append(&line, g_dates[i].isRdate ?
            L"\u91cd\u590d  " : L"\u4f8b\u5916  ");
        dt_to_display(&g_dates[i].dt, buf, ICSG_ARRAY_COUNT(buf));
        wstr_append(&line, buf);
        SendMessageW(c_dateList, LB_ADDSTRING, 0, (LPARAM)wstr_c(&line));
        wstr_free(&line);
    }
}

// ---------------------------------------------------------------- load --
static void dlg_load(void) {
    SetWindowTextW(c_summary,    wstr_c(&g_ev->summary));
    SetWindowTextW(c_location,   wstr_c(&g_ev->location));
    SetWindowTextW(c_desc,       wstr_c(&g_ev->description));
    SetWindowTextW(c_url,        wstr_c(&g_ev->url));
    SetWindowTextW(c_categories, wstr_c(&g_ev->categories));
    SetWindowTextW(c_orgName,    wstr_c(&g_ev->organizerName));
    SetWindowTextW(c_orgMail,    wstr_c(&g_ev->organizer));

    write_timestamp(c_dStart, c_tStart, &g_ev->start);
    DateTime endD = g_ev->end;
    if (dt_is_zero(&endD)) { endD = g_ev->start; dt_add_days(&endD, 1); }
    write_timestamp(c_dEnd, c_tEnd, &endD);
    SendMessageW(c_allDay, BM_SETCHECK,
                 g_ev->allDay ? BST_CHECKED : BST_UNCHECKED, 0);

    int mode = (int)g_ev->timeMode;
    if (mode < 0 || mode > 2) mode = 0;
    SendMessageW(c_mode, CB_SETCURSEL, mode, 0);
    SetWindowTextW(c_tz, wstr_c(&g_ev->tzid));

    SendMessageW(c_status, CB_SETCURSEL, (WPARAM)(int)g_ev->status, 0);
    int prio = g_ev->priority;
    if (prio < 0) prio = 0;
    if (prio > 9) prio = 9;
    SendMessageW(c_priority, CB_SETCURSEL, prio, 0);

    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) {
        bool on = i < g_ev->alarmCount && g_ev->alarms[i].enabled;
        int minutes = (i < g_ev->alarmCount) ? g_ev->alarms[i].minutesBefore : 15;
        if (minutes < 0) minutes = 0;
        if (minutes > 525600) minutes = 525600;
        SendMessageW(c_alarmChk[i], BM_SETCHECK,
                     on ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(c_alarmSpn[i], UDM_SETPOS32, 0, (LPARAM)minutes);
    }

    int freq = (int)g_ev->freq;
    if (freq < 0 || freq > 4) freq = 0;
    SendMessageW(c_freq, CB_SETCURSEL, freq, 0);
    SendMessageW(c_intervalSpn, UDM_SETPOS32, 0,
                 (LPARAM)(g_ev->interval >= 1 ? g_ev->interval : 1));
    int endMode = 0;
    if (g_ev->hasUntil) endMode = 2;
    else if (g_ev->count >= 1) endMode = 1;
    SendMessageW(c_endMode, CB_SETCURSEL, endMode, 0);
    SendMessageW(c_countSpn, UDM_SETPOS32, 0,
                 (LPARAM)(g_ev->count >= 1 ? g_ev->count : 1));
    DateTime until = g_ev->until;
    if (dt_is_zero(&until)) dt_now_local(&until);
    write_timestamp(c_until, NULL, &until);
    static const int kBydayBits[7] = { ICSG_BYDAY_SU, ICSG_BYDAY_MO,
        ICSG_BYDAY_TU, ICSG_BYDAY_WE, ICSG_BYDAY_TH, ICSG_BYDAY_FR,
        ICSG_BYDAY_SA };
    for (int i = 0; i < 7; ++i) {
        SendMessageW(c_byday[i], BM_SETCHECK,
                     (g_ev->bydayMask & kBydayBits[i]) ? BST_CHECKED :
                         BST_UNCHECKED,
                     0);
    }
    SendMessageW(c_monthSpn, UDM_SETPOS32, 0,
                 (LPARAM)(g_ev->bymonthday >= 0 ? g_ev->bymonthday : 0));

    g_attCount = 0;
    for (int i = 0; i < g_ev->attendeeCount && i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_set(&g_attMail[i], wstr_c(&g_ev->attendees[i].email));
        wstr_set(&g_attName[i], wstr_c(&g_ev->attendees[i].name));
        ++g_attCount;
    }
    refresh_attendee_list();

    g_dateCount = 0;
    for (int i = 0; i < g_ev->exdateCount && g_dateCount < ICSG_MAX_DATES * 2;
         ++i) {
        g_dates[g_dateCount].dt = g_ev->exdates[i];
        g_dates[g_dateCount].isRdate = false;
        ++g_dateCount;
    }
    for (int i = 0; i < g_ev->rdateCount && g_dateCount < ICSG_MAX_DATES * 2;
         ++i) {
        g_dates[g_dateCount].dt = g_ev->rdates[i];
        g_dates[g_dateCount].isRdate = true;
        ++g_dateCount;
    }
    refresh_date_list();

    update_allday_ui();
    update_mode_ui();
    update_repeat_ui();
}

// --------------------------------------------------------------- store --
// Copies every control back into a scratch event and validates. On success
// the scratch event replaces *g_ev (heap blocks move across, no deep copy).
static bool dlg_store(void) {
    IcsEvent tmp;
    ics_event_copy(&tmp, g_ev);   // keeps uid/dtstamp/sequence/created

    // ---- text fields
    get_ctl_wstr(c_summary,    &tmp.summary);
    get_ctl_wstr(c_location,   &tmp.location);
    get_ctl_wstr(c_desc,       &tmp.description);
    get_ctl_wstr(c_url,        &tmp.url);
    get_ctl_wstr(c_categories, &tmp.categories);
    get_ctl_wstr(c_orgName,    &tmp.organizerName);
    get_ctl_wstr(c_orgMail,    &tmp.organizer);

    // ---- mode and zone (typed text is kept verbatim)
    int mode = (int)SendMessageW(c_mode, CB_GETCURSEL, 0, 0);
    if (mode < 0 || mode > 2) mode = 0;
    tmp.timeMode = (IcsTimeMode)mode;
    get_ctl_wstr(c_tz, &tmp.tzid);
    if (tmp.timeMode == ICS_TIME_TZID && wstr_is_empty(&tmp.tzid)) {
        ics_event_free(&tmp);
        ui_error(g_dlg,
            L"\u6307\u5b9a\u65f6\u533a\u6a21\u5f0f\u4e0b\u5fc5\u987b\u586b\u5199\u65f6\u533a\u3002");
        return false;
    }
    if (tmp.timeMode != ICS_TIME_TZID) wstr_clear(&tmp.tzid);

    // ---- timestamps and validation
    bool allDay =
        (SendMessageW(c_allDay, BM_GETCHECK, 0, 0) == BST_CHECKED);
    tmp.allDay = allDay;
    read_timestamp(c_dStart, c_tStart, &tmp.start);
    read_timestamp(c_dEnd, c_tEnd, &tmp.end);
    if (allDay) {
        tmp.start.hour = tmp.start.minute = tmp.start.second = 0;
        tmp.end.hour   = tmp.end.minute   = tmp.end.second   = 0;
        tmp.start.dateOnly = tmp.end.dateOnly = true;
        tmp.start.utc = tmp.end.utc = false;
    } else {
        tmp.start.dateOnly = tmp.end.dateOnly = false;
        tmp.start.utc = tmp.end.utc = (tmp.timeMode == ICS_TIME_UTC);
    }
    if (dt_compare(&tmp.end, &tmp.start) <= 0) {
        ics_event_free(&tmp);
        ui_error(g_dlg, allDay ?
            L"\u5168\u5929\u4e8b\u4ef6\u7684\u7ed3\u675f\u65e5\u671f\u5fc5\u987b\u665a\u4e8e\u5f00\u59cb\u65e5\u671f\u3002" :
            L"\u7ed3\u675f\u65f6\u95f4\u5fc5\u987b\u665a\u4e8e\u5f00\u59cb\u65f6\u95f4\u3002");
        return false;
    }

    // ---- status and priority
    int status = (int)SendMessageW(c_status, CB_GETCURSEL, 0, 0);
    tmp.status = (status < 0) ? ICS_STATUS_NONE : (IcsStatus)status;
    int prio = (int)SendMessageW(c_priority, CB_GETCURSEL, 0, 0);
    tmp.priority = (prio < 0 || prio > 9) ? 0 : prio;

    // ---- alarms (minutes must be >= 0)
    tmp.alarmCount = ICSG_MAX_ALARMS;
    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) {
        bool on =
            (SendMessageW(c_alarmChk[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
        int minutes = (int)SendMessageW(c_alarmSpn[i], UDM_GETPOS32,
                                        (WPARAM)TRUE, 0);
        if (minutes < 0) {
            ics_event_free(&tmp);
            ui_error(g_dlg,
                L"\u63d0\u9192\u5206\u949f\u6570\u4e0d\u5f97\u4e3a\u8d1f\u3002");
            return false;
        }
        if (minutes > 525600) minutes = 525600;
        tmp.alarms[i].enabled = on;
        tmp.alarms[i].minutesBefore = minutes;
    }

    // ---- recurrence
    int freq = (int)SendMessageW(c_freq, CB_GETCURSEL, 0, 0);
    if (freq < 0 || freq > 4) freq = 0;
    tmp.freq = (IcsFreq)freq;
    tmp.interval = 0;
    tmp.count = 0;
    tmp.hasUntil = false;
    tmp.bydayMask = 0;
    tmp.bymonthday = 0;
    if (tmp.freq != ICS_FREQ_NONE) {
        int interval =
            (int)SendMessageW(c_intervalSpn, UDM_GETPOS32, (WPARAM)TRUE, 0);
        tmp.interval = (interval >= 1) ? interval : 1;
        int endMode = (int)SendMessageW(c_endMode, CB_GETCURSEL, 0, 0);
        if (endMode == 1) {
            int count =
                (int)SendMessageW(c_countSpn, UDM_GETPOS32, (WPARAM)TRUE, 0);
            tmp.count = (count >= 1) ? count : 1;
        } else if (endMode == 2) {
            read_timestamp(c_until, NULL, &tmp.until);
            tmp.until.utc = false;
            tmp.until.dateOnly = allDay;
            if (!allDay) {
                tmp.until.hour = tmp.until.minute = tmp.until.second = 0;
            }
            tmp.hasUntil = true;
        }
        if (tmp.freq == ICS_FREQ_WEEKLY) {
            static const int kBydayBits[7] = { ICSG_BYDAY_SU, ICSG_BYDAY_MO,
                ICSG_BYDAY_TU, ICSG_BYDAY_WE, ICSG_BYDAY_TH, ICSG_BYDAY_FR,
                ICSG_BYDAY_SA };
            for (int i = 0; i < 7; ++i) {
                if (SendMessageW(c_byday[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    tmp.bydayMask |= kBydayBits[i];
            }
        } else if (tmp.freq == ICS_FREQ_MONTHLY) {
            int md =
                (int)SendMessageW(c_monthSpn, UDM_GETPOS32, (WPARAM)TRUE, 0);
            tmp.bymonthday = (md >= 1 && md <= 31) ? md : 0;
        }
    }

    // ---- attendees
    for (int i = 0; i < tmp.attendeeCount; ++i) {
        wstr_free(&tmp.attendees[i].email);
        wstr_free(&tmp.attendees[i].name);
    }
    tmp.attendeeCount = 0;
    for (int i = 0; i < g_attCount && tmp.attendeeCount < ICSG_MAX_ATTENDEES;
         ++i) {
        wstr_set(&tmp.attendees[tmp.attendeeCount].email,
                 wstr_c(&g_attMail[i]));
        wstr_set(&tmp.attendees[tmp.attendeeCount].name,
                 wstr_c(&g_attName[i]));
        tmp.attendees[tmp.attendeeCount].rsvp = false;
        ++tmp.attendeeCount;
    }

    // ---- exdates and rdates
    tmp.exdateCount = 0;
    tmp.rdateCount = 0;
    for (int i = 0; i < g_dateCount; ++i) {
        DateTime d = g_dates[i].dt;
        d.dateOnly = allDay;
        d.utc = (tmp.timeMode == ICS_TIME_UTC);
        if (g_dates[i].isRdate) {
            if (tmp.rdateCount < ICSG_MAX_DATES)
                tmp.rdates[tmp.rdateCount++] = d;
        } else {
            if (tmp.exdateCount < ICSG_MAX_DATES)
                tmp.exdates[tmp.exdateCount++] = d;
        }
    }

    dt_now_utc(&tmp.lastModified);
    ics_event_free(g_ev);
    *g_ev = tmp;   // ownership of every heap block moves across
    return true;
}

// -------------------------------------------------------------- layout --
// Logical grid, ~412 units wide; every coordinate scales with the DPI.
static int dlg_layout(void) {
    const int lx = 10, lw = 58, fx = 72, fw = 326, bh = 21, row = 26;
    int y = 10;

    place(c_lbl[LB_SUMMARY], lx, y + 4, lw, 14);
    place(c_summary, fx, y, fw, bh);                     y += row;
    place(c_lbl[LB_LOCATION], lx, y + 4, lw, 14);
    place(c_location, fx, y, fw, bh);                    y += row;
    place(c_lbl[LB_DESC], lx, y + 4, lw, 14);
    place(c_desc, fx, y, fw, 56);                        y += 62;
    place(c_lbl[LB_URL], lx, y + 4, lw, 14);
    place(c_url, fx, y, fw, bh);                         y += row;
    place(c_lbl[LB_CATEGORIES], lx, y + 4, lw, 14);
    place(c_categories, fx, y, fw, bh);                  y += row;
    place(c_lbl[LB_ORG], lx, y + 4, lw, 14);
    place(c_orgName, fx, y, fw, bh);                     y += row;
    place(c_lbl[LB_ORGMAIL], lx, y + 4, lw, 14);
    place(c_orgMail, fx, y, fw, bh);                     y += row;

    place(c_lbl[LB_STATUS], lx, y + 4, lw, 14);
    place_combo(c_status, fx, y, 110);
    place(c_lbl[LB_PRIORITY], 200, y + 4, 52, 14);
    place_combo(c_priority, 258, y, 80);                 y += 28;

    place(c_lbl[LB_START], lx, y + 4, lw, 14);
    place(c_dStart, fx, y, 100, bh);
    place(c_tStart, 178, y, 80, bh);                     y += row;
    place(c_lbl[LB_END], lx, y + 4, lw, 14);
    place(c_dEnd, fx, y, 100, bh);
    place(c_tEnd, 178, y, 80, bh);                       y += row;
    place(c_allDay, fx, y, 130, bh);                     y += 24;
    place(c_lbl[LB_MODE], lx, y + 4, lw, 14);
    place_combo(c_mode, fx, y, 110);                     y += row;
    place(c_lbl[LB_ZONE], lx, y + 4, lw, 14);
    place_combo(c_tz, fx, y, 190);
    place(c_localTz, 270, y, 64, bh);                    y += row;
    place(c_toUtc, fx, y, 110, bh);                      y += 30;

    place(c_lbl[LB_ATT], lx, y + 4, lw, 14);
    place(c_attList, fx, y, 230, 60);
    place(c_attAdd, 310, y, 98, bh);
    place(c_attRemove, 310, y + 24, 98, bh);             y += 64;
    place(c_lbl[LB_ATTMAIL], lx, y + 4, lw, 14);
    place(c_attMail, fx, y, 230, bh);                    y += row;
    place(c_lbl[LB_ATTNAME], lx, y + 4, lw, 14);
    place(c_attName, fx, y, 230, bh);                    y += 28;

    place(c_lbl[LB_ALARM], lx, y + 4, lw, 14);
    for (int i = 0; i < ICSG_MAX_ALARMS; ++i) {
        place(c_alarmChk[i], fx, y, 64, bh);
        place(c_alarmEd[i], 140, y, 44, bh);
        place(c_alarmSpn[i], 140, y, 20, bh);            y += 24;
    }
    y += 4;

    place(c_lbl[LB_FREQ], lx, y + 4, lw, 14);
    place_combo(c_freq, fx, y, 110);
    place(c_lbl[LB_INTERVAL], 200, y + 4, 52, 14);
    place(c_intervalEd, 258, y, 50, bh);
    place(c_intervalSpn, 258, y, 18, bh);                y += row;
    place(c_lbl[LB_ENDMODE], lx, y + 4, lw, 14);
    place_combo(c_endMode, fx, y, 110);
    place(c_lbl[LB_COUNT], 200, y + 4, 52, 14);
    place(c_countEd, 258, y, 50, bh);
    place(c_countSpn, 258, y, 18, bh);
    place(c_lbl[LB_UNTIL], 200, y + 4, 52, 14);
    place(c_until, 258, y, 110, bh);                     y += row;
    for (int i = 0; i < 7; ++i) {
        place(c_byday[i], lx + i * 46, y, 44, bh);
    }
    y += 24;
    place(c_lbl[LB_MONTHDAY], lx, y + 4, lw, 14);
    place(c_monthEd, fx, y, 50, bh);
    place(c_monthSpn, fx, y, 18, bh);                    y += row;

    place(c_lbl[LB_DATES], lx, y + 4, lw, 14);
    place(c_dateList, fx, y, 226, 60);
    place(c_datePick, 306, y, 92, bh);
    place(c_exAdd, 306, y + 24, 92, bh);
    place(c_rdAdd, 306, y + 48, 92, bh);                 y += 64;
    place(c_dateRemove, fx, y, 92, bh);                  y += 30;

    place(c_ok, 200, y, 88, 24);
    place(c_cancel, 300, y, 88, 24);                     y += 34;
    return y + 8;
}

// ------------------------------------------------------------- actions --
static void attendee_add(void) {
    WStr mail, name;
    wstr_init(&mail);
    wstr_init(&name);
    get_ctl_wstr(c_attMail, &mail);
    get_ctl_wstr(c_attName, &name);
    if (wstr_is_empty(&mail)) {
        ui_error(g_dlg, L"\u8bf7\u8f93\u5165\u53c2\u4e0e\u8005\u90ae\u7bb1\u3002");
    } else if (g_attCount >= ICSG_MAX_ATTENDEES) {
        ui_error(g_dlg,
            L"\u53c2\u4e0e\u8005\u6570\u91cf\u5df2\u8fbe\u4e0a\u9650\uff088 \u4e2a\uff09\u3002");
    } else {
        wstr_set(&g_attMail[g_attCount], wstr_c(&mail));
        wstr_set(&g_attName[g_attCount], wstr_c(&name));
        ++g_attCount;
        refresh_attendee_list();
        SetWindowTextW(c_attMail, L"");
        SetWindowTextW(c_attName, L"");
    }
    wstr_free(&mail);
    wstr_free(&name);
}

static void attendee_remove(void) {
    int sel = (int)SendMessageW(c_attList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_attCount) return;
    SendMessageW(c_attList, LB_DELETESTRING, sel, 0);
    wstr_free(&g_attMail[sel]);
    wstr_free(&g_attName[sel]);
    for (int i = sel; i + 1 < g_attCount; ++i) {
        g_attMail[i] = g_attMail[i + 1];
        g_attName[i] = g_attName[i + 1];
    }
    xmemzero(&g_attMail[g_attCount - 1], sizeof(WStr));
    xmemzero(&g_attName[g_attCount - 1], sizeof(WStr));
    --g_attCount;
}

static void date_add(bool rdate) {
    if (g_dateCount >= ICSG_MAX_DATES * 2) {
        ui_error(g_dlg, L"\u65e5\u671f\u6570\u91cf\u5df2\u8fbe\u4e0a\u9650\u3002");
        return;
    }
    DateTime d;
    read_timestamp(c_datePick, NULL, &d);
    g_dates[g_dateCount].dt = d;
    g_dates[g_dateCount].isRdate = rdate;
    ++g_dateCount;
    refresh_date_list();
}

static void date_remove(void) {
    int sel = (int)SendMessageW(c_dateList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_dateCount) return;
    SendMessageW(c_dateList, LB_DELETESTRING, sel, 0);
    for (int i = sel; i + 1 < g_dateCount; ++i) g_dates[i] = g_dates[i + 1];
    --g_dateCount;
}

static void to_utc_clicked(void) {
    wchar_t zone[128];
    zone[0] = 0;
    GetWindowTextW(c_tz, zone, ICSG_ARRAY_COUNT(zone));
    if (!zone[0]) {
        ui_error(g_dlg,
            L"\u8bf7\u5148\u9009\u62e9\u65f6\u533a\u518d\u8f6c\u6362\u3002");
        return;
    }
    bool allDay =
        (SendMessageW(c_allDay, BM_GETCHECK, 0, 0) == BST_CHECKED);
    DateTime s, e;
    read_timestamp(c_dStart, c_tStart, &s);
    read_timestamp(c_dEnd, c_tEnd, &e);
    if (!allDay) {
        bool okS = ics_zone_local_to_utc(zone, &s);
        bool okE = ics_zone_local_to_utc(zone, &e);
        if (!okS || !okE) {
            ui_error(g_dlg,
                L"\u65f6\u533a\u8f6c\u6362\u5931\u8d25\u3002");
            return;
        }
        s.utc = e.utc = true;
        write_timestamp(c_dStart, c_tStart, &s);
        write_timestamp(c_dEnd, c_tEnd, &e);
    }
    SendMessageW(c_mode, CB_SETCURSEL, (WPARAM)(int)ICS_TIME_UTC, 0);
    update_mode_ui();
}

// ---------------------------------------------------------- dlg proc --
static void end_dialog(void) {
    g_running = false;
    if (g_dlg) DestroyWindow(g_dlg);
    PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
}

static void on_command(int id, int code) {
    switch (id) {
    case IDOK:
        if (dlg_store()) end_dialog();
        return;
    case IDCANCEL:
        end_dialog();
        return;
    case IDC_CHK_ALLDAY:
        update_allday_ui();
        return;
    case IDC_CB_TIMEMODE:
        if (code == CBN_SELCHANGE) update_mode_ui();
        return;
    case IDC_CB_FREQ:
        if (code == CBN_SELCHANGE) update_repeat_ui();
        return;
    case IDC_CB_ENDMODE:
        if (code == CBN_SELCHANGE) update_repeat_ui();
        return;
    case IDC_BTN_LOCALTZ: {
        wchar_t zone[128];
        ui_local_zone_key(zone, ICSG_ARRAY_COUNT(zone));
        SetWindowTextW(c_tz, zone);
        return;
    }
    case IDC_BTN_TOUTC:
        to_utc_clicked();
        return;
    case IDC_BTN_ATTADD:
        attendee_add();
        return;
    case IDC_BTN_ATTREMOVE:
        attendee_remove();
        return;
    case IDC_BTN_EXADD:
        date_add(false);
        return;
    case IDC_BTN_RDADD:
        date_add(true);
        return;
    case IDC_BTN_DATEREMOVE:
        date_remove();
        return;
    default:
        return;
    }
}

static BOOL CALLBACK enum_set_font(HWND child, LPARAM lp) {
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

static void release_scratch(void) {
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    for (int i = 0; i < g_zoneCount; ++i) ics_zone_info_free(&g_zones[i]);
    g_zoneCount = 0;
    for (int i = 0; i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_free(&g_attMail[i]);
        wstr_free(&g_attName[i]);
    }
    g_attCount = 0;
    g_dateCount = 0;
}

static void make_dialog_font(void);

static LRESULT CALLBACK EventDlgProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        on_command(LOWORD(wp), HIWORD(wp));
        return 0;

    case WM_CLOSE:
        end_dialog();
        return 0;

    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wp);
        if (!dpi) dpi = LOWORD(wp);
        if (dpi) g_dpi = dpi;
        make_dialog_font();
        EnumChildWindows(hwnd, enum_set_font, (LPARAM)g_font);
        dlg_layout();
        RECT* want = (RECT*)lp;
        if (want) {
            SetWindowPos(hwnd, NULL, want->left, want->top,
                         want->right - want->left,
                         want->bottom - want->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_DESTROY:
        release_scratch();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------ public api --
void eventdlg_register(HINSTANCE inst) {
    WNDCLASSEXW wc;
    xmemzero(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = EventDlgProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kDlgClass;
    RegisterClassExW(&wc);
}

static void make_dialog_font(void) {
    if (g_font) {
        DeleteObject(g_font);
        g_font = NULL;
    }
    LOGFONTW lf;
    xmemzero(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(9, (int)(g_dpi ? g_dpi : 96), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcopy(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    g_font = CreateFontIndirectW(&lf);
}

bool eventdlg_edit(HWND owner, IcsEvent* ev, bool isNew) {
    if (!ev) return false;
    g_ev = ev;
    g_isNew = isNew;
    g_ok = false;
    g_dpi = g_app.dpi ? g_app.dpi : 96;
    make_dialog_font();

    for (int i = 0; i < g_zoneCount; ++i) ics_zone_info_free(&g_zones[i]);
    g_zoneCount = ics_zone_enumerate(g_zones, ICSG_MAX_ZONES);
    for (int i = 0; i < ICSG_MAX_ATTENDEES; ++i) {
        wstr_init(&g_attMail[i]);
        wstr_init(&g_attName[i]);
    }
    g_attCount = 0;
    g_dateCount = 0;

    HWND ownerWnd = owner ? owner : g_app.mainWnd;
    const wchar_t* title = isNew ? L"\u65b0\u5efa\u4e8b\u4ef6" :
                                   L"\u7f16\u8f91\u4e8b\u4ef6";
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    g_dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDlgClass, title, style,
                            0, 0, 0, 0, ownerWnd, NULL, g_app.inst, NULL);
    if (!g_dlg) {
        release_scratch();
        return false;
    }
    build_controls();
    dlg_load();
    int logicalH = dlg_layout();

    // frame size for the laid out client area
    RECT fr;
    fr.left = 0;
    fr.top = 0;
    fr.right = dlg_scale(412);
    fr.bottom = dlg_scale(logicalH);
    AdjustWindowRectEx(&fr, style, FALSE, WS_EX_DLGMODALFRAME);
    int w = fr.right - fr.left;
    int h = fr.bottom - fr.top;
    // centre over the owner window
    RECT orc;
    if (ownerWnd && GetWindowRect(ownerWnd, &orc)) {
        int x = orc.left + ((orc.right - orc.left) - w) / 2;
        int y = orc.top + ((orc.bottom - orc.top) - h) / 3;
        SetWindowPos(g_dlg, NULL, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(g_dlg, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EnableWindow(ownerWnd, FALSE);
    g_running = true;
    ShowWindow(g_dlg, SW_SHOW);
    if (c_summary) SetFocus(c_summary);

    MSG msg;
    while (g_running) {
        int got = (int)GetMessageW(&msg, NULL, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        // Tab and arrow navigation between the programmatic controls.
        if (IsDialogMessageW(g_dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(ownerWnd, TRUE);
    SetActiveWindow(ownerWnd);
    g_dlg = NULL;
    return g_ok;
}
