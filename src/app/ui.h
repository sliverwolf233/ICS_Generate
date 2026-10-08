// ui.h - shared declarations of the Win32 application.
// Every translation unit of ICS_Generate includes this first so that the
// Windows headers are configured identically everywhere.
#pragma once
#ifndef ICSG_UI_H
#define ICSG_UI_H

// Windows 7 API level: the app uses the manifest for DPI awareness and resolves
// the newer entry points dynamically, so it also runs on older Windows 10 builds
// and never imports an API that does not exist there.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "../core/ics.h"
#include "resource.h"

// DWM attribute for the immersive dark frame (Windows 10 1809+); the value is
// missing from older SDK headers.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Process-wide state: POD, zero-initialised, no constructors (CONTRACT.md 2.2).
struct AppState {
    HINSTANCE   inst;
    HWND        mainWnd;
    HWND        list;
    HWND        filter;
    HWND        lblFilter;
    HWND        lblHint;
    HWND        status;
    HWND        buttons[9];
    int         buttonCount;
    HFONT       font;
    HBRUSH      brushBg;
    HBRUSH      brushEdit;
    HACCEL      accel;
    HMENU       menu;
    UINT        dpi;
    bool        dark;
    bool        dirty;
    int         sortCol;     // -1: model order
    bool        sortAsc;
    int         selIndex;    // event index kept selected across a refresh
    IcsCalendar cal;
    WStr        path;        // current file, empty when the calendar is untitled
    int*        rows;        // visible row -> event index
    int         rowCount;
    int         rowCap;
};

extern AppState g_app;

// ------------------------------------------------------------------ ui.cpp --
void ui_register_class(HINSTANCE inst);
HWND ui_create_main(HINSTANCE inst);
void ui_layout(HWND hwnd);
void ui_refresh_list(void);
void ui_update_title(void);
void ui_update_status(void);
void ui_apply_theme(void);
void ui_apply_dark_frame(HWND hwnd);
void ui_select_event(int eventIndex);
int  ui_selected_event(void);
void ui_focus_list(void);
void ui_toggle_sort(int column);

void ui_add_event(HWND owner);
void ui_edit_selected(HWND owner);
void ui_delete_selected(HWND owner);
void ui_duplicate_selected(HWND owner);
void ui_move_selected(HWND owner, int delta);

int  ui_scale(int value);
bool ui_is_dark(void);
COLORREF ui_bg_color(void);
COLORREF ui_text_color(void);
COLORREF ui_edit_color(void);
HBRUSH   ui_bg_brush(void);
HBRUSH   ui_edit_brush(void);
void ui_error(HWND owner, const wchar_t* text);
bool ui_confirm(HWND owner, const wchar_t* text);
int  ui_confirm_save(HWND owner, const wchar_t* text);
void ui_local_zone_key(wchar_t* out, int cap);
void ui_local_zone_display(wchar_t* out, int cap);

// ---------------------------------------------------------------- main.cpp --
void app_mark_dirty(void);
bool app_open_path(HWND owner, const wchar_t* path);
void app_cmd_new(HWND owner);
void app_cmd_open(HWND owner);
void app_cmd_merge(HWND owner);
void app_cmd_save(HWND owner, bool askPath);
void app_cmd_export(HWND owner);
void app_cmd_clear(HWND owner);
void app_cmd_exit(HWND owner);
void app_cmd_about(HWND owner);
void app_cmd_help(HWND owner);

// ------------------------------------------------------------ eventdlg.cpp --
void eventdlg_register(HINSTANCE inst);
// Shows the modal editor for *ev. Returns true when the user pressed OK, in
// which case *ev holds the edited event; false leaves *ev untouched.
bool eventdlg_edit(HWND owner, IcsEvent* ev, bool isNew);

#endif // ICSG_UI_H
