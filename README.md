# ICS Generate

[![CI](https://github.com/sliverwolf233/ICS_Generate/actions/workflows/ci.yml/badge.svg)](https://github.com/sliverwolf233/ICS_Generate/actions/workflows/ci.yml)

一个纯 Win32、零依赖、CRT-free 的 Windows 日历（.ics，iCalendar / RFC 5545）编辑器。
单个 exe，仅链接 Windows 系统自带 DLL，无需任何运行库安装，体积只有几十 KB。

---

## 功能特性

### 日历与事件管理
- **创建 / 编辑 / 删除 / 复制（Duplicate）事件**，支持上移 / 下移重新排序。
- **打开 / 保存 / 另存为** .ics 文件；**合并导入（Merge import）**将另一个日历并入当前日历。
- **导出选中的单个事件**为独立 .ics 文件（Ctrl+E）。
- **清空当前日历**。
- 命令行打开：`ICS_Generate.exe <文件.ics>` 启动时直接加载指定日历。
- 拖放：把 .ics 文件拖到主窗口即可打开。
- 标题栏显示文件名，未保存修改时追加 `*`；关闭未保存的日历时询问"保存 / 不保存 / 取消"。

### 时区（完全由用户选择）
- **枚举 Windows 全部动态时区**（`EnumDynamicTimeZoneInformation`），任何系统时区都可作为事件的 TZID。
- 支持**手动输入 IANA TZID**（如 `Asia/Shanghai`、`America/New_York`）。
- VTIMEZONE 组件依据 **Windows 时区数据库**（动态时区 API）自动生成，含正确的夏令时规则。
- 时间模式三选一：浮动本地时间 / UTC（`Z` 后缀）/ 显式 TZID。
- 编辑器内置"本地时间转 UTC"按钮（`TzSpecificLocalTimeToSystemTime`）。

### 事件字段
- 摘要（Summary）、地点（Location）、**多行备注**（Description）、URL、分类（Categories）。
- 组织者（Organizer：姓名 + 电子邮件）与**参会人列表**（Attendees，可增删）。
- 状态（Status）、优先级（Priority）。
- **最多 4 个提醒**（VALARM，每个为"启用复选框 + 提前分钟数"）。
- **重复规则 RRULE**：FREQ、间隔（INTERVAL）、结束方式（永不 / COUNT / UNTIL 日期）、
  BYDAY（按星期）、BYMONTHDAY（按日期），以及 **EXDATE**（排除日期）与 **RDATE**（附加日期）列表，均可通过日期选择器增删。
- 全天事件写 `DTSTART;VALUE=DATE` + `DTEND;VALUE=DATE`（结束日期独占）；
  计时事件写 `DTSTART[;TZID=…]` 或 `…Z` 及对应 DTEND，并校验结束晚于开始。
- 每个事件自动携带 UID、DTSTAMP、SEQUENCE，编辑时更新 LAST-MODIFIED。

### 界面
- **过滤框**：按摘要 / 地点 / 备注子串（不区分大小写）实时过滤列表。
- **列排序**：点击 摘要 / 开始 / 结束 列头排序。
- **列**：摘要 | 开始 | 结束 | 全天 | 地点 | 重复 | 提醒。
- 双击 / Enter / F2 编辑选中事件；Del 删除。
- 快捷键：Ctrl+N 新建、Ctrl+O 打开、Ctrl+S 保存、Ctrl+Shift+S 另存为、Ctrl+D 复制、Ctrl+E 导出、Del 删除、Enter/F2 编辑。
- **深色模式感知**：通过 `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` 跟随系统深浅色主题。
- **逐显示器 DPI 感知**，Segoe UI 9pt 按 DPI 缩放；可调整窗口大小，列表自适应、状态栏保持高度。
- Common Controls v6 视觉样式（清单内嵌于 exe）。
- 菜单栏（文件 / 编辑 / 帮助）、按钮行、报表模式列表（SysListView32）、状态栏。

### 写出的 .ics 文件质量
- UTF-8 无 BOM、CRLF 行尾。
- 按 RFC 5545 在 **75 字节处折行**（多字节 UTF-8 安全折行），续行前置单个空格。
- 完整的 RFC 5545 转义（逗号、分号、反斜杠、换行）。
- 解析器容错：接受仅 LF 行尾、BOM、未知属性、超长折行、缺失 END 的"脏"日历。

## 体积与依赖声明

- **CRT-free**：镜像完全不导入 C 运行时（无 msvcrt、ucrtbase、api-ms-win-crt-*，也无 libgcc / libstdc++），入口为自定义 `AppEntry`，自带 memset/memcpy/memmove/memcmp。
- **只导入系统 DLL**（白名单，CI 强制校验）：kernel32、user32、gdi32、comctl32、shell32、shlwapi、ole32、comdlg32、advapi32、dwmapi、uxtheme。
- **体积**：几十 KB（目标 ≤ 64 KB）。CI 硬性门槛 **≤ 96 KB**（三种工具链均强制），超限即构建失败；每次构建都会打印精确字节数并在 CI 摘要中给出体积表。
- **无需安装**：拷贝单个 exe 即可运行，Windows 10 / 11 x64 / x86 均可。

## CI 与发布

- [`.github/workflows/ci.yml`](.github/workflows/ci.yml) 在 `windows-latest` 上以三种工具链构建：
  **MSVC x64**、**MSVC x86 (Win32)**、**MinGW-w64 x64（MSYS2）**。
- 每个作业：Release 构建 → 运行单元测试 → 体积 ≤ 96 KB 门槛 →
  `dumpbin /imports`（MSVC）/ `objdump -p`（MinGW）证明无 CRT 导入且导入表不超出白名单 →
  将体积表写入 `$GITHUB_STEP_SUMMARY` → 上传 exe 构件（artifact）。
- 推送 `v*` 标签（如 `v1.0.0`）时自动创建 **GitHub Release**，附带三种构建的 exe
  （`ICS_Generate-msvc-x64.exe`、`ICS_Generate-msvc-x86.exe`、`ICS_Generate-mingw-x64.exe`）及各自的体积报告。

## 从源码构建

依赖：CMake ≥ 3.20；MSVC（Visual Studio 2022，含 C++ 工具集）或 MinGW-w64（GCC）。

### MinGW-w64（本仓库开发机使用的方式）

```bat
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw -j
```

或直接使用一键脚本（自动探测生成器、构建、跑测试、校验体积与导入表）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -BuildDir build-mingw -Generator "MinGW Makefiles"
```

产物：`build-mingw\bin\ICS_Generate.exe` 与 `build-mingw\bin\ics_core_tests.exe`。

### MSVC（Visual Studio 2022）

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc --config Release
```

x86 构建把 `-A x64` 换成 `-A Win32` 即可。

### 关键构建开关

- `ICSG_BUILD_TESTS`（默认 ON）：是否构建 `ics_core_tests` 单元测试。
- `ICSG_SIZE_GATE_KB`（默认 96）：链接后体积硬门槛（KB），超限构建失败。
- `ICSG_SIZE_TARGET_KB`（默认 64）：信息性体积目标。

MSVC 路径使用 `/NODEFAULTLIB /ENTRY:AppEntry /GS- /GR- /EHs-c- /utf-8 /O1 /Gy /Gw /GL /LTCG /OPT:REF /OPT:ICF`
并只链接白名单系统库；MinGW 路径使用
`-nostdlib -nostartfiles -nodefaultlibs -Wl,-e,AppEntry -Wl,--gc-sections -Os -s -fno-exceptions -fno-rtti -fno-stack-protector -ffunction-sections -fdata-sections`。
两条路径都在一处定义（`CMakeLists.txt`），保证 MSVC 正确性与 MinGW 实测体积一致（本仓库没有本地 MSVC，MSVC 路径由 CI 验证）。

## 运行单元测试

```
build-mingw\bin\ics_core_tests.exe
```

覆盖：RFC 5545 转义/反转义、75 字节折行（含多字节 UTF-8）、CRLF、DATE / DATE-TIME / UTC 往返、
RRULE 文本、TRIGGER 时长、UID 唯一性、UTF-8 编解码边界（代理对、4 字节、过长序列、截断）、
完整"保存-加载-逐字节比较"往返、对脏日历的容错解析。退出码 0 即全部通过；CI 中每个作业都会运行它。

---

## English

**ICS Generate** is a self-contained Windows GUI application for creating, editing,
opening and saving `.ics` (iCalendar, RFC 5545) calendar files. It is a single
executable built with pure Win32 (User32 + GDI + Common Controls v6, DWM/UxTheme
frame, Shell/Comdlg32 dialogs) and **no C runtime at all** (custom `AppEntry`
entry point, own memset/memcpy/memmove/memcmp). It imports only the whitelisted
Windows system DLLs — no redistributable, no runtime install, tens of KB in size
(product target ≤ 64 KB, CI hard gate ≤ 96 KB).

Features: create/edit/delete/duplicate/reorder events; merge import; export the
selected event; a fully user-selectable timezone (every Windows time zone via
`EnumDynamicTimeZoneInformation` plus hand-typed IANA TZIDs such as
`Asia/Shanghai`, with VTIMEZONE generated from the Windows timezone database);
location, multiline notes, URL, categories, organizer and attendees, status,
priority, up to 4 reminders, and RRULE recurrence with BYDAY/BYMONTHDAY/COUNT/
UNTIL/EXDATE/RDATE; a filter box; column sorting; drag & drop of .ics files;
dark-mode aware frame; per-monitor DPI awareness; and opening files from the
command line.

Build (MinGW-w64):

```bat
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw -j
```

Build (MSVC): `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64` then
`cmake --build build-msvc --config Release` (use `-A Win32` for x86).
Run `bin\ics_core_tests.exe` for the unit tests and `scripts\build.ps1` for a
one-command build that also verifies the size gate and the CRT-free import table.
CI builds msvc-x64, msvc-x86 and mingw-x64 on every push/PR, and a `v*` tag
publishes a GitHub Release with all executables attached.

### Why not WinUI 3 / Windows App SDK?

WinUI 3 requires the **Windows App SDK runtime** — either an installer/MSIX
deployment or a self-contained deployment that ships dozens of megabytes of
runtime DLLs alongside (or inside) the app. That directly conflicts with this
project's core goal: a **zero-dependency, tens-of-KB exe** you can copy to any
Windows 10/11 machine and just run. Pure Win32 + Common Controls v6 (with the v6
manifest), a DWM dark-mode frame and per-monitor DPI awareness deliver a modern,
native-feeling application in tens of kilobytes with literally nothing to
install — which is why this project stays on plain Win32.

## 许可证 / License

[MIT](LICENSE) — Copyright (c) 2026 sliverwolf233
