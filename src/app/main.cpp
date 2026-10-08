// main.cpp - CRT-free process entry, app-level commands, file dialogs, loop.
//
// AppMain is called from crt_entry.cpp; the WinMain-free entry keeps the image
// free of any C runtime (CONTRACT.md 2.1). Everything else follows ui.h.
#include "ui.h"

// ------------------------------------------------------------------ state --
// Path given on the command line; opened after the main window exists.
static wchar_t g_cmdPath[512];

// Open/save dialog filter: iCalendar filter then an all-files fallback.
static const wchar_t kIcsFilter[] =
    L"iCalendar \u65e5\u5386 (*.ics)\0*.ics\0"
    L"\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0\0";

#define ICSG_SAVE_PROMPT \
    L"\u5f53\u524d\u65e5\u5386\u5df2\u4fee\u6539\uff0c\u662f\u5426\u4fdd\u5b58\u66f4\u6539\uff1f"

// ---------------------------------------------------------- file dialogs --
// buf receives the chosen path; returns false when the user cancels.
static bool dialog_get_open_path(HWND owner, wchar_t* buf, int cap) {
    OPENFILENAMEW ofn;
    xmemzero(&ofn, sizeof(ofn));
    buf[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = kIcsFilter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)cap;
    ofn.lpstrDefExt = L"ics";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn) != 0;
}

static bool dialog_get_save_path(HWND owner, wchar_t* buf, int cap) {
    OPENFILENAMEW ofn;
    xmemzero(&ofn, sizeof(ofn));
    buf[0] = 0;
    if (!wstr_is_empty(&g_app.path)) wcopy(buf, cap, wstr_c(&g_app.path));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = kIcsFilter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)cap;
    ofn.lpstrDefExt = L"ics";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    return GetSaveFileNameW(&ofn) != 0;
}

// Asks about unsaved changes first. true: proceed, false: abort.
static bool confirm_lose_changes(HWND owner) {
    if (!g_app.dirty) return true;
    int answer = ui_confirm_save(owner, ICSG_SAVE_PROMPT);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) {
        app_cmd_save(owner, false);
        if (g_app.dirty) return false;   // save failed or was cancelled
    }
    return true;
}

// ------------------------------------------------------------- commands --
void app_mark_dirty(void) {
    if (g_app.dirty) return;
    g_app.dirty = true;
    ui_update_title();
    ui_update_status();
}

bool app_open_path(HWND owner, const wchar_t* path) {
    IcsCalendar loaded;
    ics_calendar_init(&loaded);
    if (!ics_load_file(&loaded, path)) {
        ics_calendar_free(&loaded);
        WStr msg;
        wstr_init(&msg);
        wstr_append(&msg, L"\u65e0\u6cd5\u6253\u5f00\u6587\u4ef6\uff1a");
        wstr_append(&msg, path);
        ui_error(owner, wstr_c(&msg));
        wstr_free(&msg);
        return false;
    }
    if (!confirm_lose_changes(owner)) {
        ics_calendar_free(&loaded);
        return false;
    }
    ics_calendar_clear(&g_app.cal);
    g_app.cal = loaded;                 // heap blocks move across, no deep copy
    wstr_set(&g_app.path, path);
    g_app.dirty = false;
    g_app.selIndex = -1;
    ui_refresh_list();
    ui_update_title();
    return true;
}

void app_cmd_new(HWND owner) {
    if (!confirm_lose_changes(owner)) return;
    ics_calendar_clear(&g_app.cal);
    wstr_clear(&g_app.path);
    g_app.dirty = false;
    g_app.selIndex = -1;
    ui_refresh_list();
    ui_update_title();
}

void app_cmd_open(HWND owner) {
    wchar_t buf[512];
    if (!dialog_get_open_path(owner, buf, ICSG_ARRAY_COUNT(buf))) return;
    app_open_path(owner, buf);
}

void app_cmd_merge(HWND owner) {
    wchar_t buf[512];
    if (!dialog_get_open_path(owner, buf, ICSG_ARRAY_COUNT(buf))) return;
    IcsCalendar src;
    ics_calendar_init(&src);
    if (!ics_load_file(&src, buf)) {
        ics_calendar_free(&src);
        WStr msg;
        wstr_init(&msg);
        wstr_append(&msg, L"\u65e0\u6cd5\u8bfb\u53d6\u6587\u4ef6\uff1a");
        wstr_append(&msg, buf);
        ui_error(owner, wstr_c(&msg));
        wstr_free(&msg);
        return;
    }
    int added = ics_merge(&g_app.cal, &src);
    if (added <= 0) {
        ui_error(owner, L"\u6587\u4ef6\u4e2d\u6ca1\u6709\u53ef\u5bfc\u5165\u7684\u4e8b\u4ef6\u3002");
        return;
    }
    app_mark_dirty();
    ui_refresh_list();
}

void app_cmd_save(HWND owner, bool askPath) {
    wchar_t buf[512];
    if (askPath || wstr_is_empty(&g_app.path)) {
        if (!dialog_get_save_path(owner, buf, ICSG_ARRAY_COUNT(buf))) return;
    } else {
        wcopy(buf, ICSG_ARRAY_COUNT(buf), wstr_c(&g_app.path));
    }
    if (!ics_save_file(&g_app.cal, buf)) {
        WStr msg;
        wstr_init(&msg);
        wstr_append(&msg, L"\u4fdd\u5b58\u5931\u8d25\uff1a");
        wstr_append(&msg, buf);
        ui_error(owner, wstr_c(&msg));
        wstr_free(&msg);
        return;
    }
    wstr_set(&g_app.path, buf);
    g_app.dirty = false;
    ui_update_title();
    ui_update_status();
}

void app_cmd_export(HWND owner) {
    int index = ui_selected_event();
    if (index < 0) {
        ui_error(owner, L"\u8bf7\u5148\u5728\u5217\u8868\u4e2d\u9009\u62e9\u4e00\u4e2a\u4e8b\u4ef6\u3002");
        return;
    }
    wchar_t buf[512];
    if (!dialog_get_save_path(owner, buf, ICSG_ARRAY_COUNT(buf))) return;
    if (!ics_save_event_file(&g_app.cal.events[index], buf)) {
        WStr msg;
        wstr_init(&msg);
        wstr_append(&msg, L"\u5bfc\u51fa\u5931\u8d25\uff1a");
        wstr_append(&msg, buf);
        ui_error(owner, wstr_c(&msg));
        wstr_free(&msg);
        return;
    }
}

void app_cmd_clear(HWND owner) {
    if (g_app.cal.count == 0) return;
    if (!ui_confirm(owner, L"\u786e\u5b9a\u8981\u6e05\u7a7a\u6574\u4e2a\u65e5\u5386\u5417\uff1f")) return;
    ics_calendar_clear(&g_app.cal);
    app_mark_dirty();
    g_app.selIndex = -1;
    ui_refresh_list();
}

void app_cmd_exit(HWND owner) {
    if (!confirm_lose_changes(owner)) return;
    DestroyWindow(g_app.mainWnd);
}

void app_cmd_about(HWND owner) {
    MessageBoxW(owner ? owner : g_app.mainWnd,
                L"ICS Generate 1.0.0\n"
                L"\u7eaf Win32 iCalendar \u65e5\u5386\u7f16\u8f91\u5668\uff08RFC 5545\uff09\n"
                L"\u65e0 CRT \u4f9d\u8d56\uff0c\u4ec5\u4f7f\u7528\u7cfb\u7edf DLL\u3002\n\n"
                L"(c) sliverwolf233",
                L"\u5173\u4e8e ICS Generate", MB_OK | MB_ICONINFORMATION);
}

void app_cmd_help(HWND owner) {
    MessageBoxW(owner ? owner : g_app.mainWnd,
                L"\u2022 Ctrl+N \u65b0\u5efa\u4e8b\u4ef6\uff0c\u53cc\u51fb\u3001Enter \u6216 F2 \u7f16\u8f91\uff0cDel \u5220\u9664\u3002\n"
                L"\u2022 Ctrl+D \u590d\u5236\u6240\u9009\u4e8b\u4ef6\uff0cCtrl+E \u5bfc\u51fa\u6240\u9009\u4e8b\u4ef6\u3002\n"
                L"\u2022 Ctrl+O \u6253\u5f00\u3001Ctrl+S \u4fdd\u5b58\u3001Ctrl+Shift+S \u53e6\u5b58\u4e3a\u3002\n"
                L"\u2022 \u9876\u90e8\u7b5b\u9009\u6846\u6309\u6458\u8981/\u5730\u70b9/\u5907\u6ce8\u5b9e\u65f6\u8fc7\u6ee4\uff0c\u70b9\u5217\u5934\u6392\u5e8f\u3002\n"
                L"\u2022 \u53ef\u5c06 .ics \u6587\u4ef6\u62d6\u653e\u5230\u7a97\u53e3\u6253\u5f00\uff0c\u6216\u7528\u547d\u4ee4\u884c\u53c2\u6570\u6253\u5f00\u3002",
                L"\u4f7f\u7528\u8bf4\u660e", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------ entry point --
static void show_usage(void) {
    MessageBoxW(NULL,
                L"ICS Generate - iCalendar \u65e5\u5386\u7f16\u8f91\u5668\n\n"
                L"\u7528\u6cd5: ICS_Generate.exe [\u6587\u4ef6.ics]\n"
                L"  /? \u6216 --help \u663e\u793a\u672c\u5e2e\u52a9",
                L"ICS Generate", MB_OK | MB_ICONINFORMATION);
}

static void parse_command_line(void) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        if (weq(argv[i], L"/?") || weq(argv[i], L"-?") ||
            weq(argv[i], L"--help") || weq(argv[i], L"/help")) {
            show_usage();
            break;
        }
        if (argv[i][0] != L'-' && argv[i][0] != L'/') {
            wcopy(g_cmdPath, ICSG_ARRAY_COUNT(g_cmdPath), argv[i]);
            break;
        }
    }
    LocalFree(argv);
}

extern "C" int AppMain(void) {
    g_app.inst = GetModuleHandleW(NULL);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_DATE_CLASSES
              | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    parse_command_line();

    ui_register_class(g_app.inst);
    eventdlg_register(g_app.inst);

    // ui_create_main also builds the in-memory accelerator table
    // (Ctrl+N/O/S/Shift+S/D/E, Del, Enter, F2, Ctrl+F, F1).
    if (!ui_create_main(g_app.inst)) return 1;
    ShowWindow(g_app.mainWnd, SW_SHOW);

    if (g_cmdPath[0]) app_open_path(g_app.mainWnd, g_cmdPath);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_app.accel && TranslateAcceleratorW(g_app.mainWnd, g_app.accel, &msg))
            continue;
        // Tab navigation for the programmatically laid out main window.
        if (IsDialogMessageW(g_app.mainWnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
