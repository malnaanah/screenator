cmake_minimum_required(VERSION 3.0)
project(screenator VERSION 3.2 LANGUAGES C)
include(GNUInstallDirs)

set(target_name screenator)

set(CMAKE_C_FLAGS " ${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

find_package(PkgConfig)

pkg_check_modules(gtk3 REQUIRED "gtk+-3.0 >= 3.22")
pkg_check_modules(xinput REQUIRED "xi >= 1.3")
pkg_check_modules(x11 REQUIRED x11)
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
)

link_directories(
    ${gtk3_LIBRARY_DIRS}
    ${appindicator3_LIBRARY_DIRS}
    ${xinput_LIBRARY_DIRS}
    ${x11_LIBRARY_DIRS}
)

set(sources
    src/screenator.c
    src/screenator.h
    src/callbacks.c
    src/callbacks.h
    src/config.c
    src/config.h
    src/input.c
    src/input.h
    data/paint_cursor.xpm
    data/text_cursor.xpm
    data/erase_cursor.xpm
    data/bucket_cursor.xpm
)

add_executable(${target_name} ${sources})

target_link_libraries(${target_name}
    ${gtk3_LIBRARIES}
    ${appindicator3_LIBRARIES}
    ${xinput_LIBRARIES}
    ${x11_LIBRARIES}
    -lm
)

install(TARGETS ${target_name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/screenator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/screenator.cfg DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/screenator)
install(FILES README.md AUTHORS DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES screenator.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES data/screenator.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES data/screenator.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/screenator-active.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES
    data/pen.png
    data/arrow.png
    data/text.png
    data/free-line.png
    data/spline.png
    data/polygon.png
    data/triangle.png
    data/bucket.png
    data/math.png
    data/bars.png
    data/ellipse.png
    data/rounded-rectangle.png
    data/marker.png
    data/eraser.png
    data/undo.png
    data/redo.png
    data/trash.png
    data/reset-settings.png
    data/hide.png
    data/font-settings.png
    data/shapes.png
    data/paper.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/screenator)
install(FILES data/screenator.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
