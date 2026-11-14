cmake_minimum_required(VERSION 3.0)
project(screenator VERSION 8.1 LANGUAGES C)
include(GNUInstallDirs)

set(target_name screenator)

set(CMAKE_C_FLAGS " ${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

find_package(PkgConfig)

pkg_check_modules(gtk3 REQUIRED "gtk+-3.0 >= 3.22")
pkg_check_modules(xinput REQUIRED "xi >= 1.3")
pkg_check_modules(x11 REQUIRED x11)
pkg_check_modules(xtst xtst)
pkg_check_modules(poppler_glib REQUIRED "poppler-glib >= 0.20")
pkg_check_modules(appindicator3 "ayatana-appindicator3-0.1 >= 0.5")
if(NOT appindicator3_FOUND)
  pkg_check_modules(appindicator3 REQUIRED "appindicator3-0.1 >= 0.4.92")
  set(APPINDICATOR_IS_LEGACY 1)
endif()

configure_file(build-config.h.in build-config.h)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${gtk3_INCLUDE_DIRS}
    ${appindicator3_INCLUDE_DIRS}
    ${xinput_INCLUDE_DIRS}
    ${x11_INCLUDE_DIRS}
    ${xtst_INCLUDE_DIRS}
    ${poppler_glib_INCLUDE_DIRS}
)

link_directories(
    ${gtk3_LIBRARY_DIRS}
    ${appindicator3_LIBRARY_DIRS}
    ${xinput_LIBRARY_DIRS}
    ${x11_LIBRARY_DIRS}
    ${xtst_LIBRARY_DIRS}
    ${poppler_glib_LIBRARY_DIRS}
)

if(xtst_FOUND)
  add_definitions(-DHAVE_XTEST)
endif()

if(poppler_glib_FOUND)
  add_definitions(-DHAVE_POPPLER_GLIB)
endif()

set(sources
    src/screenator.c
    src/screenator.h
    src/callbacks.c
    src/callbacks.h
    src/config.c
    src/config.h
    src/input.c
    src/input.h
    assets/paint_cursor.xpm
    assets/text_cursor.xpm
    assets/erase_cursor.xpm
    assets/bucket_cursor.xpm
)

add_executable(${target_name} ${sources})

target_link_libraries(${target_name}
    ${gtk3_LIBRARIES}
    ${appindicator3_LIBRARIES}
    ${xinput_LIBRARIES}
    ${x11_LIBRARIES}
    ${xtst_LIBRARIES}
    ${poppler_glib_LIBRARIES}
    -lXext
    -lm
)

install(TARGETS ${target_name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/screenator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/screenator.cfg DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/screenator)
install(FILES README.md AUTHORS DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES screenator.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES assets/icons/screenator.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES assets/screenator.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES assets/screenator-active.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES
    assets/icons/pen.png
    assets/icons/arrow.png
    assets/icons/timer.png
    assets/icons/text.png
    assets/icons/free-line.png
    assets/icons/spline.png
    assets/icons/polygon.png
    assets/icons/triangle.png
    assets/icons/bucket.png
    assets/icons/math.png
    assets/icons/bars.png
    assets/icons/random-signal.png
    assets/icons/ellipse.png
    assets/icons/rounded-rectangle.png
    assets/icons/marker.png
    assets/icons/eraser.png
    assets/icons/undo.png
    assets/icons/redo.png
    assets/icons/trash.png
    assets/icons/reset-settings.png
    assets/icons/eye-opened.png
    assets/icons/eye-closed.png
    assets/icons/hint.png
    assets/icons/font-settings.png
    assets/icons/settings.png
    assets/icons/quit.png
    assets/icons/switch_mode.png
    assets/icons/shapes.png
    assets/icons/paper.png
    assets/icons/add_before.png
    assets/icons/delete-page.png
    assets/icons/add_after.png
    assets/icons/goto-page.png
    assets/icons/actions.png
    assets/icons/save.png
    assets/icons/screenshot.png
    assets/icons/opacity.png
    assets/icons/marker_opacity.png
    assets/icons/cursor.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator/icons)
install(FILES assets/hints.html DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator)
install(FILES assets/screenator.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
