cmake_minimum_required(VERSION 3.20)
project(TurboIDE LANGUAGES CXX)

set(TV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(tvision
    GIT_REPOSITORY https://github.com/magiblot/tvision.git
    GIT_TAG b4831e2ca16652db327fb7cc964f1c8c2d512524
)
FetchContent_MakeAvailable(tvision)

# Turbo Vision's private active buffer must permit CONOUT$ to reopen it so Run
# can restore the IDE after switching to the user's screen buffer.
set(_tv_console_source "${tvision_SOURCE_DIR}/source/platform/conctl.cpp")
file(READ "${_tv_console_source}" _tv_console_text)
set(_tv_create_old [=[cn[activeOutput].handle = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        0,]=])
set(_tv_create_new [=[cn[activeOutput].handle = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,]=])
string(FIND "${_tv_console_text}" "${_tv_create_new}" _tv_already_patched)
if(_tv_already_patched EQUAL -1)
    string(REPLACE "${_tv_create_old}" "${_tv_create_new}" _tv_console_patched "${_tv_console_text}")
    if(_tv_console_patched STREQUAL _tv_console_text)
        string(REPLACE "\n" "\r\n" _tv_create_old_crlf "${_tv_create_old}")
        string(REPLACE "\n" "\r\n" _tv_create_new_crlf "${_tv_create_new}")
        string(REPLACE "${_tv_create_old_crlf}" "${_tv_create_new_crlf}"
               _tv_console_patched "${_tv_console_text}")
    endif()
    if(_tv_console_patched STREQUAL _tv_console_text)
        message(FATAL_ERROR "Turbo Vision console buffer patch did not match the pinned source.")
    endif()
    file(WRITE "${_tv_console_source}" "${_tv_console_patched}")
endif()

# Make the editor tab width configurable by TurboIDE's Environment dialog.
set(_tv_editor_header "${tvision_SOURCE_DIR}/include/tvision/editors.h")
set(_tv_editor_static_source "${tvision_SOURCE_DIR}/source/tvision/editstat.cpp")
file(READ "${_tv_editor_header}" _tv_editor_header_text)
file(READ "${_tv_editor_static_source}" _tv_editor_static_text)
set(_tv_tabs_header_old [=[    static ushort _NEAR editorFlags;]=])
set(_tv_tabs_header_new [=[    static ushort _NEAR editorFlags;
    static int tabSize;]=])
set(_tv_tabs_source_old [=[ushort _NEAR TEditor::editorFlags = efBackupFiles | efPromptOnReplace;]=])
set(_tv_tabs_source_new [=[ushort _NEAR TEditor::editorFlags = efBackupFiles | efPromptOnReplace;
int TEditor::tabSize = 8;]=])
string(FIND "${_tv_editor_header_text}" "${_tv_tabs_header_new}" _tv_tabs_header_patched)
if(_tv_tabs_header_patched EQUAL -1)
    string(FIND "${_tv_editor_header_text}" "${_tv_tabs_header_old}" _tv_tabs_header_found)
    if(_tv_tabs_header_found EQUAL -1)
        message(FATAL_ERROR "Turbo Vision tab-size declaration patch did not match the pinned source.")
    endif()
    string(REPLACE "${_tv_tabs_header_old}" "${_tv_tabs_header_new}" _tv_editor_header_text "${_tv_editor_header_text}")
    file(WRITE "${_tv_editor_header}" "${_tv_editor_header_text}")
endif()
string(FIND "${_tv_editor_static_text}" "${_tv_tabs_source_new}" _tv_tabs_source_patched)
if(_tv_tabs_source_patched EQUAL -1)
    string(FIND "${_tv_editor_static_text}" "${_tv_tabs_source_old}" _tv_tabs_source_found)
    if(_tv_tabs_source_found EQUAL -1)
        message(FATAL_ERROR "Turbo Vision tab-size definition patch did not match the pinned source.")
    endif()
    string(REPLACE "${_tv_tabs_source_old}" "${_tv_tabs_source_new}" _tv_editor_static_text "${_tv_editor_static_text}")
    file(WRITE "${_tv_editor_static_source}" "${_tv_editor_static_text}")
endif()

set(_tv_editor_navigation_source "${tvision_SOURCE_DIR}/source/tvision/teditor1.cpp")
file(READ "${_tv_editor_navigation_source}" _tv_editor_navigation_text)
set(_tv_tabs_math_old [=[                pos = (pos | 7) + 1;]=])
set(_tv_tabs_math_new [=[                pos = (pos / tabSize + 1) * tabSize;]=])
string(FIND "${_tv_editor_navigation_text}" "${_tv_tabs_math_new}" _tv_tabs_math_patched)
if(_tv_tabs_math_patched EQUAL -1)
    string(FIND "${_tv_editor_navigation_text}" "${_tv_tabs_math_old}" _tv_tabs_math_found)
    if(_tv_tabs_math_found EQUAL -1)
        message(FATAL_ERROR "Turbo Vision tab-size calculation patch did not match the pinned source.")
    endif()
    string(REPLACE "${_tv_tabs_math_old}" "${_tv_tabs_math_new}" _tv_editor_navigation_text "${_tv_editor_navigation_text}")
    file(WRITE "${_tv_editor_navigation_source}" "${_tv_editor_navigation_text}")
endif()

# Let TEditor subclasses apply syntax attributes while Turbo Vision redraws a line.
set(_tv_editor_header "${tvision_SOURCE_DIR}/include/tvision/editors.h")
file(READ "${_tv_editor_header}" _tv_editor_text)
set(_tv_format_old [=[    void formatLine( TDrawBuffer &, uint, int, int, TAttrPair );]=])
set(_tv_format_new [=[    virtual void formatLine( TDrawBuffer &, uint, int, int, TAttrPair );]=])
string(FIND "${_tv_editor_text}" "${_tv_format_new}" _tv_format_already_patched)
if(_tv_format_already_patched EQUAL -1)
    string(REPLACE "${_tv_format_old}" "${_tv_format_new}" _tv_editor_patched "${_tv_editor_text}")
    if(_tv_editor_patched STREQUAL _tv_editor_text)
        string(REPLACE "\n" "\r\n" _tv_format_old_crlf "${_tv_format_old}")
        string(REPLACE "\n" "\r\n" _tv_format_new_crlf "${_tv_format_new}")
        string(REPLACE "${_tv_format_old_crlf}" "${_tv_format_new_crlf}"
               _tv_editor_patched "${_tv_editor_text}")
    endif()
    if(_tv_editor_patched STREQUAL _tv_editor_text)
        message(FATAL_ERROR "Turbo Vision editor formatting patch did not match the pinned source.")
    endif()
    file(WRITE "${_tv_editor_header}" "${_tv_editor_patched}")
endif()

add_executable(turboide src/main.cpp src/build.cpp src/project.cpp src/run.cpp src/debugger.cpp src/syntax.cpp src/editor_features.cpp src/settings.cpp src/desktop_session.cpp)
target_compile_features(turboide PRIVATE cxx_std_17)
target_link_libraries(turboide PRIVATE tvision)
add_custom_command(TARGET turboide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:turboide>/cfgfiles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/SetsEd/cfgfiles/pmacros.pmc"
            "$<TARGET_FILE_DIR:turboide>/cfgfiles/pmacros.pmc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/SetsEd/cfgfiles/cpmacros.pmc"
            "$<TARGET_FILE_DIR:turboide>/cfgfiles/cpmacros.pmc"
    VERBATIM
)
if(MSVC)
    target_compile_options(turboide PRIVATE /utf-8)
endif()

option(TURBOIDE_BUILD_TESTS "Build TurboIDE integration tests" OFF)
if(TURBOIDE_BUILD_TESTS)
    add_executable(run_interrupt_test tests/run_interrupt_test.cpp src/run.cpp)
    target_compile_features(run_interrupt_test PRIVATE cxx_std_17)
    add_executable(gdb_mi_self_check tests/gdb_mi_self_check.cpp)
    target_compile_features(gdb_mi_self_check PRIVATE cxx_std_17)
endif()
