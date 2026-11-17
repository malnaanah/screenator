cmake_minimum_required(VERSION 3.0)
project(screenator VERSION 8.2 LANGUAGES C)
include(GNUInstallDirs)

set(target_name screenator)
set(SHARED_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
set(LINUX_ASSET_DIR "${SHARED_ASSET_DIR}/linux")

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
    ${LINUX_ASSET_DIR}/paint_cursor.xpm
    ${LINUX_ASSET_DIR}/text_cursor.xpm
    ${LINUX_ASSET_DIR}/erase_cursor.xpm
    ${SHARED_ASSET_DIR}/bucket_cursor.xpm
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
install(FILES ${LINUX_ASSET_DIR}/screenator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${LINUX_ASSET_DIR}/screenator.cfg DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/screenator)
install(FILES README.md AUTHORS DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES screenator.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES ${SHARED_ASSET_DIR}/icons/screenator.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES ${LINUX_ASSET_DIR}/screenator.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${LINUX_ASSET_DIR}/screenator-active.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES
    ${SHARED_ASSET_DIR}/icons/pen.png
    ${SHARED_ASSET_DIR}/icons/arrow.png
    ${SHARED_ASSET_DIR}/icons/timer.png
    ${SHARED_ASSET_DIR}/icons/text.png
    ${SHARED_ASSET_DIR}/icons/free-line.png
    ${SHARED_ASSET_DIR}/icons/spline.png
    ${SHARED_ASSET_DIR}/icons/polygon.png
    ${SHARED_ASSET_DIR}/icons/triangle.png
    ${SHARED_ASSET_DIR}/icons/bucket.png
    ${SHARED_ASSET_DIR}/icons/math.png
    ${SHARED_ASSET_DIR}/icons/bars.png
    ${SHARED_ASSET_DIR}/icons/random-signal.png
    ${SHARED_ASSET_DIR}/icons/ellipse.png
    ${SHARED_ASSET_DIR}/icons/rounded-rectangle.png
    ${SHARED_ASSET_DIR}/icons/marker.png
    ${SHARED_ASSET_DIR}/icons/eraser.png
    ${SHARED_ASSET_DIR}/icons/undo.png
    ${SHARED_ASSET_DIR}/icons/redo.png
    ${SHARED_ASSET_DIR}/icons/trash.png
    ${SHARED_ASSET_DIR}/icons/reset-settings.png
    ${SHARED_ASSET_DIR}/icons/eye-opened.png
    ${SHARED_ASSET_DIR}/icons/eye-closed.png
    ${SHARED_ASSET_DIR}/icons/hint.png
    ${SHARED_ASSET_DIR}/icons/font-settings.png
    ${SHARED_ASSET_DIR}/icons/settings.png
    ${SHARED_ASSET_DIR}/icons/quit.png
    ${SHARED_ASSET_DIR}/icons/switch_mode.png
    ${SHARED_ASSET_DIR}/icons/shapes.png
    ${SHARED_ASSET_DIR}/icons/paper.png
    ${SHARED_ASSET_DIR}/icons/add_before.png
    ${SHARED_ASSET_DIR}/icons/delete-page.png
    ${SHARED_ASSET_DIR}/icons/add_after.png
    ${SHARED_ASSET_DIR}/icons/goto-page.png
    ${SHARED_ASSET_DIR}/icons/actions.png
    ${SHARED_ASSET_DIR}/icons/save.png
    ${SHARED_ASSET_DIR}/icons/screenshot.png
    ${SHARED_ASSET_DIR}/icons/opacity.png
    ${SHARED_ASSET_DIR}/icons/marker_opacity.png
    ${SHARED_ASSET_DIR}/icons/cursor.png
    ${SHARED_ASSET_DIR}/icons/app.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator/icons)
install(FILES ${SHARED_ASSET_DIR}/hints.html DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator)
install(FILES ${LINUX_ASSET_DIR}/screenator.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
