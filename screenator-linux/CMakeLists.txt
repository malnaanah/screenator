cmake_minimum_required(VERSION 3.0)
project(screenator VERSION 9.2 LANGUAGES C)
include(GNUInstallDirs)

set(target_name screenator)
set(SHARED_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
set(LINUX_ASSET_DIR "${SHARED_ASSET_DIR}/linux")
set(LINUX_ICON_DIR "${SHARED_ASSET_DIR}/icons-linux")

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

find_program(IMAGEMAGICK_CONVERT_EXECUTABLE convert)
if(NOT IMAGEMAGICK_CONVERT_EXECUTABLE)
  message(FATAL_ERROR "ImageMagick convert is required to create the tray PNG from the Linux SVG icon")
endif()
set(SCREENATOR_TRAY_ICON "${CMAKE_CURRENT_BINARY_DIR}/screenator.png")
add_custom_command(
    OUTPUT "${SCREENATOR_TRAY_ICON}"
    COMMAND "${IMAGEMAGICK_CONVERT_EXECUTABLE}" -background none
            "${LINUX_ICON_DIR}/screenator.svg" -resize 128x128
            "${SCREENATOR_TRAY_ICON}"
    DEPENDS "${LINUX_ICON_DIR}/screenator.svg"
    VERBATIM)
add_custom_target(screenator_tray_icon ALL DEPENDS "${SCREENATOR_TRAY_ICON}")

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
install(FILES ${SCREENATOR_TRAY_ICON} DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES ${LINUX_ICON_DIR}/screenator.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${LINUX_ASSET_DIR}/screenator-active.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES
    ${LINUX_ICON_DIR}/pen.svg
    ${LINUX_ICON_DIR}/arrow.svg
    ${LINUX_ICON_DIR}/timer.svg
    ${LINUX_ICON_DIR}/text.svg
    ${LINUX_ICON_DIR}/free-line.svg
    ${LINUX_ICON_DIR}/spline.svg
    ${LINUX_ICON_DIR}/polygon.svg
    ${LINUX_ICON_DIR}/triangle.svg
    ${LINUX_ICON_DIR}/bucket.svg
    ${LINUX_ICON_DIR}/math.svg
    ${LINUX_ICON_DIR}/bars.svg
    ${LINUX_ICON_DIR}/random-signal.svg
    ${LINUX_ICON_DIR}/ellipse.svg
    ${LINUX_ICON_DIR}/rounded-rectangle.svg
    ${LINUX_ICON_DIR}/marker.svg
    ${LINUX_ICON_DIR}/eraser.svg
    ${LINUX_ICON_DIR}/undo.svg
    ${LINUX_ICON_DIR}/redo.svg
    ${LINUX_ICON_DIR}/trash.svg
    ${LINUX_ICON_DIR}/eye-opened.svg
    ${LINUX_ICON_DIR}/eye-closed.svg
    ${LINUX_ICON_DIR}/hint.svg
    ${LINUX_ICON_DIR}/settings.svg
    ${LINUX_ICON_DIR}/quit.svg
    ${LINUX_ICON_DIR}/switch_mode.svg
    ${LINUX_ICON_DIR}/shapes.svg
    ${LINUX_ICON_DIR}/paper.svg
    ${LINUX_ICON_DIR}/add_before.svg
    ${LINUX_ICON_DIR}/delete-page.svg
    ${LINUX_ICON_DIR}/add_after.svg
    ${LINUX_ICON_DIR}/goto-page.svg
    ${LINUX_ICON_DIR}/actions.svg
    ${LINUX_ICON_DIR}/save.svg
    ${LINUX_ICON_DIR}/screenshot.svg
    ${LINUX_ICON_DIR}/opacity.svg
    ${LINUX_ICON_DIR}/marker_opacity.svg
    ${LINUX_ICON_DIR}/cursor.svg
    ${LINUX_ICON_DIR}/app.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator/icons)
install(FILES ${SHARED_ASSET_DIR}/hints.html DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator)
install(FILES ${LINUX_ASSET_DIR}/screenator.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
