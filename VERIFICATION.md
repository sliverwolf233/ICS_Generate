# VERIFICATION.md — independent clean-room verification (verify workstream)

Everything below was reproduced in a fresh build directory `build-verify/` from the
current workspace, with an independent Python oracle and driver written for this
verification only (under `artifacts/`). No source file was modified.

## Commands

    cmake -S . -B build-verify -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=E:/scoop/apps/mingw/current/bin/g++.exe \
      -DCMAKE_C_COMPILER=E:/scoop/apps/mingw/current/bin/gcc.exe
    cmake --build build-verify -j
    build-verify\bin\ics_core_tests.exe
    objdump -p build-verify\bin\ICS_Generate.exe   (saved: artifacts/imports_objdump.txt)
    g++ -std=c++17 -O1 -municode -I src/core artifacts/oracle_driver.cpp \
        build-verify/CMakeFiles/icsg_core.dir/src/core/{base,dt,tz,ics,ics_parse}.cpp.obj \
        -o build-verify/driver.exe
    python artifacts/rfc5545_oracle.py artifacts\samples\*.ics
    python artifacts/make_adversarial.py  +  build-verify\driver.exe parse <in> <reser.ics>
    python artifacts/rfc5545_oracle.py artifacts\adversarial\*.re.ics

## Verdicts per claim

| # | Claim | Verdict | Evidence |
|---|-------|---------|----------|
| 1 | Clean MinGW build green | PASS | configure + build exit 0, no errors |
| 2 | ics_core_tests all green | PASS | "17 passed, 0 failed", exit 0 (ics_core_tests.exe = 176,239 bytes) |
| 3 | Exe size 88,576 bytes | PASS (number confirmed) | Length = 88576, reproduced exactly |
| 4 | 9-DLL whitelist, no CRT | PASS | imports: ADVAPI32, COMCTL32, comdlg32, dwmapi, GDI32, KERNEL32, SHELL32, USER32, UxTheme — exactly 9, all whitelisted; NO msvcrt/ucrtbase/api-ms-win-crt/libgcc/libstdc++/libwinpthread |
| 5 | RFC 5545 output (CRLF, <=75-octet folding, single-space continuations, no split UTF-8, UTF-8 no BOM, UID+DTSTAMP per VEVENT, BEGIN/END balance, VTIMEZONE for TZID events) | PASS for all normal calendars | independent stdlib-only oracle artifacts/rfc5545_oracle.py: all 5 samples PASS (all-day, timed UTC, TZID "China Standard Time" incl. VTIMEZONE, RRULE weekly BYDAY=MO,WE + VALARM + EXDATE, 200x U+1F600 emoji + long summary) |
| 6 | Tolerant parser must not crash on malformed input | PASS | 12 adversarial fixtures, no crash/hang (30 s kill-guard); all reserialized outputs re-validated by the oracle |
| 7 | GUI launches, stays alive with visible window | PASS | alive after 5 s, MainWindowTitle "ICS Generate", closed cleanly |
| 8 | Target size <= 64 KB (x64 Release) | **FAIL (known risk, contract §1)** | 88,576 B > 65,536 B; within the <=96 KB hard CI gate but misses the 64 KB product target by 23,040 B |
| 9 | VTIMEZONE emitted for every TZID event | **FAIL (defect D1)** | see below |

## Defects / data-loss findings

### D1 (real defect): unknown TZID serializes with TZID= but no VTIMEZONE
Repro: build-verify\driver.exe parse artifacts\adversarial\mars_tz.ics artifacts\adversarial\mars_tz.ics.re.ics
with artifacts/adversarial/mars_tz.ics (DTSTART/DTEND ;TZID=Mars/Valles_Marineris).
Observed: reserialized output contains DTSTART;TZID=Mars/Valles_Marineris:... but no
BEGIN:VTIMEZONE; the independent oracle flags "no VTIMEZONE at all" (RFC 5545 requires a
matching VTIMEZONE for every referenced TZID). No crash; the TZID string round-trips intact.
Suggested fix: when ics_zone_vtimezone() fails for a TZID, either fall back to UTC or emit
a minimal VTIMEZONE carrying that TZID.

### Documented, non-crashing data loss (judged acceptable but should be user-visible)
- 10,000-event file -> only the first 1,024 events load (ICSG_MAX_EVENTS, frozen cap); 9,976 silently dropped.
- Invalid UTF-8 bytes (FF FE) -> replaced with U+FFFD on load (lossy, safe).
- SUMMARY with unknown escape \s -> backslash silently dropped on load (back\slash -> backslash); \; and \, round-trip correctly; a raw newline inside SUMMARY ends the property.
- Truncated file (cut mid-property) -> parses "successfully" as 1 event with empty summary and start 00000000T000000; arguably should be rejected.
- END before start (DTSTART 10:00 > DTEND 09:00) -> accepted by the parser without correction or error.
- 1 MB single line and TRIGGER:-P525600M (1-year alarm): parsed and reserialized fine, no overflow, no crash.

## Observed numbers
- ICS_Generate.exe: 88,576 bytes; 9 import DLLs (list above); no CRT imports.
- ics_core_tests.exe: 176,239 bytes; 17/17 PASS.
- Oracle: 5/5 sample calendars PASS; 12/13 reserialized adversarial outputs PASS (1 FAIL = D1).
- Launch: alive > 5 s, window "ICS Generate", ~12.5 MB working set.

## Residual risks
1. Size target: at 88,576 B the <=64 KB product target is not met on this toolchain; only the 96 KB CI gate holds. If 64 KB is still a requirement, the ~23 KB excess needs a plan (check MSVC /Os and clang builds in CI before concluding it is toolchain-inherent).
2. D1 is reachable from the UI: the TZID combo accepts arbitrary IANA names; any name Windows cannot resolve yields a standards-violating file strict consumers may reject.
3. Verification used MinGW only; the MSVC x64/Win32 CI paths were not reproducible on this machine.
4. The silent 1,024-event cap could surprise users merging large calendars; consider a status-bar warning.
5. One driver-side %s/%ls formatting artifact initially produced a false "TZID truncated to M" observation — retracted; TZID values round-trip fully.
