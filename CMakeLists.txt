cmake_minimum_required(VERSION 3.21)
project(Hydra64 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---- Memory checking: ./build.sh asan (build-asan/ folder) builds everything with ASan + UBSan,
# for the readers of binary files (.ctg, .hsh/.elm, .cdp, .hy64*) and the tests.
option(HYDRA64_SANITIZE "Compiler avec AddressSanitizer et UndefinedBehaviorSanitizer" OFF)
if(HYDRA64_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(MINGW)
    # Our programs embed their C++ library: that of the system MinGW and that of MSYS2 (which
    # Qt's DLLs depend on) are not equivalent ("__cxa_thread_atexit" is missing from the latter).
    add_link_options(-static-libgcc -static-libstdc++)
    # Files over 2 GB: 64-bit off_t (databases go through src/db/fileoffset.h anyway)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

# ---- Chess core: pure C++, no Qt (rules, FEN, SAN, PGN, Hydra64 databases) ----
find_package(Threads REQUIRED)
add_library(hydra64_core STATIC
    src/chess/types.h
    src/chess/position.h src/chess/position.cpp
    src/chess/game.h     src/chess/game.cpp
    src/chess/pgn.h      src/chess/pgn.cpp
    src/chess/epd.h      src/chess/epd.cpp
    src/chess/polyglot.h src/chess/polyglot.cpp src/chess/polyglot_random.inc
    src/chess/ctg.h      src/chess/ctg.cpp      src/chess/ctg_tables.inc
    src/chess/treebook.h src/chess/treebook.cpp
    src/chess/eco.h src/chess/eco.cpp
    src/chess/repertoire.h src/chess/repertoire.cpp
    src/chess/tablebase.h src/chess/tablebase.cpp
    third_party/fathom/tbprobe.c
    src/deep/deeptree.h src/deep/deeptree.cpp
    src/deep/deepplanner.h src/deep/deepplanner.cpp
    src/chess/aquarium.h src/chess/aquarium.cpp src/chess/aquarium_codes.inc
    src/db/materialquery.h src/db/materialquery.cpp
    src/db/patternsearch.h src/db/patternsearch.cpp
    src/tablefs/udf.h src/tablefs/udf.cpp
    src/db/nameindex.h src/db/nameindex.cpp
    src/db/headerquery.h src/db/headerquery.cpp
    src/db/format.h      src/db/format.cpp
    src/db/mappedfile.h  src/db/mappedfile.cpp
    src/db/names.h       src/db/names.cpp
    src/db/gamecodec.h   src/db/gamecodec.cpp
    src/db/database.h    src/db/database.cpp
    src/db/positionindex.h src/db/positionindex.cpp
    src/db/gametree.h      src/db/gametree.cpp
    src/db/beauty.h        src/db/beauty.cpp
    src/db/preparation.h   src/db/preparation.cpp
    src/db/heatmap.h       src/db/heatmap.cpp
    src/db/convert.h     src/db/convert.cpp
    src/db/chessassistant.h src/db/chessassistant.cpp
    src/db/playerbase.h src/db/playerbase.cpp
)
target_include_directories(hydra64_core PUBLIC src)
target_include_directories(hydra64_core PRIVATE third_party/fathom)
# Fathom (C): probing of the Syzygy tables
set_source_files_properties(third_party/fathom/tbprobe.c PROPERTIES COMPILE_OPTIONS "-w")
# Hydra64 databases (src/db): game text compressed with zlib
find_package(ZLIB REQUIRED)
target_link_libraries(hydra64_core PUBLIC ZLIB::ZLIB Threads::Threads)
target_compile_options(hydra64_core PRIVATE -Wall -Wextra)

# ---- Qt application ----
set(QT_NO_PRIVATE_MODULE_WARNING ON) # ADS uses Qt6::GuiPrivate on Linux
set(QT_NO_QTPATHS_DEPLOYMENT_WARNING ON) # Windows: the archive is made by tools/windows/package.py, not by Qt's deployment (no qtpaths in the sysroot)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Network PrintSupport)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate) # required by ADS on Linux
endif()
qt_standard_project_setup()

# ---- Qt Advanced Docking System (LGPL 2.1), built statically ----
include(FetchContent)
set(QT_VERSION_MAJOR 6)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(qtads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        v5.1.1
    GIT_SHALLOW    TRUE
    SYSTEM
    # Parentless floating windows (orphaned native windows with Qt 6.12): see the script
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-qtads.cmake
)
FetchContent_MakeAvailable(qtads)
target_compile_options(qtadvanceddocking-qt6 PRIVATE -w) # warnings of the dependency hidden

# Interface: library shared by the application and the interface tests
add_library(hydra64_ui STATIC
    src/engine/uciengine.h      src/engine/uciengine.cpp
    src/engine/engineregistry.h src/engine/engineregistry.cpp
    src/ui/enginemanager.h      src/ui/enginemanager.cpp
    src/ui/theme.h              src/ui/theme.cpp
    src/ui/display.h            src/ui/display.cpp
    src/ui/preferences.h        src/ui/preferences.cpp
    src/ui/piecerenderer.h      src/ui/piecerenderer.cpp
    src/ui/boardtheme.h         src/ui/boardtheme.cpp
    src/ui/boardwidget.h        src/ui/boardwidget.cpp
    src/ui/gamesession.h        src/ui/gamesession.cpp
    src/ui/evalbar.h            src/ui/evalbar.cpp
    src/ui/evalgraph.h          src/ui/evalgraph.cpp
    src/ui/notationview.h       src/ui/notationview.cpp
    src/ui/enginepanel.h        src/ui/enginepanel.cpp
    src/ui/bookpanel.h          src/ui/bookpanel.cpp
    src/ui/modulewidgets.h      src/ui/modulewidgets.cpp
    src/ui/clockpanel.h         src/ui/clockpanel.cpp
    src/ui/playenginepanel.h    src/ui/playenginepanel.cpp
    src/ui/tournament.h         src/ui/tournament.cpp
    src/ui/tournamentpanel.h    src/ui/tournamentpanel.cpp
    src/ui/tournamentoverview.cpp
    src/ui/tournamenthtml.h     src/ui/tournamenthtml.cpp
    src/ui/participantdialog.h  src/ui/participantdialog.cpp
    src/ui/gamelist.h           src/ui/gamelist.cpp
    src/ui/baseicons.h          src/ui/baseicons.cpp
    src/ui/bundledbases.h       src/ui/bundledbases.cpp
    src/ui/userdirs.h           src/ui/userdirs.cpp
    src/chessbase/chessbase.h   src/chessbase/chessbase.cpp
    src/chessbase/cbtexts.h     src/chessbase/cbtexts.cpp
    src/ui/indexedsource.h      src/ui/hy64source.h          src/ui/hy64source.cpp
    src/ui/conversion.h         src/ui/conversion.cpp
    src/ui/convertdialog.h      src/ui/convertdialog.cpp
    src/ui/solutiontest.h       src/ui/solutiontest.cpp
    src/ui/annotator.h          src/ui/annotator.cpp
    src/ui/coach.h              src/ui/coach.cpp
    src/ui/deepanalysis.h       src/ui/deepanalysis.cpp
    src/ui/deepdialogs.h        src/ui/deepdialogs.cpp
    src/ui/deeppanel.h          src/ui/deeppanel.cpp
    src/ui/headersearchdialog.h src/ui/headersearchdialog.cpp
    src/ui/playerreport.h       src/ui/playerreport.cpp
    src/ui/preparationdialog.h  src/ui/preparationdialog.cpp
    src/ui/errorreport.h        src/ui/errorreport.cpp
    src/ui/assistedanalysis.h   src/ui/assistedanalysis.cpp
    src/ui/indexdialog.h        src/ui/indexdialog.cpp
    src/ui/tablebases.h         src/ui/tablebases.cpp
    src/ui/defaultsets.h        src/ui/defaultsets.cpp
    src/ui/tabengine.h          src/ui/tabengine.cpp
    src/ui/quizdialog.h         src/ui/quizdialog.cpp
    src/ui/guessdialog.h        src/ui/guessdialog.cpp
    src/ui/pieceheatdialog.h    src/ui/pieceheatdialog.cpp
    src/ui/namefixdialog.h      src/ui/namefixdialog.cpp
       src/ui/playerencyclopedia.cpp
    src/ui/eventtable.h         src/ui/eventtable.cpp
    src/ui/openings.h           src/ui/openings.cpp
    src/ui/gamepreview.h        src/ui/gamepreview.cpp
    src/ui/autoplayer.h         src/ui/autoplayer.cpp
    src/ui/training.h           src/ui/training.cpp
    src/ui/trainingprogress.h   src/ui/trainingprogress.cpp
    src/ui/collectioncheck.h    src/ui/collectioncheck.cpp
    src/ui/addexercise.h        src/ui/addexercise.cpp
    src/ui/tablebasepanel.h     src/ui/tablebasepanel.cpp
    src/ui/filtergraph.h        src/ui/filtergraph.cpp
    src/ui/ecobrowser.h         src/ui/ecobrowser.cpp
    src/ui/surveyindex.h        src/ui/surveyindex.cpp
    src/ui/surveybrowser.h      src/ui/surveybrowser.cpp
    src/ui/piecetracker.h       src/ui/piecetracker.cpp
    src/ui/calcvar.h            src/ui/calcvar.cpp
    src/ui/openingreport.h      src/ui/openingreport.cpp
    src/ui/solutiontestdialog.h src/ui/solutiontestdialog.cpp
    src/ui/boardviewer.h        src/ui/boardviewer.cpp
    src/ui/chessclock.h         src/ui/chessclock.cpp
    src/ui/enginegame.h         src/ui/enginegame.cpp
    src/ui/autoevaluator.h      src/ui/autoevaluator.cpp
    src/ui/playdialog.h         src/ui/playdialog.cpp
    src/ui/positioneditor.h     src/ui/positioneditor.cpp
    src/ui/gameinfodialog.h     src/ui/gameinfodialog.cpp
    src/ui/mainwindow.h         src/ui/mainwindow.cpp
)
# Images of the style sheet (drop-down arrow…) and the logo (tools/logo/make_logo.py)
qt_add_resources(hydra64_ui "style_images" PREFIX /style BASE resources FILES
    resources/hydra64.svg resources/hydra64-small.svg
    resources/chevron-down.svg resources/chevron-down-hover.svg resources/chevron-down-disabled.svg
    resources/check.svg resources/check-disabled.svg)
# Default piece set (resources/pieces/hydra64/wK.svg…) and board (resources/board/hydra64/light.svg, dark.svg):
# the only ones in the code (the board is drawn by tools/sets/make_hydra64.py)
file(GLOB PIECE_IMAGES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/pieces/hydra64/*.svg)
qt_add_resources(hydra64_ui "piece_sets" PREFIX /pieces BASE resources/pieces FILES ${PIECE_IMAGES})
qt_add_resources(hydra64_ui "default_board" PREFIX /board BASE resources/board FILES
    resources/board/hydra64/light.svg resources/board/hydra64/dark.svg)
# Other piece sets (SVG) and boards (images or SVG) shipped, from Lichess: resources/sets/<Pieces|Boards>/<set>/, copied at
# first launch into the user's themes folder, with format.html (src/ui/defaultsets.cpp). Adding a folder is enough, without touching the code.
file(GLOB_RECURSE DEFAULT_SETS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    resources/sets/*.svg resources/sets/*.png resources/sets/*.jpg resources/sets/*/CREDITS.txt resources/sets/format.html)
qt_add_resources(hydra64_ui "default_sets" PREFIX /sets BASE resources/sets FILES ${DEFAULT_SETS})
# ECO codes and opening names (lichess-org/chess-openings, public domain)
qt_add_resources(hydra64_ui "eco_openings" PREFIX /eco BASE resources/eco-opening FILES
    resources/eco-opening/a.tsv resources/eco-opening/b.tsv resources/eco-opening/c.tsv resources/eco-opening/d.tsv resources/eco-opening/e.tsv)
# Bundled databases (databases/ folder: sample games, training, tournament openings, engine
# tests — single-file Hydra64 databases and their README.html notices): read from disk
# (not as a resource), copied as they are next to the executable. This is the folder that is distributed.
file(GLOB_RECURSE BUNDLED_BASES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/databases CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/databases/*)
set(BUNDLED_BASES_OUT)
foreach(rel ${BUNDLED_BASES})
    set(out ${CMAKE_CURRENT_BINARY_DIR}/databases/${rel})
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/databases/${rel} ${out}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/databases/${rel} VERBATIM)
    list(APPEND BUNDLED_BASES_OUT ${out})
endforeach()
add_custom_target(bundled_bases ALL DEPENDS ${BUNDLED_BASES_OUT})
add_dependencies(hydra64_ui bundled_bases)
# Fallback when the executable does not have the databases next to it (started from another folder)
target_compile_definitions(hydra64_ui PRIVATE HYDRA64_DATABASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/databases")
target_link_libraries(hydra64_ui PUBLIC hydra64_core Qt6::Widgets Qt6::Svg Qt6::Network Qt6::PrintSupport ads::qtadvanceddocking-qt6)
target_compile_options(hydra64_ui PRIVATE -Wall -Wextra)

qt_add_executable(hydra64 src/main.cpp)
target_link_libraries(hydra64 PRIVATE hydra64_ui)
# Windows: graphical program, otherwise a console window opens next to the application
set_target_properties(hydra64 PROPERTIES WIN32_EXECUTABLE TRUE)
if(UNIX AND NOT APPLE)
    # Function names in the call stacks of the diagnostics (src/main.cpp)
    target_link_options(hydra64 PRIVATE -rdynamic)
endif()

# Command-line conversion (PGN, ChessBase → Hydra64 database)
qt_add_executable(hydra64-convert src/tools/convert_main.cpp)
set_target_properties(hydra64-convert PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
target_link_libraries(hydra64-convert PRIVATE hydra64_ui)

if(WIN32)
    # Manifest: file paths in UTF-8 (accented folders), otherwise fopen would read them in the
    # system code page.
    target_sources(hydra64 PRIVATE resources/windows/hydra64.rc)
    target_sources(hydra64-convert PRIVATE resources/windows/hydra64.rc)
endif()

# ---- Translations: the interface is written in French, English is translated.
# Update after adding texts: cmake --build build --target update_translations
# then python3 translations/fill_en.py
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(TARGETS hydra64
    SOURCE_TARGETS hydra64 hydra64_ui hydra64_core  # not ADS's sources
    TS_FILES translations/hydra64_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -source-language fr_FR -target-language en_US
)
# ADS (written in English) has no French translation: ours, compiled only.
qt_add_lrelease(hydra64 TS_FILES translations/ads_fr.ts QM_FILES_OUTPUT_VARIABLE ads_qm_files)
qt_add_resources(hydra64 "ads_translations" PREFIX /i18n BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${ads_qm_files})
target_compile_options(hydra64 PRIVATE -Wall -Wextra)

# ---- Shuffle: embedded UCI engine in Rust (engines/shuffle) ----
# Built by cargo and copied next to the hydra64 executable, which registers it at first launch.
find_program(CARGO cargo)
# Cross-compilation to Windows (cmake/mingw-w64.cmake): cargo targets the same platform, its outputs
# are in a subfolder named after it and the programs carry ".exe". Built on Windows (MSYS2, see
# .github/workflows/release.yml), MSYS2's cargo targets that platform by itself.
set(CARGO_TARGET_ARGS)
set(CARGO_OUT_SUBDIR "release")
if(WIN32 AND CMAKE_CROSSCOMPILING)
    set(CARGO_TARGET_ARGS --target x86_64-pc-windows-gnu)
    set(CARGO_OUT_SUBDIR "x86_64-pc-windows-gnu/release")
endif()
if(CARGO)
    set(SHUFFLE_DIR ${CMAKE_SOURCE_DIR}/engines/shuffle)
    set(SHUFFLE_TARGET_DIR ${CMAKE_BINARY_DIR}/shuffle-target)
    # "shuffle_engine" and not "shuffle": with Ninja, a target cannot bear the name of the file it
    # produces in the build folder (Linux: build/shuffle).
    add_custom_target(shuffle_engine ALL
        COMMAND ${CARGO} build --release --quiet ${CARGO_TARGET_ARGS} --manifest-path ${SHUFFLE_DIR}/Cargo.toml --target-dir ${SHUFFLE_TARGET_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHUFFLE_TARGET_DIR}/${CARGO_OUT_SUBDIR}/shuffle${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_BINARY_DIR}/shuffle${CMAKE_EXECUTABLE_SUFFIX}
        BYPRODUCTS ${CMAKE_BINARY_DIR}/shuffle${CMAKE_EXECUTABLE_SUFFIX}
        COMMENT "Compilation du moteur Shuffle (cargo)"
        VERBATIM)
    add_dependencies(hydra64 shuffle_engine)
else()
    message(STATUS "cargo introuvable : le moteur Shuffle (Rust) ne sera pas compilé")
endif()

# ---- Reading of ChessBase databases (.cbh, .2cbh, .cbv archives) ----
# Rust bridge (src/chessbase/bridge) over the cbvault library (MIT, https://github.com/itshak/cbvault ;
# modified local copy in src/chessbase/bridge/vendor/cbvault),
# built by cargo as a static library. First build: download of the crates (versions pinned
# by Cargo.lock). Without cargo, Hydra64 builds without ChessBase reading.
option(HYDRA64_CHESSBASE "Lire les bases ChessBase (nécessite cargo)" ON)
if(CARGO AND HYDRA64_CHESSBASE)
    set(CBBRIDGE_DIR ${CMAKE_SOURCE_DIR}/src/chessbase/bridge)
    set(CBBRIDGE_TARGET_DIR ${CMAKE_BINARY_DIR}/cbbridge-target)
    set(CBBRIDGE_LIB ${CBBRIDGE_TARGET_DIR}/${CARGO_OUT_SUBDIR}/libcbbridge.a)
    # Sources of the bridge and of the modified copy of cbvault (src/chessbase/bridge/vendor/cbvault/HYDRA64.md).
    file(GLOB_RECURSE CBBRIDGE_SOURCES CONFIGURE_DEPENDS ${CBBRIDGE_DIR}/src/*.rs ${CBBRIDGE_DIR}/vendor/*.rs ${CBBRIDGE_DIR}/vendor/*.toml)
    add_custom_command(
        OUTPUT ${CBBRIDGE_LIB}
        COMMAND ${CARGO} build --release --quiet --locked ${CARGO_TARGET_ARGS} --manifest-path ${CBBRIDGE_DIR}/Cargo.toml --target-dir ${CBBRIDGE_TARGET_DIR}
        DEPENDS ${CBBRIDGE_SOURCES} ${CBBRIDGE_DIR}/Cargo.toml ${CBBRIDGE_DIR}/Cargo.lock
        COMMENT "Compilation du pont ChessBase (cargo, cbvault)"
        VERBATIM)
    add_custom_target(cbbridge DEPENDS ${CBBRIDGE_LIB})
    add_library(cbbridge_lib STATIC IMPORTED GLOBAL)
    set_target_properties(cbbridge_lib PROPERTIES IMPORTED_LOCATION ${CBBRIDGE_LIB})
    add_dependencies(cbbridge_lib cbbridge)
    add_dependencies(hydra64_ui cbbridge)
    if(WIN32)
        # System libraries the Rust standard library needs on Windows ("synchronization":
        # WaitOnAddress and WakeByAddress*, which its locks use since Rust 1.100)
        target_link_libraries(hydra64_ui PUBLIC cbbridge_lib ws2_32 userenv bcrypt ntdll advapi32 kernel32 synchronization)
    else()
        target_link_libraries(hydra64_ui PUBLIC cbbridge_lib ${CMAKE_DL_LIBS} pthread m)
    endif()
    target_compile_definitions(hydra64_ui PRIVATE HYDRA64_CHESSBASE)
    add_test(NAME cbbridge_tests COMMAND ${CARGO} test --release --quiet --locked --manifest-path ${CBBRIDGE_DIR}/Cargo.toml --target-dir ${CBBRIDGE_TARGET_DIR})
else()
    message(STATUS "Lecture ChessBase désactivée (cargo absent ou HYDRA64_CHESSBASE=OFF)")
endif()

# ---- Embedded Stockfish: the automatic evaluation depends on it ----
# Built only once from the official sources (Internet connection needed at the
# first ./build.sh: sources and NNUE networks), then copied next to the hydra64 executable.
option(BUNDLE_STOCKFISH "Compiler et embarquer Stockfish" ON)
set(STOCKFISH_TAG "sf_19" CACHE STRING "Version de Stockfish embarquée (étiquette git)")
set(STOCKFISH_ARCH "native" CACHE STRING "Architecture de compilation de Stockfish (native, x86-64-avx2, x86-64…)")
if(BUNDLE_STOCKFISH)
    include(ExternalProject)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC EQUAL 0)
        set(NPROC 4)
    endif()
    set(STOCKFISH_COMP)
    if(MINGW)
        # Its Makefile then takes x86_64-w64-mingw32-c++ (static linking): the system MinGW from
        # Linux, that of MSYS2 on Windows.
        set(STOCKFISH_COMP COMP=mingw)
    endif()
    ExternalProject_Add(stockfish_engine # not "stockfish": name of the file produced, see shuffle_engine
        GIT_REPOSITORY https://github.com/official-stockfish/Stockfish.git
        GIT_TAG ${STOCKFISH_TAG}
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        PREFIX ${CMAKE_BINARY_DIR}/stockfish-build
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make -C src -j${NPROC} build ARCH=${STOCKFISH_ARCH} ${STOCKFISH_COMP}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/src/stockfish${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_BINARY_DIR}/stockfish${CMAKE_EXECUTABLE_SUFFIX}
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/stockfish${CMAKE_EXECUTABLE_SUFFIX}
        LOG_BUILD TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
        COMMENT "Compilation de Stockfish (${STOCKFISH_TAG})")
    add_dependencies(hydra64 stockfish_engine)
endif()

# ---- Tests ----
option(HYDRA64_TESTS "Compiler les tests" ON)
if(NOT HYDRA64_TESTS)
    return() # cross-compilation: only the programs
endif()
enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE hydra64_core)
add_test(NAME core_tests COMMAND core_tests ${CMAKE_SOURCE_DIR}/tests/data/classics.pgn)
# Interface tests (real main window, off screen, isolated configuration)
add_executable(ui_tests tests/ui_tests.cpp)
target_link_libraries(ui_tests PRIVATE hydra64_ui)
# Test of the script of the tournaments' HTML page: run by Qt's JavaScript engine if installed.
find_package(Qt6 QUIET COMPONENTS Qml)
if(Qt6Qml_FOUND)
    target_link_libraries(ui_tests PRIVATE Qt6::Qml)
    target_compile_definitions(ui_tests PRIVATE HYDRA64_TEST_JS)
endif()
target_compile_definitions(ui_tests PRIVATE HYDRA64_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(core_tests PRIVATE HYDRA64_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ui_tests COMMAND ui_tests) # isolates its configuration itself (ui_tests_home)
set_tests_properties(ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(BUNDLE_STOCKFISH)
    add_dependencies(ui_tests stockfish_engine)
endif()
if(CARGO)
    add_dependencies(ui_tests shuffle_engine)
    add_test(NAME shuffle_tests COMMAND ${CARGO} test --release --quiet --manifest-path ${SHUFFLE_DIR}/Cargo.toml --target-dir ${SHUFFLE_TARGET_DIR})
endif()

# Endgame tablebases read from disc images (.iso): small FUSE file system,
# started by the application. Optional: without libfuse3, the images cannot be used.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND UNIX AND NOT APPLE)
    pkg_check_modules(FUSE3 QUIET IMPORTED_TARGET fuse3)
endif()
if(TARGET PkgConfig::FUSE3)
    add_executable(hydra64-tablefs tools/tablefs/main.cpp)
    target_include_directories(hydra64-tablefs PRIVATE src)
    target_link_libraries(hydra64-tablefs PRIVATE hydra64_core PkgConfig::FUSE3)
    message(STATUS "Tables de finales dans des images ISO : hydra64-tablefs (FUSE 3)")
else()
    message(STATUS "libfuse3 absente : les tables de finales dans des images ISO ne seront pas utilisables")
endif()
