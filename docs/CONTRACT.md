# ICS Generate - frozen engineering contract

Owner of this document: **Lead**. It is the coordination artifact for all writers.
Anything marked *frozen* may only be changed by the Lead.

## 1. Product

A single self-contained Windows GUI executable that creates, edits, opens and saves
.ics (iCalendar, RFC 5545) files. Pure Win32: User32 + GDI + Common Controls v6,
DWM/UxTheme for the frame, Shell/Comdlg32 for file dialogs. No MFC, no ATL, no WIL,
no WinUI 3 / Windows App SDK, no third-party code, no runtime redistributable.
The hard product requirement is **minimum executable size**: the app is built
**CRT-free** (no C runtime at all) and links only Windows system DLLs.

Target size: **<= 64 KB** (x64 Release). Hard CI gate: **<= 96 KB** on every
architecture and toolchain. The exact size is printed in the CI job summary.

## 2. Frozen constraints (every writer)

1. **No CRT in the app target.** MSVC: /NODEFAULTLIB, custom entry, own
   memset/memcpy/memmove/memcmp definitions. MinGW: -nostdlib -nostartfiles
   -nodefaultlibs. Therefore every file compiled into icsg_core or ICS_Generate:
   - includes only Windows headers (windows.h, commctrl.h, shellapi.h, commdlg.h,
     dwmapi.h, uxtheme.h, shlobj.h);
   - never includes string.h, stdio.h, stdlib.h, time.h, new, memory, string,
     vector, algorithm or any other CRT/STL header;
   - uses no new/delete, no exceptions, no RTTI, no std::, no float/double;
   - uses the xmalloc/xrealloc/xfree/WStr/utf8_* helpers from src/core/base.h.
1b. **Stack discipline.** No single stack frame may exceed 4096 bytes and no stack
   buffer may exceed 1 KB (a CRT-free image has no __chkstk). Use xmalloc for large
   temporary buffers, and free them before returning.
2. **No dynamic initialisers.** No global objects with constructors; global state must
   be POD and zero-initialised. Function-local static with a runtime initialiser is
   also forbidden. static const scalars/arrays are fine.
3. **No DIALOG resources.** Every window, control and menu is created programmatically
   so UI strings stay in UTF-8 C++ sources. src/app/app.rc contains only ICON and
   VERSIONINFO, ASCII text only.
4. **Sources are UTF-8 without BOM.** MSVC is invoked with /utf-8, so wide Chinese
   literals are safe in both toolchains. Chinese UI text is the default language;
   keep labels short.
5. **Import whitelist** (link libraries): kernel32 user32 gdi32 comctl32 shell32
   shlwapi ole32 comdlg32 advapi32 dwmapi uxtheme. Import nothing else; if you think
   you need another DLL, ask the Lead first.
6. **Written .ics files**: UTF-8 without BOM, CRLF line endings, lines folded at 75
   octets with a single leading space on continuations, RFC 5545 escaping.
7. **Warning clean**: MSVC /W4, GCC -Wall -Wextra. Use every parameter or write
   (void)param; - no unused-function noise.
8. **No shelling out, no network, no registry writes.**

## 3. Layout and write scopes

| Path | Owner | Notes |
|---|---|---|
| src/core/base.h, dt.h, ics.h | Lead | **frozen** API |
| src/core/base.cpp, dt.cpp, ics.cpp, ics_parse.cpp | core | implementation |
| tests/test_core.cpp | core | CRT console test runner |
| src/app/main.cpp, ui.cpp, ui.h, eventdlg.cpp, resource.h, app.rc | ui | Win32 GUI |
| CMakeLists.txt, cmake/*, .github/workflows/*, .gitignore, LICENSE, README.md, scripts/* | build | build + CI |
| res/app.manifest, res/app.ico, tools/make_icon.py, docs/CONTRACT.md | Lead | frozen inputs |
| VERIFICATION.md, artifacts/* | verify | independent verification report |

Never edit a file outside your scope. Need a change elsewhere? Message the Lead.

## 4. Build interface (agreed names - do not rename)

* CMake project ICS_Generate, version 1.0.0.
* Object library icsg_core built from src/core/*.cpp with the tiny flags.
* ICS_Generate - WIN32 executable from src/app/*.cpp + icsg_core + app.rc;
  output name ICS_Generate.exe in CMAKE_BINARY_DIR/bin.
* ics_core_tests - console executable from tests/test_core.cpp + icsg_core
  (a normal CRT console app is fine for tests), output bin/ics_core_tests.exe.
* Option ICSG_BUILD_TESTS (default ON) controls the test target.
* Local build (this machine has no MSVC):
  cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
  cmake --build build-mingw -j
* CI build: MSVC x64 + Win32 (Visual Studio 2022 generator) and MinGW-w64 (MSYS2) -
  see .github/workflows/ci.yml.

## 5. Application behaviour (agreed product surface)

Main window: menu bar (File / Edit / Help), a button row, a report-mode
SysListView32 of events, and a status bar.

* Columns: Summary | Start | End | All-day | Location | Repeat | Reminder.
* Commands: New, Edit, Delete, Duplicate, Move up, Move down, Open, Merge import,
  Save, Save as, Export selected event, Clear calendar, About, Exit.
  Shortcuts: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+D, Ctrl+E (export), Del, Enter, F2.
* A filter edit box narrows the list by summary/location/description substring
  (case-insensitive), and clicking the Summary/Start/End column header sorts by it.
* Double click / Enter / F2 edits the selected event.
* The event editor is a modal programmatic window with: Summary, Location,
  start/end SysDateTimePick32 plus a time-mode combo (floating local time / UTC /
  explicit TZID with an editable combo of common IANA names), All-day checkbox,
  multi-line Description, URL, Categories, Organizer (name + e-mail), Attendees
  (list + add/remove), Status, Priority, up to 4 reminders (checkbox + minutes
  up-down each), EXDATE/RDATE lists (add/remove with a date picker),
  Repeat (FREQ combo, interval, end mode never/count/date, BYDAY checkboxes), and a
  button that converts the entered local wall time to UTC with the Win32 timezone API
  (TzSpecificLocalTimeToSystemTime).
* Drag and drop a .ics onto the window opens it (DragAcceptFiles).
* The title shows the file name and a * when modified; closing with unsaved changes
  asks Save / Do not save / Cancel.
* ICS_Generate.exe <path.ics> opens that file at startup.
* All-day events write DTSTART;VALUE=DATE + DTEND;VALUE=DATE (end exclusive).
* Timed events: DTSTART[;TZID=..] or with a Z suffix, and a matching DTEND. End must
  be after start (for all-day events, end date must be after start date).
* Every event carries UID, DTSTAMP, SEQUENCE; LAST-MODIFIED is set when edited.
* Appearance: Segoe UI 9pt scaled by DPI, Common Controls v6 manifest, dark-mode
  aware frame via DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE), resizable
  window with the list stretching and the status bar keeping its height.

## 6. Acceptance criteria

* **core**: ics_core_tests passes every case: escaping/unescaping, 75-octet folding
  with multi-byte UTF-8, CRLF, DATE / DATE-TIME / UTC round trips, RRULE text,
  TRIGGER durations, UID uniqueness, UTF-8 codec edge cases (surrogates, 4-byte,
  overlong, truncated), full save-load-compare round trip, tolerant parse of a messy
  calendar (LF only, BOM, unknown properties, folded long line, missing END).
* **ui**: builds warning-clean, window opens, create/edit/delete/save/open works, and
  no CRT DLL is imported (objdump -p locally, dumpbin /imports in CI: no msvcrt,
  ucrtbase or api-ms-win-crt-* entries).
* **build**: local MinGW build green, exe <= 64 KB, tests run in CI, artifacts
  uploaded, a v* tag produces a GitHub Release with the exe.
* **verify**: independent reproduction of the above from a clean checkout.

## 7. Communication

Report blockers to the Lead immediately with the exact command and error text.
When a task is done: run your build/tests, then message the Lead with the commands
you ran and their results.
