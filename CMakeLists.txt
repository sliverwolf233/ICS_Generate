# ============================================================================
#  ICS_Generate - CRT-free Win32 iCalendar (.ics) editor
#
#  Build interface frozen by docs/CONTRACT.md section 4:
#    icsg_core       OBJECT library   src/core/*.cpp                (tiny flags)
#    ICS_Generate    WIN32 executable src/app/*.cpp + icsg_core + src/app/app.rc
#                                     -> ${CMAKE_BINARY_DIR}/bin/ICS_Generate.exe
#    ics_core_tests  console exe      tests/test_core.cpp + icsg_core
#                                     -> bin/ics_core_tests.exe (ICSG_BUILD_TESTS)
#
#  The application image must not import the C runtime at all, so all the
#  size/no-CRT flags live in exactly one place below and both the object
#  library and the executable are compiled with them.
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(ICS_Generate
        VERSION 1.0.0
        DESCRIPTION "CRT-free Win32 iCalendar (.ics) editor"
        LANGUAGES CXX RC)

# Single-config generators (MinGW Makefiles, Ninja, NMake) use CMAKE_BUILD_TYPE.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ICSG_BUILD_TESTS "Build the ics_core_tests console runner" ON)
set(ICSG_SIZE_GATE_KB "96" CACHE STRING "Hard maximum size of ICS_Generate.exe in KB (CI gate)")
set(ICSG_SIZE_TARGET_KB "64" CACHE STRING "Informational size target of ICS_Generate.exe in KB")

if(NOT MSVC AND NOT MINGW)
    message(FATAL_ERROR
        "ICS_Generate supports MSVC (Visual Studio generator) and MinGW-w64 only. "
        "Both paths rely on link flags that drop the C runtime (/NODEFAULTLIB, "
        "-nostdlib -nostartfiles -nodefaultlibs). See README.md.")
endif()

# ---------------------------------------------------------------------------
#  One place for every size-critical flag.
# ---------------------------------------------------------------------------
# --- MSVC: why each flag is here (all of them exist to shrink the image) ----
#   /utf-8                      sources are UTF-8 without BOM; wide Chinese
#                               literals only survive with this switch
#   /std:c++17                  the contract's language level
#   /O1                         optimise for size, not speed
#   /GS-                        no stack cookie: drops __security_cookie,
#                               __security_check_cookie and __chkstk helpers
#   /GR-                        no RTTI: drops type descriptors and vtables
#                               metadata
#   /EHs-c-                     no C++ exceptions: drops unwind tables, frame
#                               handlers and _CxxThrowException
#   /D_HAS_EXCEPTIONS=0         keeps any surviving CRT/STL header honest
#                               (they are forbidden in this code base anyway)
#   /Gy                         function level linking: every function becomes a
#                               COMDAT the linker can drop with /OPT:REF
#   /Gw                         whole program data optimisation: unused data
#                               is dropped as well
#   /GL                         whole program optimisation, consumed by /LTCG
#   /DNDEBUG                    no assert machinery
#   /W4                         contract 2.7: the code must stay warning clean
set(ICSG_TINY_COMPILE_MSVC
    /utf-8 /std:c++17 /O1 /GS- /GR- /EHs-c- /D_HAS_EXCEPTIONS=0
    /Gy /Gw /GL /DNDEBUG /W4)

#   /LTCG                       whole program optimisation (reads the /GL objects)
#   /OPT:REF                    remove every unreferenced function and data item
#   /OPT:ICF                    fold identical COMDATs together
#   /INCREMENTAL:NO             no incremental padding, no jump thunks
#   /NODEFAULTLIB               never pull in libcmt/msvcrt/ucrt - the image is
#                               CRT-free by construction
#   /ENTRY:AppEntry             entry point supplied by src/app/crt_entry.cpp
#                               (plus its own memset/memcpy/memmove/memcmp)
#   /SUBSYSTEM:WINDOWS          GUI image, no console window
# The default stack reserve/commit is kept: the app must stay usable.
set(ICSG_TINY_LINK_MSVC
    /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /NODEFAULTLIB /ENTRY:AppEntry
    /SUBSYSTEM:WINDOWS)

# --- MinGW/GCC: why each flag is here ---------------------------------------
#   -std=c++17                  the contract's language level
#   -Os                         optimise for size
#   -fno-exceptions             no unwind tables, no __cxa_throw machinery
#   -fno-rtti                   no typeinfo/vtable metadata
#   -fno-stack-protector        no __stack_chk_fail / canary code
#   -fno-asynchronous-unwind-tables  removes .eh_frame entirely
#   -fno-ident                  removes the .comment section (GCC version)
#   -ffunction-sections -fdata-sections  one section per symbol so that
#                               --gc-sections can discard the unused ones
#   -DNDEBUG                    no assert machinery
#   -Wall -Wextra               contract 2.7: the code must stay warning clean
set(ICSG_TINY_COMPILE_GCC
    -std=c++17 -Os -fno-exceptions -fno-rtti -fno-stack-protector
    -fno-asynchronous-unwind-tables -fno-ident
    -ffunction-sections -fdata-sections -DNDEBUG -Wall -Wextra)

#   -nostdlib -nostartfiles -nodefaultlibs
#                               nothing from libmingw32/libgcc/the CRT start
#                               files may end up in the image
#   -Wl,-e,AppEntry             entry point supplied by src/app/crt_entry.cpp
#   -Wl,--gc-sections           drop every section nothing references
#   -s                          strip the symbol table
# (-mwindows / --subsystem,windows is added by CMake for a WIN32 executable.)
set(ICSG_TINY_LINK_GCC
    -nostdlib -nostartfiles -nodefaultlibs
    -Wl,-e,AppEntry -Wl,--gc-sections -s)

# Frozen import whitelist (docs/CONTRACT.md section 2.5).
set(ICSG_SYSTEM_LIBS
    kernel32 user32 gdi32 comctl32 shell32 shlwapi
    ole32 comdlg32 advapi32 dwmapi uxtheme)

# ---------------------------------------------------------------------------
#  Sources + configure time sanity checks
# ---------------------------------------------------------------------------
file(GLOB ICSG_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB ICSG_APP_SOURCES  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp")

set(ICSG_RC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/app/app.rc")
set(ICSG_TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_core.cpp")
set(ICSG_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest")

if(NOT ICSG_CORE_SOURCES)
    message(FATAL_ERROR "No engine sources found: src/core/*.cpp is missing or empty.")
endif()
if(NOT ICSG_APP_SOURCES)
    message(FATAL_ERROR "No UI sources found: src/app/*.cpp is missing or empty.")
endif()
if(NOT EXISTS "${ICSG_RC_SOURCES}")
    message(FATAL_ERROR "Resource script missing: ${ICSG_RC_SOURCES} (icon + VERSIONINFO).")
endif()
if(ICSG_BUILD_TESTS AND NOT EXISTS "${ICSG_TEST_SOURCE}")
    message(FATAL_ERROR "Unit test source missing: ${ICSG_TEST_SOURCE} (or configure with -DICSG_BUILD_TESTS=OFF).")
endif()

# Common Controls v6 / DPI awareness manifest. res/app.manifest is a frozen
# input owned by the Lead: embed it as an RT_MANIFEST resource unless the
# resource script already references it (then app.rc wins and nothing is
# duplicated in the image).
if(EXISTS "${ICSG_MANIFEST}")
    file(READ "${ICSG_RC_SOURCES}" ICSG_RC_TEXT)
    string(FIND "${ICSG_RC_TEXT}" "app.manifest" ICSG_RC_MANIFEST_POS)
    if(ICSG_RC_MANIFEST_POS EQUAL -1)
        set(ICSG_GENERATED_RC "${CMAKE_BINARY_DIR}/generated/icsg_manifest.rc")
        file(WRITE "${ICSG_GENERATED_RC}"
             "// Generated by CMake: embeds res/app.manifest as RT_MANIFEST (id 1).\r\n"
             "1 24 \"${ICSG_MANIFEST}\"\r\n")
        list(APPEND ICSG_RC_SOURCES "${ICSG_GENERATED_RC}")
        message(STATUS "ICS_Generate: res/app.manifest will be embedded by a generated .rc")
    else()
        message(STATUS "ICS_Generate: src/app/app.rc already references res/app.manifest")
    endif()
endif()

# ---------------------------------------------------------------------------
#  Flags plumbing
# ---------------------------------------------------------------------------
# Binaries always land in <build>/bin, whatever the generator and config.
function(icsg_set_output_dir target)
    set(_dir "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_dir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_dir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${_dir}"
        PDB_OUTPUT_DIRECTORY "${_dir}"
        PDB_OUTPUT_DIRECTORY_DEBUG "${_dir}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${_dir}"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_dir}"
        PDB_OUTPUT_DIRECTORY_MINSIZEREL "${_dir}")
endfunction()

# Applies the tiny compile+link flag set and the exact import whitelist.
function(icsg_apply_tiny_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE ${ICSG_TINY_COMPILE_MSVC})
        target_link_options(${target} PRIVATE ${ICSG_TINY_LINK_MSVC})
    else()
        target_compile_options(${target} PRIVATE ${ICSG_TINY_COMPILE_GCC})
        target_link_options(${target} PRIVATE ${ICSG_TINY_LINK_GCC})
    endif()
    target_link_libraries(${target} PRIVATE ${ICSG_SYSTEM_LIBS})
endfunction()

# ---------------------------------------------------------------------------
#  icsg_core - the engine, compiled with the tiny flags
# ---------------------------------------------------------------------------
add_library(icsg_core OBJECT ${ICSG_CORE_SOURCES})
target_include_directories(icsg_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
icsg_apply_tiny_flags(icsg_core)

# ---------------------------------------------------------------------------
#  ICS_Generate - the application
# ---------------------------------------------------------------------------
add_executable(ICS_Generate WIN32 ${ICSG_APP_SOURCES} ${ICSG_RC_SOURCES} $<TARGET_OBJECTS:icsg_core>)
target_include_directories(ICS_Generate PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/res")
icsg_apply_tiny_flags(ICS_Generate)
icsg_set_output_dir(ICS_Generate)

# Size report + hard size gate, printed after every successful link.
add_custom_command(TARGET ICS_Generate POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DICSG_EXE=$<TARGET_FILE:ICS_Generate>"
            "-DICSG_SIZE_GATE_KB=${ICSG_SIZE_GATE_KB}"
            "-DICSG_SIZE_TARGET_KB=${ICSG_SIZE_TARGET_KB}"
            "-DICSG_SIZE_TXT=${CMAKE_BINARY_DIR}/bin/size.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/report_size.cmake"
    COMMENT "ICS_Generate: size report"
    VERBATIM)

# ---------------------------------------------------------------------------
#  ics_core_tests - plain CRT console runner, never size gated
# ---------------------------------------------------------------------------
if(ICSG_BUILD_TESTS)
    enable_testing()
    add_executable(ics_core_tests ${ICSG_TEST_SOURCE} $<TARGET_OBJECTS:icsg_core>)
    target_include_directories(ics_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
    if(MSVC)
        # A static CRT keeps the test runner self-contained on the CI image.
        target_compile_options(ics_core_tests PRIVATE /utf-8 /std:c++17 /W4 /DNDEBUG)
        set_property(TARGET ics_core_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    else()
        # -static keeps the runner independent of the MinGW runtime DLLs.
        target_compile_options(ics_core_tests PRIVATE -std=c++17 -O2 -Wall -Wextra -DNDEBUG)
        target_link_options(ics_core_tests PRIVATE -static)
    endif()
    target_link_libraries(ics_core_tests PRIVATE ${ICSG_SYSTEM_LIBS})
    icsg_set_output_dir(ics_core_tests)
    add_test(NAME ics_core_tests COMMAND ics_core_tests)
endif()

# ---------------------------------------------------------------------------
#  Configure summary
# ---------------------------------------------------------------------------
message(STATUS "ICS_Generate ${PROJECT_VERSION} | compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build type        : ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  engine sources    : ${ICSG_CORE_SOURCES}")
message(STATUS "  app sources       : ${ICSG_APP_SOURCES}")
message(STATUS "  unit tests        : ${ICSG_BUILD_TESTS}")
message(STATUS "  size gate         : ${ICSG_SIZE_GATE_KB} KB (target ${ICSG_SIZE_TARGET_KB} KB)")
