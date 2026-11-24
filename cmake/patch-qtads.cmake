# ADS (Qt Advanced Docking System) fix applied after the download (PATCH_COMMAND).
#
# Under X11, ADS creates the floating window of a detached panel as a child of the dock
# manager then calls winId() on it. With Qt 6.12, this makes the manager and its
# areas native; when a page's layout is then restored, these native windows are left
# orphaned: pieces of other pages drawn on top, frozen page, and the warning
# "QWidgetWindow(…) must be a top level window" at each context menu.
# ADS already creates these windows without a parent under Wayland: we do the same everywhere (the manager
# deletes them itself and keeps them on top under X11).
set(file "${CMAKE_CURRENT_SOURCE_DIR}/src/FloatingDockContainer.cpp")
file(READ "${file}" text)
set(before "tFloatingWidgetBase(internal::isWayland() ? nullptr : static_cast<QWidget*>(DockManager)),")
set(after "tFloatingWidgetBase(nullptr), // Hydra64 : sans parent partout (cmake/patch-qtads.cmake)")
string(FIND "${text}" "${after}" done)
if(done GREATER -1)
    return()
endif()
string(FIND "${text}" "${before}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "patch-qtads : ligne attendue introuvable dans ${file} (version d'ADS changée ?)")
endif()
string(REPLACE "${before}" "${after}" text "${text}")
file(WRITE "${file}" "${text}")
message(STATUS "patch-qtads : fenêtres flottantes sans parent")
