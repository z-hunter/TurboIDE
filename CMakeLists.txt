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

option(TURBOIDE_BUNDLE_MINGW_RUNTIME "Copy MinGW runtime DLLs beside built executables" ON)
option(TURBOIDE_BUNDLE_CTAGS "Copy pinned Universal Ctags into a portable output" OFF)
option(TURBOIDE_ALLOW_EXTERNAL_CTAGS "Allow TURBOIDE_CTAGS in developer builds" ON)
set(TURBOIDE_CTAGS_ARCHIVE "" CACHE FILEPATH "Verified Universal Ctags x64 ZIP")
set(TURBOIDE_CTAGS_SHA256 "f1d027bebb64c20081357d83815d8c7601bd3f4499d38ba050e806e9e360dddf"
    CACHE STRING "Expected SHA-256 of the pinned Universal Ctags ZIP")
if(TURBOIDE_BUNDLE_CTAGS)
    if(NOT EXISTS "${TURBOIDE_CTAGS_ARCHIVE}")
        message(FATAL_ERROR "TURBOIDE_BUNDLE_CTAGS requires TURBOIDE_CTAGS_ARCHIVE. Run tools/fetch_ctags.ps1 first.")
    endif()
    file(SHA256 "${TURBOIDE_CTAGS_ARCHIVE}" _ctags_archive_hash)
    if(NOT _ctags_archive_hash STREQUAL TURBOIDE_CTAGS_SHA256)
        message(FATAL_ERROR "Universal Ctags archive checksum does not match the pinned manifest.")
    endif()
    set(_ctags_extract_dir "${CMAKE_BINARY_DIR}/_deps/turboide-ctags")
    file(REMOVE_RECURSE "${_ctags_extract_dir}")
    file(MAKE_DIRECTORY "${_ctags_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${TURBOIDE_CTAGS_ARCHIVE}" DESTINATION "${_ctags_extract_dir}")
    if(NOT EXISTS "${_ctags_extract_dir}/ctags.exe" OR NOT EXISTS "${_ctags_extract_dir}/license")
        message(FATAL_ERROR "Universal Ctags archive has an unexpected layout.")
    endif()
    set(TURBOIDE_ALLOW_EXTERNAL_CTAGS OFF CACHE BOOL "Allow TURBOIDE_CTAGS in developer builds" FORCE)
endif()
if(MINGW AND TURBOIDE_BUNDLE_MINGW_RUNTIME)
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB _mingw_runtime_dlls "${_mingw_bin}/libgcc_s_*-1.dll"
         "${_mingw_bin}/libstdc++-6.dll" "${_mingw_bin}/libwinpthread-1.dll")
    function(copy_mingw_runtime target)
        foreach(_dll IN LISTS _mingw_runtime_dlls)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endforeach()
    endfunction()
endif()

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

# Let SyntaxEditor record every buffer mutation and mark its save point.
set(_tv_editor_header "${tvision_SOURCE_DIR}/include/tvision/editors.h")
set(_tv_editor_source "${tvision_SOURCE_DIR}/source/tvision/teditor2.cpp")
file(READ "${_tv_editor_header}" _tv_undo_header_text)
file(READ "${_tv_editor_source}" _tv_undo_editor_text)
set(_tv_insert_header_old [=[    Boolean insertBuffer( const char *, uint, uint, Boolean, Boolean );]=])
set(_tv_insert_header_new [=[    virtual Boolean insertBuffer( const char *, uint, uint, Boolean, Boolean, Boolean raw = False );]=])
set(_tv_insert_source_old [=[Boolean TEditor::insertBuffer( const char *p,
                               uint offset,
                               uint length,
                               Boolean allowUndo,
                               Boolean selectText
                             )]=])
set(_tv_insert_source_new [=[Boolean TEditor::insertBuffer( const char *p,
                               uint offset,
                               uint length,
                               Boolean allowUndo,
                               Boolean selectText,
                               Boolean raw
                             )]=])
set(_tv_insert_length_old [=[    uint insLen = lengthWithConvertedLineEndings( p, length );]=])
set(_tv_insert_length_new [=[    uint insLen = raw ? length : lengthWithConvertedLineEndings( p, length );]=])
set(_tv_insert_copy_old [=[    if( length > 0 )
        copyAndConvertLineEndings( &buffer[curPtr], &p[offset], length );]=])
set(_tv_insert_copy_new [=[    if( length > 0 )
        if( raw )
            memcpy( &buffer[curPtr], &p[offset], length );
        else
            copyAndConvertLineEndings( &buffer[curPtr], &p[offset], length );]=])
set(_tv_save_header_old [=[    Boolean saveFile() noexcept;]=])
set(_tv_save_header_new [=[    virtual Boolean saveFile() noexcept;]=])
foreach(_tv_undo_header_pair insert_header save_header)
    set(_tv_old "${_tv_${_tv_undo_header_pair}_old}")
    set(_tv_new "${_tv_${_tv_undo_header_pair}_new}")
    string(FIND "${_tv_undo_header_text}" "${_tv_new}" _tv_patched)
    if(_tv_patched EQUAL -1)
        string(REPLACE "${_tv_old}" "${_tv_new}" _tv_undo_header_text "${_tv_undo_header_text}")
    endif()
endforeach()
foreach(_tv_undo_source_pair insert_source insert_length insert_copy)
    set(_tv_old "${_tv_${_tv_undo_source_pair}_old}")
    set(_tv_new "${_tv_${_tv_undo_source_pair}_new}")
    string(FIND "${_tv_undo_editor_text}" "${_tv_new}" _tv_patched)
    if(_tv_patched EQUAL -1)
        string(REPLACE "${_tv_old}" "${_tv_new}" _tv_undo_editor_text "${_tv_undo_editor_text}")
    endif()
endforeach()
string(FIND "${_tv_undo_header_text}" "${_tv_insert_header_new}" _tv_insert_header_found)
string(FIND "${_tv_undo_header_text}" "${_tv_save_header_new}" _tv_save_header_found)
string(FIND "${_tv_undo_editor_text}" "${_tv_insert_source_new}" _tv_insert_source_found)
string(FIND "${_tv_undo_editor_text}" "${_tv_insert_length_new}" _tv_insert_length_found)
string(FIND "${_tv_undo_editor_text}" "${_tv_insert_copy_new}" _tv_insert_copy_found)
if(_tv_insert_header_found EQUAL -1 OR _tv_save_header_found EQUAL -1 OR
   _tv_insert_source_found EQUAL -1 OR _tv_insert_length_found EQUAL -1 OR _tv_insert_copy_found EQUAL -1)
    message(FATAL_ERROR "Turbo Vision undo extension patch did not match the pinned source.")
endif()
file(WRITE "${_tv_editor_header}" "${_tv_undo_header_text}")
file(WRITE "${_tv_editor_source}" "${_tv_undo_editor_text}")

add_executable(turboide_tvhc "${tvision_SOURCE_DIR}/examples/tvhc/tvhc.cpp")
target_link_libraries(turboide_tvhc PRIVATE tvision)
target_include_directories(turboide_tvhc PRIVATE "${tvision_SOURCE_DIR}/include/tvision/compat/borland")
target_compile_features(turboide_tvhc PRIVATE cxx_std_17)

set(TURBOIDE_HELP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/docs/help/turboide.txt")
set(TURBOIDE_NATIVE_HELP_DATABASE "${CMAKE_CURRENT_BINARY_DIR}/turboide.h32")
set(TURBOIDE_HELP_DATABASE "${CMAKE_CURRENT_BINARY_DIR}/tchelp.h32")
set(TURBOIDE_HELP_CONTEXTS "${CMAKE_CURRENT_BINARY_DIR}/turboide_help_contexts.h")
add_custom_command(
    OUTPUT "${TURBOIDE_NATIVE_HELP_DATABASE}" "${TURBOIDE_HELP_CONTEXTS}"
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "${TURBOIDE_NATIVE_HELP_DATABASE}" "${TURBOIDE_HELP_CONTEXTS}"
    COMMAND $<TARGET_FILE:turboide_tvhc>
            "${TURBOIDE_HELP_SOURCE}" "${TURBOIDE_NATIVE_HELP_DATABASE}" "${TURBOIDE_HELP_CONTEXTS}"
    DEPENDS turboide_tvhc "${TURBOIDE_HELP_SOURCE}"
    COMMENT "Compiling TurboIDE help database"
    VERBATIM
)
add_executable(turboide_help_merge tools/merge_help.cpp)
target_link_libraries(turboide_help_merge PRIVATE tvision)
target_compile_features(turboide_help_merge PRIVATE cxx_std_17)
add_custom_command(
    OUTPUT "${TURBOIDE_HELP_DATABASE}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${TURBOIDE_HELP_DATABASE}"
    COMMAND $<TARGET_FILE:turboide_help_merge>
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/TCHELP.h32"
            "${TURBOIDE_NATIVE_HELP_DATABASE}" "${TURBOIDE_HELP_DATABASE}"
    DEPENDS turboide_help_merge "${CMAKE_CURRENT_SOURCE_DIR}/reference/TCHELP.h32"
            "${TURBOIDE_NATIVE_HELP_DATABASE}"
    COMMENT "Merging TurboIDE topics into the legacy help database"
    VERBATIM
)
add_custom_target(turboide_help DEPENDS "${TURBOIDE_HELP_DATABASE}")

add_executable(turboide src/main.cpp src/build.cpp src/project.cpp src/run.cpp src/debugger.cpp src/syntax.cpp src/editor_features.cpp src/settings.cpp src/desktop_session.cpp src/legacy_help.cpp src/symbol_index.cpp)
add_dependencies(turboide turboide_help)
target_compile_features(turboide PRIVATE cxx_std_17)
target_link_libraries(turboide PRIVATE tvision)
if(TURBOIDE_ALLOW_EXTERNAL_CTAGS)
    target_compile_definitions(turboide PRIVATE TURBOIDE_ALLOW_EXTERNAL_CTAGS=1)
else()
    target_compile_definitions(turboide PRIVATE TURBOIDE_ALLOW_EXTERNAL_CTAGS=0)
endif()
if(MINGW AND TURBOIDE_BUNDLE_MINGW_RUNTIME)
    copy_mingw_runtime(turboide)
endif()
add_custom_command(TARGET turboide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:turboide>/conio"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/added"
            "$<TARGET_FILE_DIR:turboide>/conio"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:turboide>/cfgfiles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/SetsEd/cfgfiles/pmacros.pmc"
            "$<TARGET_FILE_DIR:turboide>/cfgfiles/pmacros.pmc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/reference/SetsEd/cfgfiles/cpmacros.pmc"
            "$<TARGET_FILE_DIR:turboide>/cfgfiles/cpmacros.pmc"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:turboide>/help"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TURBOIDE_HELP_DATABASE}"
            "$<TARGET_FILE_DIR:turboide>/help/tchelp.h32"
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:turboide>/help/turboide.h32"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:turboide>/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_FILE_DIR:turboide>/THIRD_PARTY_NOTICES.md"
    VERBATIM
)
if(TURBOIDE_BUNDLE_CTAGS)
    add_custom_command(TARGET turboide POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:turboide>/tools/ctags"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ctags_extract_dir}/ctags.exe" "$<TARGET_FILE_DIR:turboide>/tools/ctags/ctags.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_ctags_extract_dir}/license" "$<TARGET_FILE_DIR:turboide>/tools/ctags/license"
        VERBATIM
    )
endif()
if(MSVC)
    target_compile_options(turboide PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC 16 enables char8_t as an extension in C++17 mode; TurboIDE stores UTF-8 as char strings.
    target_compile_options(turboide PRIVATE -fno-char8_t)
endif()

option(TURBOIDE_BUILD_HELP_TOOLS "Build optional legacy help conversion tools" OFF)
if(TURBOIDE_BUILD_HELP_TOOLS)
    add_executable(tch_to_tvhc tools/tch_to_tvhc.cpp)
    target_compile_features(tch_to_tvhc PRIVATE cxx_std_17)

    add_executable(help_database_check tests/help_database_check.cpp src/legacy_help.cpp)
    target_link_libraries(help_database_check PRIVATE tvision)
    target_compile_features(help_database_check PRIVATE cxx_std_17)
endif()

option(TURBOIDE_BUILD_TESTS "Build TurboIDE integration tests" OFF)
if(TURBOIDE_BUILD_TESTS)
    add_executable(turboide_help_database_check tests/turboide_help_database_check.cpp)
    add_dependencies(turboide_help_database_check turboide_help)
    target_link_libraries(turboide_help_database_check PRIVATE tvision)
    target_compile_features(turboide_help_database_check PRIVATE cxx_std_17)
    target_compile_definitions(turboide_help_database_check PRIVATE
        TURBOIDE_HELP_DATABASE="${TURBOIDE_HELP_DATABASE}")
    add_executable(run_interrupt_test tests/run_interrupt_test.cpp src/run.cpp)
    target_compile_features(run_interrupt_test PRIVATE cxx_std_17)
    add_executable(gdb_mi_self_check tests/gdb_mi_self_check.cpp)
    target_compile_features(gdb_mi_self_check PRIVATE cxx_std_17)
    add_executable(toolchain_environment_test tests/toolchain_environment_test.cpp)
    target_compile_features(toolchain_environment_test PRIVATE cxx_std_17)
    add_executable(fake_build_compiler tests/fake_build_compiler.cpp)
    target_compile_features(fake_build_compiler PRIVATE cxx_std_17)
    if(MINGW)
        target_link_options(fake_build_compiler PRIVATE -municode)
    endif()
    add_executable(build_stream_test tests/build_stream_test.cpp src/build.cpp)
    target_compile_features(build_stream_test PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(build_stream_test PRIVATE -fno-char8_t)
    endif()
    if(MINGW AND TURBOIDE_BUNDLE_MINGW_RUNTIME)
        copy_mingw_runtime(fake_build_compiler)
        copy_mingw_runtime(build_stream_test)
    endif()
    add_dependencies(build_stream_test fake_build_compiler)
    add_executable(editor_selection_test tests/editor_selection_test.cpp)
    target_compile_features(editor_selection_test PRIVATE cxx_std_17)
    target_link_libraries(editor_selection_test PRIVATE tvision)
    add_executable(editor_undo_test tests/editor_undo_test.cpp)
    target_compile_features(editor_undo_test PRIVATE cxx_std_17)
    target_link_libraries(editor_undo_test PRIVATE tvision)
    add_executable(desktop_session_test tests/desktop_session_test.cpp src/desktop_session.cpp)
    target_compile_features(desktop_session_test PRIVATE cxx_std_17)
    add_executable(symbol_index_test tests/symbol_index_test.cpp src/symbol_index.cpp)
    target_compile_features(symbol_index_test PRIVATE cxx_std_17)
    target_compile_definitions(symbol_index_test PRIVATE TURBOIDE_ALLOW_EXTERNAL_CTAGS=1)
endif()
