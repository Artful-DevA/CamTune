cmake_minimum_required(VERSION 3.16)
project(CamTune VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CAMTUNE_BUILD_GUI "Build the Qt application" ON)
option(CAMTUNE_BUILD_TESTS "Build unit tests and benchmark" ON)
option(CAMTUNE_NATIVE "Optimize for the build machine's CPU (-march=native)" OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

# ---------------------------------------------------------------------------
# Core: capture, processing and virtual-camera output. No Qt dependency.
# ---------------------------------------------------------------------------
# The person-detection model is compiled into the binary (no files to find
# at runtime, nothing downloaded).
set(CAMTUNE_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/resources/models/selfie_segmentation_landscape.camnn)
set(CAMTUNE_MODEL_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/SelfieModelData.cpp)
add_custom_command(
  OUTPUT ${CAMTUNE_MODEL_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${CAMTUNE_MODEL} -DOUT=${CAMTUNE_MODEL_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedModel.cmake
  DEPENDS ${CAMTUNE_MODEL} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedModel.cmake
  COMMENT "Embedding person segmentation model")

add_library(camcore STATIC
  ${CAMTUNE_MODEL_CPP}
  src/core/Blur.cpp
  src/core/PersonSegmenter.cpp
  src/core/nn/Network.cpp
  src/core/Frame.cpp
  src/core/ThreadPool.cpp
  src/core/Framing.cpp
  src/core/Processor.cpp
  src/core/Effects.cpp
  src/core/MjpegDecoder.cpp
  src/v4l2/V4l2Util.cpp
  src/v4l2/CaptureDevice.cpp
  src/v4l2/CameraControls.cpp
  src/v4l2/LoopbackOutput.cpp
  src/pipeline/CaptureWorker.cpp
  src/pipeline/ControlWorker.cpp
  src/pipeline/OutputWorker.cpp
  src/pipeline/ProcessingWorker.cpp
  src/pipeline/Engine.cpp
)
# Pixel/tensor math never produces NaN or infinities by design; these flags
# let the compiler vectorize clamps and blends in the hot loops.
set_source_files_properties(
  src/core/Blur.cpp src/core/Effects.cpp src/core/PersonSegmenter.cpp src/core/nn/Network.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-math-errno;-ffinite-math-only;-fno-signed-zeros;-fno-trapping-math")
target_include_directories(camcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(camcore PUBLIC Threads::Threads PRIVATE PkgConfig::TURBOJPEG)
target_compile_options(camcore PRIVATE -Wall -Wextra -Wno-unused-parameter
  $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O3>)
if(CAMTUNE_NATIVE)
  target_compile_options(camcore PRIVATE -march=native)
endif()

# ---------------------------------------------------------------------------
# Qt application
# ---------------------------------------------------------------------------
if(CAMTUNE_BUILD_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets DBus)
  set(CMAKE_AUTOMOC ON)

  add_executable(camtune
    src/main.cpp
    src/app/Application.cpp
    src/app/CameraController.cpp
    src/app/Serialization.cpp
    src/app/PresetStore.cpp
    src/app/AppSettings.cpp
    src/app/DBusService.cpp
    src/app/SystemIntegration.cpp
    src/app/GlobalShortcuts.cpp
    src/app/ImageUtil.cpp
    src/ui/MainWindow.cpp
    src/ui/PreviewWidget.cpp
    src/ui/Widgets.cpp
    src/ui/HardwareControlsPanel.cpp
    src/ui/PresetsPanel.cpp
    src/ui/BackgroundPanel.cpp
    src/ui/Theme.cpp
    src/ui/Icons.cpp
    resources/resources.qrc
  )
  set_target_properties(camtune PROPERTIES AUTORCC ON)
  target_compile_definitions(camtune PRIVATE
    CAMTUNE_VERSION="${PROJECT_VERSION}"
    CAMTUNE_LIBEXECDIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}"
    QT_NO_KEYWORDS)
  target_compile_options(camtune PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(camtune PRIVATE camcore
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::DBus)

  install(TARGETS camtune RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/io.github.CamTune.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES resources/icons/hicolor/camtune-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME io.github.CamTune.png)
  endforeach()
  install(FILES packaging/io.github.CamTune.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(PROGRAMS scripts/camtune-setup-v4l2loopback
          DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
  configure_file(packaging/io.github.CamTune.policy.in
                 ${CMAKE_CURRENT_BINARY_DIR}/io.github.CamTune.policy @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.CamTune.policy
          DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CAMTUNE_BUILD_TESTS)
  enable_testing()
  add_executable(camcore_tests tests/core_tests.cpp)
  target_link_libraries(camcore_tests PRIVATE camcore PkgConfig::TURBOJPEG)
  add_test(NAME camcore_tests COMMAND camcore_tests)

  add_executable(camcore_soak tests/soak.cpp)
  target_link_libraries(camcore_soak PRIVATE camcore)
  add_test(NAME camcore_soak COMMAND camcore_soak 6 60)

  add_executable(camcore_bench tests/bench.cpp)
  target_link_libraries(camcore_bench PRIVATE camcore)
endif()

# ---------------------------------------------------------------------------
# Packages: cpack -G DEB / cpack -G RPM from a build directory configured with
# -DCMAKE_INSTALL_PREFIX=/usr. Build on the oldest release of each distro you
# want to support; dependencies are detected from the linked libraries.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME camtune)
set(CPACK_PACKAGE_VENDOR "CamTune")
set(CPACK_PACKAGE_CONTACT "Dev-Png497 <246381290+Dev-Png497@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Dev-Png497/LinuxCameraAdjust")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Webcam controls and low-latency virtual camera for video calls")
set(CPACK_PACKAGE_DESCRIPTION
  "CamTune adjusts your webcam (exposure, focus, white balance, color, zoom and\nframing, background blur) and sends the result to a v4l2loopback virtual\ncamera that Zoom, Meet, Teams, Discord and OBS can use. Everything runs locally.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Debian / Ubuntu
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION video)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "v4l2loopback-dkms, pkexec | policykit-1, qt6-wayland")
# Debian continuation lines start with a space.
string(REPLACE "\n" "\n " _camtune_deb_desc "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n ${_camtune_deb_desc}")

# Fedora / openSUSE
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
# The polkit policy names the helper's absolute path.
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
set(CPACK_RPM_PACKAGE_SUGGESTS "v4l2loopback, polkit, qt6-qtwayland")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /usr/share/applications /usr/share/metainfo /usr/share/polkit-1 /usr/share/polkit-1/actions
  /usr/share/icons /usr/share/icons/hicolor /usr/libexec)
foreach(size 16 22 24 32 48 64 128 256 512)
  list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/icons/hicolor/${size}x${size} /usr/share/icons/hicolor/${size}x${size}/apps)
endforeach()

include(CPack)
