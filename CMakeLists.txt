cmake_minimum_required(VERSION 3.21)
project(PcbSupplierWorkflow VERSION 1.8.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt widgets rely on moc/uic/rcc code generation - CMake's Qt integration
# handles this automatically when these AUTO* flags are on.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Test)

qt_standard_project_setup()

# QuaZip is LGPL - built as its own shared DLL and dynamically linked so we
# never trigger the LGPL static-linking relink obligation (research.md).
# BUILD_SHARED_LIBS defaults to ON in QuaZip's own CMakeLists already on
# Windows, but set explicitly here so the intent isn't silently dependent on
# that default.
set(BUILD_SHARED_LIBS ON)
include(FetchContent)

# QuaZip needs zlib and there's no system zlib install to find - fetch it
# too, and redirect QuaZip's own find_package(ZLIB) at this fetched copy
# (CMake 3.24+ feature) instead of requiring a manual local zlib install.
# zlib's own CMakeLists doesn't export a ZLIB::ZLIB target/config the way
# find_package(ZLIB) expects, so alias it by hand once it's fetched. Static
# (zlibstatic), not shared - zlib's permissive zlib-license has no
# static-linking obligations (unlike QuaZip's LGPL), so linking it straight
# into the QuaZip DLL avoids shipping a second small DLL for no benefit.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(zlib)
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

FetchContent_Declare(
    quazip
    GIT_REPOSITORY https://github.com/stachenov/quazip.git
    GIT_TAG v1.7.2
)
set(QUAZIP_QT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
# We only need QuaZip built and linked in-tree, not `cmake --install`-able -
# and its default install() rules fail here anyway since our hand-aliased
# zlibstatic dependency isn't part of a matching install export set.
set(QUAZIP_INSTALL OFF CACHE BOOL "" FORCE)
# QuaZip defaults to optionally linking Qt6Core5Compat (Qt5-era QTextCodec
# support, for legacy zip filename encodings) when that module happens to
# be present in the Qt kit. We don't need it, and windeployqt didn't follow
# that indirect dependency into quazip1-qt6d.dll, which is exactly what
# caused the "Qt6Core5Compatd.dll was not found" launch error - turning it
# off removes the dependency instead of chasing down another DLL to ship.
set(QUAZIP_ENABLE_QTEXTCODEC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(quazip)

add_executable(PcbSupplierWorkflow WIN32
    src/main.cpp
    src/app/mainwindow.h
    src/app/mainwindow.cpp
    src/app/sidebarnav.h
    src/app/sidebarnav.cpp
    src/app/statuslegend.h
    src/app/statuslegend.cpp
    src/app/theme.h
    src/app/theme.cpp
    src/common/palette.h
    src/common/localsettings.h
    src/common/localsettings.cpp
    src/common/filepicker.h
    src/common/filepicker.cpp
    src/archivecompare/archivecomparepage.h
    src/archivecompare/archivecomparepage.cpp
    src/archivecompare/archivecomparisonresult.h
    src/archivecompare/archivereader.h
    src/archivecompare/archivereader.cpp
    src/archivecompare/crccomparator.h
    src/archivecompare/crccomparator.cpp
    src/archivecompare/comparisonsummary.h
    src/archivecompare/comparisonsummary.cpp
    src/archivecompare/archivecomparison.h
    src/archivecompare/archivecomparison.cpp
    src/archivecompare/archivetreemodel.h
    src/archivecompare/archivetreemodel.cpp
    src/archivecompare/archivetreedelegate.h
    src/archivecompare/archivetreedelegate.cpp
    src/archivecompare/archivedroputil.h
    src/archivecompare/archiveheader.h
    src/archivecompare/archiveheader.cpp
    src/archivecompare/archivepanel.h
    src/archivecompare/archivepanel.cpp
    src/archivecompare/gerberdifftrigger.h
    src/archivecompare/gerberdifftrigger.cpp
    src/archivecompare/samedatacheck.h
    src/archivecompare/samedatacheck.cpp
    src/gerber/exportnoise.h
    src/gerber/layerequivalence.h
    src/gerber/layerequivalence.cpp
    src/gerber/gerberfiletype.h
    src/gerber/gerberfiletype.cpp
    src/gerber/gerberdiffrenderer.h
    src/gerber/gerberdiffrenderer.cpp
    src/gerber/gerberformat.h
    src/gerber/gerberformat.cpp
    src/gerber/gerberexpression.h
    src/gerber/gerberexpression.cpp
    src/gerber/gerberaperture.h
    src/gerber/gerberaperture.cpp
    src/gerber/gerbermacro.h
    src/gerber/gerbermacro.cpp
    src/gerber/gerberobjectmodel.h
    src/gerber/gerberobjectpaths.h
    src/gerber/gerberobjectpaths.cpp
    src/gerber/gerberparser.h
    src/gerber/gerberparser.cpp
    src/gerber/excellonparser.h
    src/gerber/excellonparser.cpp
    src/gerber/layermask.h
    src/gerber/layermaskrenderer.h
    src/gerber/layermaskrenderer.cpp
    src/gerber/maskcompositor.h
    src/gerber/maskcompositor.cpp
    src/gerber/compositeview.h
    src/gerber/compositeview.cpp
    resources/resources.qrc
    resources/app.rc
)

target_include_directories(PcbSupplierWorkflow PRIVATE src)

target_link_libraries(PcbSupplierWorkflow PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    QuaZip::QuaZip
)

enable_testing()
add_subdirectory(tests/unit)
