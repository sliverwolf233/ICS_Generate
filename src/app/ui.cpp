// ui.cpp - main window: menu bar, button row, event list, filter and status bar.
//
// Everything is created programmatically (no DIALOG resources, CONTRACT.md 2.3).
// The window owns the visible part of g_app.cal; the file level commands live in
// main.cpp, the modal event editor in eventdlg.cpp.
#include "ui.h"

// ------------------------------------------------------------------ layout --
enum {
    COL_SUMMARY = 0,
    COL_START,
    COL_END,
    COL_ALLDAY,
    COL_LOCATION,
    COL_TZ,
    COL_REPEAT,
    COL_REMINDER,
    COL_COUNT
};

#define ICSG_DARK_BG   RGB(32, 32, 32)
#define ICSG_DARK_TEXT RGB(240, 240, 240)
#define ICSG_DARK_EDIT RGB(48, 48, 48)

struct ButtonSpec {
    int id;
    int width;
    const wchar_t* text;
};

AppState g_app;

static const wchar_t kMainClass[] = L"IcsGenerateMainWnd";

static const wchar_t* const kColumnTitles[COL_COUNT] = {
    L"摘要", L"开始", L"结束", L"全天", L"地点", L"时区", L"重复", L"提醒"
};
static const int kColumnWidths[COL_COUNT] = { 220, 128, 128, 46, 150, 150, 120, 110 };

static const ButtonSpec kButtons[] = {
    { IDM_EDIT_ADD,     68, L"新建" },
    { IDM_EDIT_EDIT,    68, L"编辑" },
    { IDM_EDIT_DELETE,  68, L"删除" },
    { IDM_EDIT_DUP,     68, L"复制" },
    { IDM_EDIT_UP,      62, L"上移" },
    { IDM_EDIT_DOWN,    62, L"下移" },
    { IDM_FILE_OPEN,    68, L"打开" },
    { IDM_FILE_SAVE,    68, L"保存" },
    { IDM_FILE_EXPORT,  76, L"导出" }
};

// Keyboard shortcuts; Enter/F2 add the list editing keys of the contract.
static const ACCEL kAccels[] = {
    { FVIRTKEY | FCONTROL,          'N', IDM_EDIT_ADD },
    { FVIRTKEY | FCONTROL,          'O', IDM_FILE_OPEN },
    { FVIRTKEY | FCONTROL,          'S', IDM_FILE_SAVE },
    { FVIRTKEY | FCONTROL | FSHIFT, 'S', IDM_FILE_SAVEAS },
    { FVIRTKEY | FCONTROL | FSHIFT, 'N', IDM_FILE_NEW },
    { FVIRTKEY | FCONTROL,          'D', IDM_EDIT_DUP },
    { FVIRTKEY | FCONTROL,          'E', IDM_FILE_EXPORT },
    { FVIRTKEY | FCONTROL,          'F', IDM_EDIT_FIND },
    { FVIRTKEY,              VK_DELETE,  IDM_EDIT_DELETE },
    { FVIRTKEY,              VK_RETURN,  IDM_EDIT_EDIT },
    { FVIRTKEY,              VK_F2,      IDM_EDIT_EDIT },
    { FVIRTKEY,              VK_F1,      IDM_HELP_HELP }
};

// ------------------------------------------------------------- utilities ----
int ui_scale(int value) {
    if (g_app.dpi == 0) return value;
    return MulDiv(value, (int)g_app.dpi, 96);
}

bool ui_is_dark(void) { return g_app.dark; }
COLORREF ui_bg_color(void) { return g_app.dark ? ICSG_DARK_BG : GetSysColor(COLOR_BTNFACE); }
COLORREF ui_text_color(void) { return g_app.dark ? ICSG_DARK_TEXT : GetSysColor(COLOR_WINDOWTEXT); }
COLORREF ui_edit_color(void) { return g_app.dark ? ICSG_DARK_EDIT : GetSysColor(COLOR_WINDOW); }
HBRUSH ui_bg_brush(void) { return g_app.brushBg; }

void ui_error(HWND owner, const wchar_t* text) {
    MessageBoxW(owner ? owner : g_app.mainWnd, text, L"ICS Generate", MB_OK | MB_ICONERROR);
}

bool ui_confirm(HWND owner, const wchar_t* text) {
    return MessageBoxW(owner, text, L"ICS Generate", MB_YESNO | MB_ICONWARNING) == IDYES;
}

int ui_confirm_save(HWND owner, const wchar_t* text) {
    return (int)MessageBoxW(owner, text, L"ICS Generate",
                            MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
}

// Case folding good enough for the filter box: ASCII + Latin-1, everything else
// (Chinese) compares exactly.
static wchar_t lower_w(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return (wchar_t)(c + 32);
    return c;
}

static bool text_has_ci(const wchar_t* hay, const wchar_t* needle) {
    if (!needle[0]) return true;
    int nlen = wlen(needle);
    for (const wchar_t* p = hay; *p; ++p) {
        int i = 0;
        while (i < nlen && p[i] && lower_w(p[i]) == lower_w(needle[i])) ++i;
        if (i == nlen) return true;
    }
    return false;
}

static wchar_t* get_text_alloc(HWND ctl) {
    int n = GetWindowTextLengthW(ctl);
    wchar_t* buf = (wchar_t*)xmalloc(((size_t)n + 1) * sizeof(wchar_t));
    buf[0] = 0;
    if (n > 0) GetWindowTextW(ctl, buf, n + 1);
    return buf;
}

static void set_font_if_possible(HWND ctl) {
    if (ctl && g_app.font) SendMessageW(ctl, WM_SETFONT, (WPARAM)g_app.font, TRUE);
}

static BOOL CALLBACK enum_set_font(HWND child, LPARAM lp) {
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

static void apply_font_to_children(HWND parent) {
    if (parent && g_app.font) EnumChildWindows(parent, enum_set_font, (LPARAM)g_app.font);
}

static void make_font(void) {
    if (g_app.font) {
        DeleteObject(g_app.font);
        g_app.font = NULL;
    }
    LOGFONTW lf;
    xmemzero(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(9, (int)(g_app.dpi ? g_app.dpi : 96), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcopy(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    g_app.font = CreateFontIndirectW(&lf);
}

// Dynamic DPI lookup: GetDpiForWindow exists from Windows 10 1607 on, the
// GetProcAddress keeps the import table free of it.
typedef UINT (WINAPI *PfnGetDpiForWindow)(HWND);

static UINT query_dpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        PfnGetDpiForWindow fn =
            reinterpret_cast<PfnGetDpiForWindow>(reinterpret_cast<void*>(
                GetProcAddress(user32, "GetDpiForWindow")));
        if (fn) {
            UINT dpi = fn(hwnd);
            if (dpi) return dpi;
        }
    }
    HDC dc = GetDC(NULL);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return (UINT)(dpi > 0 ? dpi : 96);
}

void ui_local_zone_key(wchar_t* out, int cap) {
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    DWORD id = GetDynamicTimeZoneInformation(&dtzi);
    if (id == TIME_ZONE_ID_INVALID) {
        wcopy(out, cap, L"未知");
        return;
    }
    if (dtzi.TimeZoneKeyName[0]) wcopy(out, cap, dtzi.TimeZoneKeyName);
    else wcopy(out, cap, dtzi.StandardName);
}

void ui_local_zone_display(wchar_t* out, int cap) {
    ui_local_zone_key(out, cap);
}

// ------------------------------------------------------------ dark theme ----
static bool system_uses_dark_mode(void) {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LONG rc = RegGetValueW(HKEY_CURRENT_USER,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    return rc == ERROR_SUCCESS && value == 0;
}

void ui_apply_dark_frame(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = g_app.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));   // Windows 10 1803-1809
}

void ui_apply_theme(void) {
    g_app.dark = system_uses_dark_mode();
    if (g_app.brushBg) DeleteObject(g_app.brushBg);
    if (g_app.brushEdit) DeleteObject(g_app.brushEdit);
    g_app.brushBg = CreateSolidBrush(ui_bg_color());
    g_app.brushEdit = CreateSolidBrush(ui_edit_color());
    ui_apply_dark_frame(g_app.mainWnd);
    if (g_app.list) {
        ListView_SetBkColor(g_app.list, g_app.dark ? ICSG_DARK_BG : GetSysColor(COLOR_WINDOW));
        ListView_SetTextBkColor(g_app.list, g_app.dark ? ICSG_DARK_BG : GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor(g_app.list, g_app.dark ? ICSG_DARK_TEXT : GetSysColor(COLOR_WINDOWTEXT));
        SetWindowTheme(g_app.list, g_app.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        InvalidateRect(g_app.list, NULL, TRUE);
    }
    if (g_app.mainWnd) {
        InvalidateRect(g_app.mainWnd, NULL, TRUE);
        RedrawWindow(g_app.mainWnd, NULL, NULL,
                     RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

// ------------------------------------------------------------ list model ----
static void rows_push(int eventIndex) {
    if (g_app.rowCount >= g_app.rowCap) {
        int cap = g_app.rowCap ? g_app.rowCap * 2 : 64;
        g_app.rows = (int*)xrealloc(g_app.rows, (size_t)cap * sizeof(int));
        g_app.rowCap = cap;
    }
    g_app.rows[g_app.rowCount++] = eventIndex;
}

static void ev_col_text(const IcsEvent* ev, int col, WStr* out) {
    wstr_clear(out);
    switch (col) {
    case COL_SUMMARY:
        wstr_set(out, wstr_c(&ev->summary));
        break;
    case COL_START: {
        wchar_t buf[64];
        dt_to_display(&ev->start, buf, ICSG_ARRAY_COUNT(buf));
        wstr_set(out, buf);
        break;
    }
    case COL_END: {
        wchar_t buf[64];
        dt_to_display(&ev->end, buf, ICSG_ARRAY_COUNT(buf));
        wstr_set(out, buf);
        break;
    }
    case COL_ALLDAY:
        wstr_set(out, ev->allDay ? L"是" : L"否");
        break;
    case COL_LOCATION:
        wstr_set(out, wstr_c(&ev->location));
        break;
    case COL_TZ:
        if (ev->timeMode == ICS_TIME_UTC) wstr_set(out, L"UTC");
        else if (ev->timeMode == ICS_TIME_TZID) wstr_set(out, wstr_c(&ev->tzid));
        else wstr_set(out, L"浮动");
        break;
    case COL_REPEAT:
        ics_rrule_display(ev, out);
        break;
    case COL_REMINDER:
        ics_alarm_display(ev, out);
        break;
    default:
        break;
    }
}

static int row_compare(int ia, int ib) {
    const IcsEvent* a = &g_app.cal.events[ia];
    const IcsEvent* b = &g_app.cal.events[ib];
    int r = 0;
    if (g_app.sortCol == COL_START) {
        r = dt_compare(&a->start, &b->start);
        if (r == 0) r = dt_compare(&a->end, &b->end);
    } else if (g_app.sortCol == COL_END) {
        r = dt_compare(&a->end, &b->end);
        if (r == 0) r = dt_compare(&a->start, &b->start);
    } else if (g_app.sortCol >= 0) {
        WStr ta, tb;
        wstr_init(&ta);
        wstr_init(&tb);
        ev_col_text(a, g_app.sortCol, &ta);
        ev_col_text(b, g_app.sortCol, &tb);
        int c = CompareStringOrdinal(wstr_c(&ta), -1, wstr_c(&tb), -1, TRUE);
        wstr_free(&ta);
        wstr_free(&tb);
        r = c - 2;   // CSTR_LESS_THAN(1) / EQUAL(2) / GREATER_THAN(3)
    }
    if (!g_app.sortAsc) r = -r;
    if (r == 0) r = ia - ib;    // stable, and a total order for equal keys
    return r;
}

// Bottom-up merge sort: predictable O(n log n) and no recursion.
static void rows_sort(void) {
    int n = g_app.rowCount;
    if (g_app.sortCol < 0 || n < 2) return;
    int* tmp = (int*)xmalloc((size_t)n * sizeof(int));
    for (int width = 1; width < n; width *= 2) {
        for (int i = 0; i < n; i += 2 * width) {
            int lo = i;
            int mid = i + width < n ? i + width : n;
            int hi = i + 2 * width < n ? i + 2 * width : n;
            int a = lo, b = mid, k = lo;
            while (a < mid && b < hi) {
                if (row_compare(g_app.rows[b], g_app.rows[a]) < 0) tmp[k++] = g_app.rows[b++];
                else tmp[k++] = g_app.rows[a++];
            }
            while (a < mid) tmp[k++] = g_app.rows[a++];
            while (b < hi) tmp[k++] = g_app.rows[b++];
            for (int j = lo; j < hi; ++j) g_app.rows[j] = tmp[j];
        }
    }
    xfree(tmp);
}

static bool ev_matches_filter(const IcsEvent* ev, const wchar_t* needle) {
    return text_has_ci(wstr_c(&ev->summary), needle)
        || text_has_ci(wstr_c(&ev->location), needle)
        || text_has_ci(wstr_c(&ev->description), needle);
}

// --------------------------------------------------------- list controls ----
static void update_column_titles(void) {
    if (!g_app.list) return;
    for (int i = 0; i < COL_COUNT; ++i) {
        WStr t;
        wstr_init(&t);
        wstr_set(&t, kColumnTitles[i]);
        if (i == g_app.sortCol) wstr_append(&t, g_app.sortAsc ? L" \xE2\x96\xB2" : L" \xE2\x96\xBC");
        LVCOLUMNW col;
        xmemzero(&col, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = (LPWSTR)wstr_c(&t);
        ListView_SetColumn(g_app.list, i, &col);
        wstr_free(&t);
    }
}

static void insert_row(int row, int eventIndex) {
    const IcsEvent* ev = &g_app.cal.events[eventIndex];
    WStr t;
    wstr_init(&t);
    ev_col_text(ev, COL_SUMMARY, &t);
    LVITEMW item;
    xmemzero(&item, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = (LPWSTR)wstr_c(&t);
    item.lParam = (LPARAM)eventIndex;
    ListView_InsertItem(g_app.list, &item);
    for (int c = 1; c < COL_COUNT; ++c) {
        ev_col_text(ev, c, &t);
        ListView_SetItemText(g_app.list, row, c, (LPWSTR)wstr_c(&t));
    }
    wstr_free(&t);
}

static void select_row_for_event(int eventIndex) {
    if (!g_app.list || eventIndex < 0) return;
    for (int r = 0; r < g_app.rowCount; ++r) {
        if (g_app.rows[r] == eventIndex) {
            ListView_SetItemState(g_app.list, r,
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_app.list, r, FALSE);
            return;
        }
    }
}

void ui_select_event(int eventIndex) {
    g_app.selIndex = eventIndex;
    select_row_for_event(eventIndex);
}

int ui_selected_event(void) {
    if (!g_app.list) return -1;
    int row = ListView_GetNextItem(g_app.list, -1, LVNI_SELECTED);
    if (row < 0 || row >= g_app.rowCount) return -1;
    return g_app.rows[row];
}

void ui_refresh_list(void) {
    if (!g_app.list) return;
    SendMessageW(g_app.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_app.list);
    wchar_t* needle = g_app.filter ? get_text_alloc(g_app.filter) : NULL;
    g_app.rowCount = 0;
    for (int i = 0; i < g_app.cal.count; ++i) {
        const IcsEvent* ev = &g_app.cal.events[i];
        if (needle && needle[0] && !ev_matches_filter(ev, needle)) continue;
        rows_push(i);
    }
    if (needle) xfree(needle);
    rows_sort();
    for (int r = 0; r < g_app.rowCount; ++r) insert_row(r, g_app.rows[r]);
    SendMessageW(g_app.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_app.list, NULL, TRUE);
    update_column_titles();
    select_row_for_event(g_app.selIndex);
    ui_update_status();
}

void ui_toggle_sort(int column) {
    if (column < 0 || column >= COL_COUNT) return;
    if (g_app.sortCol == column) g_app.sortAsc = !g_app.sortAsc;
    else {
        g_app.sortCol = column;
        g_app.sortAsc = true;
    }
    ui_refresh_list();
}

void ui_focus_list(void) {
    if (g_app.list) SetFocus(g_app.list);
}

// ------------------------------------------------------------- status/title --
void ui_update_status(void) {
    if (!g_app.status) return;
    WStr t;
    wstr_init(&t);

    wstr_append(&t, L"事件 ");
    wstr_append_num(&t, g_app.rowCount, 0);
    wstr_append(&t, L" / ");
    wstr_append_num(&t, g_app.cal.count, 0);
    SendMessageW(g_app.status, SB_SETTEXTW, 0, (LPARAM)wstr_c(&t));

    int bytes = ics_serialize(&g_app.cal, NULL, 0);
    wstr_clear(&t);
    wstr_append(&t, L"大小 ");
    if (bytes < 0) wstr_append(&t, L"?");
    else wstr_append_num(&t, bytes, 0);
    wstr_append(&t, L" 字节");
    SendMessageW(g_app.status, SB_SETTEXTW, 1, (LPARAM)wstr_c(&t));

    wstr_clear(&t);
    wstr_append(&t, wstr_is_empty(&g_app.path) ? L"未命名" : wstr_c(&g_app.path));
    if (g_app.dirty) wstr_append(&t, L" *");
    SendMessageW(g_app.status, SB_SETTEXTW, 2, (LPARAM)wstr_c(&t));

    wchar_t zone[128];
    ui_local_zone_display(zone, ICSG_ARRAY_COUNT(zone));
    wstr_clear(&t);
    wstr_append(&t, L"本机时区 ");
    wstr_append(&t, zone);
    SendMessageW(g_app.status, SB_SETTEXTW, 3, (LPARAM)wstr_c(&t));

    wstr_free(&t);
}

void ui_update_title(void) {
    if (!g_app.mainWnd) return;
    WStr t;
    wstr_init(&t);
    if (wstr_is_empty(&g_app.path)) {
        wstr_append(&t, L"未命名");
    } else {
        const wchar_t* p = wstr_c(&g_app.path);
        const wchar_t* base = p;
        for (const wchar_t* q = p; *q; ++q) {
            if (*q == L'\\' || *q == L'/') base = q + 1;
        }
        wstr_append(&t, base);
    }
    if (g_app.dirty) wstr_append(&t, L" *");
    wstr_append(&t, L" - ICS Generate");
    SetWindowTextW(g_app.mainWnd, wstr_c(&t));
    wstr_free(&t);
}

// ----------------------------------------------------------------- layout ----
void ui_layout(HWND hwnd) {
    if (!hwnd) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;
    int pad = ui_scale(8);
    int bh = ui_scale(26);
    int gap = ui_scale(6);

    int statusH = 0;
    if (g_app.status) {
        SendMessageW(g_app.status, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(g_app.status, &sr);
        statusH = sr.bottom - sr.top;
    }

    int y = pad;
    int x = pad;
    for (int i = 0; i < g_app.buttonCount; ++i) {
        int bw = ui_scale(kButtons[i].width);
        if (g_app.buttons[i]) SetWindowPos(g_app.buttons[i], NULL, x, y, bw, bh, SWP_NOZORDER);
        x += bw + gap;
    }

    y += bh + gap;
    int labelW = ui_scale(40);
    if (g_app.lblFilter) {
        SetWindowPos(g_app.lblFilter, NULL, pad, y + ui_scale(5), labelW, bh, SWP_NOZORDER);
    }
    int filterX = pad + labelW + ui_scale(4);
    int filterW = ui_scale(240);
    if (g_app.filter) SetWindowPos(g_app.filter, NULL, filterX, y, filterW, bh, SWP_NOZORDER);
    if (g_app.lblHint) {
        int hintX = filterX + filterW + ui_scale(10);
        int hintW = cw - hintX - pad;
        if (hintW < 0) hintW = 0;
        SetWindowPos(g_app.lblHint, NULL, hintX, y + ui_scale(5), hintW, bh, SWP_NOZORDER);
    }

    y += bh + gap;
    int listH = ch - y - statusH - pad;
    if (listH < ui_scale(80)) listH = ui_scale(80);
    if (g_app.list) {
        SetWindowPos(g_app.list, NULL, pad, y, cw - 2 * pad, listH, SWP_NOZORDER);
    }

    if (g_app.status) {
        int parts[4];
        parts[0] = ui_scale(120);
        parts[1] = parts[0] + ui_scale(150);
        parts[2] = cw - ui_scale(300);
        if (parts[2] < parts[1] + ui_scale(100)) parts[2] = parts[1] + ui_scale(100);
        parts[3] = -1;
        SendMessageW(g_app.status, SB_SETPARTS, 4, (LPARAM)parts);
    }
}

// ----------------------------------------------------- window construction ----
static HWND mk_child(HWND parent, const wchar_t* cls, const wchar_t* text,
                     DWORD style, DWORD exStyle, int id) {
    HWND h = CreateWindowExW(exStyle, cls, text, style, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g_app.inst, NULL);
    set_font_if_possible(h);
    return h;
}

static HMENU build_menu(void) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_FILE_NEW, L"新建日历(&N)\tCtrl+Shift+N");
    AppendMenuW(file, MF_STRING, IDM_FILE_OPEN, L"打开(&O)...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_FILE_MERGE, L"合并导入(&M)...");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_SAVE, L"保存(&S)\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_FILE_SAVEAS, L"另存为(&A)...\tCtrl+Shift+S");
    AppendMenuW(file, MF_STRING, IDM_FILE_EXPORT, L"导出所选事件(&E)...\tCtrl+E");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_CLEAR, L"清空日历(&C)");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"退出(&X)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"文件(&F)");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, IDM_EDIT_ADD, L"新建事件(&N)\tCtrl+N");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_EDIT, L"编辑所选事件(&E)\tEnter / F2");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DUP, L"复制事件(&D)\tCtrl+D");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DELETE, L"删除事件(&L)\tDel");
    AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_UP, L"上移(&U)");
    AppendMenuW(edit, MF_STRING, IDM_EDIT_DOWN, L"下移(&W)");
    AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(edit, MF_STRING, IDM_EDIT_FIND, L"筛选(&F)\tCtrl+F");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"编辑(&E)");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_HELP_HELP, L"使用说明(&H)\tF1");
    AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"关于 ICS Generate(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"帮助(&H)");
    return bar;
}

static void create_children(HWND hwnd) {
    g_app.buttonCount = 0;
    for (int i = 0; i < ICSG_ARRAY_COUNT(kButtons); ++i) {
        g_app.buttons[g_app.buttonCount] = mk_child(
            hwnd, L"BUTTON", kButtons[i].text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, kButtons[i].id);
        ++g_app.buttonCount;
    }

    g_app.lblFilter = mk_child(hwnd, L"STATIC", L"筛选:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                               0, IDC_LBL_FILTER);
    g_app.filter = mk_child(hwnd, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            WS_EX_CLIENTEDGE, IDC_FILTER);
    g_app.lblHint = mk_child(hwnd, L"STATIC", L"匹配摘要 / 地点 / 备注（不区分大小写）",
                             WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_LBL_HINT);

    g_app.list = mk_child(hwnd, WC_LISTVIEWW, L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT
                              | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                          WS_EX_CLIENTEDGE, IDC_LIST);
    ListView_SetExtendedListViewStyle(g_app.list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER
                                          | LVS_EX_LABELTIP);
    for (int c = 0; c < COL_COUNT; ++c) {
        LVCOLUMNW col;
        xmemzero(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.cx = ui_scale(kColumnWidths[c]);
        col.pszText = (LPWSTR)kColumnTitles[c];
        col.iSubItem = c;
        ListView_InsertColumn(g_app.list, c, &col);
    }

    g_app.status = mk_child(hwnd, STATUSCLASSNAMEW, NULL,
                            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, IDC_STATUS);
}

HWND ui_create_main(HINSTANCE inst) {
    g_app.inst = inst;
    g_app.menu = build_menu();
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kMainClass,
                                L"ICS Generate", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, ui_scale(980), ui_scale(640),
                                NULL, g_app.menu, inst, NULL);
    if (!hwnd) return NULL;
    g_app.mainWnd = hwnd;
    g_app.dpi = query_dpi(hwnd);
    make_font();
    create_children(hwnd);
    g_app.accel = CreateAcceleratorTableW((LPACCEL)kAccels, ICSG_ARRAY_COUNT(kAccels));
    ui_apply_theme();
    ui_layout(hwnd);
    SetWindowPos(hwnd, NULL, 0, 0, ui_scale(980), ui_scale(640),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    DragAcceptFiles(hwnd, TRUE);
    ui_refresh_list();
    return hwnd;
}

// ----------------------------------------------------------- event edits ----
void ui_add_event(HWND owner) {
    if (g_app.cal.count >= ICSG_MAX_EVENTS) {
        ui_error(owner, L"事件数量已达上限（1024 个）。");
        return;
    }
    IcsEvent* ev = ics_calendar_add(&g_app.cal);
    if (!ev) {
        ui_error(owner, L"内存不足，无法新建事件。");
        return;
    }
    int index = g_app.cal.count - 1;
    if (!eventdlg_edit(owner, ev, true)) {
        ics_calendar_remove(&g_app.cal, index);
        ui_refresh_list();
        return;
    }
    app_mark_dirty();
    ui_select_event(index);
    ui_refresh_list();
}

void ui_edit_selected(HWND owner) {
    int index = ui_selected_event();
    if (index < 0) {
        ui_error(owner, L"请先在列表中选择一个事件。");
        return;
    }
    if (!eventdlg_edit(owner, &g_app.cal.events[index], false)) return;
    app_mark_dirty();
    ui_select_event(index);
    ui_refresh_list();
}

void ui_delete_selected(HWND owner) {
    int index = ui_selected_event();
    if (index < 0) {
        ui_error(owner, L"请先在列表中选择一个事件。");
        return;
    }
    WStr t;
    wstr_init(&t);
    wstr_append(&t, L"确定删除事件 \u201C");
    wstr_append(&t, wstr_c(&g_app.cal.events[index].summary));
    wstr_append(&t, L"\u201D 吗？");
    int answer = (int)MessageBoxW(owner, wstr_c(&t), L"ICS Generate",
                                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    wstr_free(&t);
    if (answer != IDYES) return;

    ics_calendar_remove(&g_app.cal, index);
    app_mark_dirty();
    int next = index;
    if (next >= g_app.cal.count) next = g_app.cal.count - 1;
    ui_select_event(next);
    ui_refresh_list();
}

void ui_duplicate_selected(HWND owner) {
    int index = ui_selected_event();
    if (index < 0) {
        ui_error(owner, L"请先在列表中选择一个事件。");
        return;
    }
    if (g_app.cal.count >= ICSG_MAX_EVENTS) {
        ui_error(owner, L"事件数量已达上限（1024 个）。");
        return;
    }
    IcsEvent* copy = ics_calendar_duplicate(&g_app.cal, &g_app.cal.events[index], true);
    if (!copy) {
        ui_error(owner, L"内存不足，无法复制事件。");
        return;
    }
    app_mark_dirty();
    ui_select_event(g_app.cal.count - 1);
    ui_refresh_list();
}

void ui_move_selected(HWND owner, int delta) {
    int index = ui_selected_event();
    if (index < 0) {
        ui_error(owner, L"请先在列表中选择一个事件。");
        return;
    }
    if (!ics_calendar_move(&g_app.cal, index, delta)) return;
    app_mark_dirty();
    ui_select_event(index + delta);
    ui_refresh_list();
}

// --------------------------------------------------------------- messages ----
static void on_command(HWND hwnd, WPARAM wp, LPARAM lp) {
    UINT id = LOWORD(wp);
    UINT code = HIWORD(wp);
    if (lp == (LPARAM)g_app.filter && code == EN_CHANGE) {
        ui_refresh_list();
        return;
    }
    switch (id) {
    case IDM_FILE_NEW:    app_cmd_new(hwnd); return;
    case IDM_FILE_OPEN:   app_cmd_open(hwnd); return;
    case IDM_FILE_MERGE:  app_cmd_merge(hwnd); return;
    case IDM_FILE_SAVE:   app_cmd_save(hwnd, false); return;
    case IDM_FILE_SAVEAS: app_cmd_save(hwnd, true); return;
    case IDM_FILE_EXPORT: app_cmd_export(hwnd); return;
    case IDM_FILE_CLEAR:  app_cmd_clear(hwnd); return;
    case IDM_FILE_EXIT:   app_cmd_exit(hwnd); return;
    case IDM_EDIT_ADD:    ui_add_event(hwnd); return;
    case IDM_EDIT_EDIT:   ui_edit_selected(hwnd); return;
    case IDM_EDIT_DELETE: ui_delete_selected(hwnd); return;
    case IDM_EDIT_DUP:    ui_duplicate_selected(hwnd); return;
    case IDM_EDIT_UP:     ui_move_selected(hwnd, -1); return;
    case IDM_EDIT_DOWN:   ui_move_selected(hwnd, 1); return;
    case IDM_EDIT_FIND:
        if (g_app.filter) {
            SetFocus(g_app.filter);
            SendMessageW(g_app.filter, EM_SETSEL, 0, -1);
        }
        return;
    case IDM_HELP_HELP:   app_cmd_help(hwnd); return;
    case IDM_HELP_ABOUT:  app_cmd_about(hwnd); return;
    default: return;
    }
}

static void on_notify(NMHDR* nh) {
    if (nh->hwndFrom != g_app.list) return;
    if (nh->code == LVN_COLUMNCLICK) {
        NMLISTVIEW* nv = (NMLISTVIEW*)nh;
        ui_toggle_sort(nv->iSubItem);
    } else if (nh->code == NM_DBLCLK) {
        ui_edit_selected(g_app.mainWnd);
    } else if (nh->code == LVN_ITEMCHANGED) {
        NMLISTVIEW* nv = (NMLISTVIEW*)nh;
        if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED)
            && nv->iItem >= 0 && nv->iItem < g_app.rowCount) {
            g_app.selIndex = g_app.rows[nv->iItem];
        }
    }
}

static LRESULT on_ctl_color(UINT msg, WPARAM wp) {
    HDC dc = (HDC)wp;
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ui_text_color());
        if (g_app.dark) SetBkColor(dc, ICSG_DARK_BG);
        return (LRESULT)(g_app.brushBg ? g_app.brushBg : GetSysColorBrush(COLOR_BTNFACE));
    }
    SetTextColor(dc, ui_text_color());
    SetBkColor(dc, ui_edit_color());
    return (LRESULT)(g_app.brushEdit ? g_app.brushEdit : GetSysColorBrush(COLOR_WINDOW));
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        ui_layout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = ui_scale(640);
        mmi->ptMinTrackSize.y = ui_scale(420);
        return 0;
    }

    case WM_COMMAND:
        on_command(hwnd, wp, lp);
        return 0;

    case WM_NOTIFY:
        on_notify((NMHDR*)lp);
        return 0;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t* path = (wchar_t*)xmalloc(4096 * sizeof(wchar_t));
        UINT got = DragQueryFileW(drop, 0, path, 4096);
        DragFinish(drop);
        if (got > 0) app_open_path(hwnd, path);
        xfree(path);
        return 0;
    }

    case WM_CLOSE:
        app_cmd_exit(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_app.brushBg ? g_app.brushBg : GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return on_ctl_color(msg, wp);

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        ui_apply_theme();
        return 0;

    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wp);
        if (!dpi) dpi = LOWORD(wp);
        if (dpi) g_app.dpi = dpi;
        RECT* want = (RECT*)lp;
        if (want) {
            SetWindowPos(hwnd, NULL, want->left, want->top,
                         want->right - want->left, want->bottom - want->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        make_font();
        apply_font_to_children(hwnd);
        ui_layout(hwnd);
        ui_update_status();
        return 0;
    }

    case WM_INITMENUPOPUP: {
        HMENU menu = (HMENU)wp;
        UINT state = ui_selected_event() >= 0 ? MF_ENABLED : MF_GRAYED;
        const UINT ids[] = { IDM_EDIT_EDIT, IDM_EDIT_DELETE, IDM_EDIT_DUP,
                             IDM_EDIT_UP, IDM_EDIT_DOWN, IDM_FILE_EXPORT };
        for (int i = 0; i < ICSG_ARRAY_COUNT(ids); ++i) {
            EnableMenuItem(menu, ids[i], MF_BYCOMMAND | state);
        }
        return 0;
    }

    case WM_SETFOCUS:
        if (g_app.list) SetFocus(g_app.list);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ui_register_class(HINSTANCE inst) {
    WNDCLASSEXW wc;
    xmemzero(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
}
