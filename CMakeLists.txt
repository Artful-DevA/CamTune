cmake_minimum_required(VERSION 3.16)
project(LinuxCameraAdjust VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CAMADJUST_BUILD_GUI "Build the Qt application" ON)
option(CAMADJUST_BUILD_TESTS "Build unit tests and benchmark" ON)
option(CAMADJUST_NATIVE "Optimize for the build machine's CPU (-march=native)" OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

# ---------------------------------------------------------------------------
# Core: capture, processing and virtual-camera output. No Qt dependency.
# ---------------------------------------------------------------------------
# The person-detection model is compiled into the binary (no files to find
# at runtime, nothing downloaded).
set(CAMADJUST_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/resources/models/selfie_segmentation_landscape.camnn)
set(CAMADJUST_MODEL_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/SelfieModelData.cpp)
add_custom_command(
  OUTPUT ${CAMADJUST_MODEL_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${CAMADJUST_MODEL} -DOUT=${CAMADJUST_MODEL_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedModel.cmake
  DEPENDS ${CAMADJUST_MODEL} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedModel.cmake
  COMMENT "Embedding person segmentation model")

add_library(camcore STATIC
  ${CAMADJUST_MODEL_CPP}
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
if(CAMADJUST_NATIVE)
  target_compile_options(camcore PRIVATE -march=native)
endif()

# ---------------------------------------------------------------------------
# Qt application
# ---------------------------------------------------------------------------
if(CAMADJUST_BUILD_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets DBus)
  set(CMAKE_AUTOMOC ON)

  add_executable(camadjust
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
    src/ui/EffectsPanel.cpp
    resources/resources.qrc
  )
  set_target_properties(camadjust PROPERTIES AUTORCC ON)
  target_compile_definitions(camadjust PRIVATE
    CAMADJUST_VERSION="${PROJECT_VERSION}"
    CAMADJUST_LIBEXECDIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}"
    QT_NO_KEYWORDS)
  target_compile_options(camadjust PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(camadjust PRIVATE camcore
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::DBus)

  install(TARGETS camadjust RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/io.github.LinuxCameraAdjust.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/icons/camadjust.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
          RENAME io.github.LinuxCameraAdjust.svg)
  install(FILES packaging/io.github.LinuxCameraAdjust.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(PROGRAMS scripts/camadjust-setup-v4l2loopback
          DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
  configure_file(packaging/io.github.LinuxCameraAdjust.policy.in
                 ${CMAKE_CURRENT_BINARY_DIR}/io.github.LinuxCameraAdjust.policy @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.LinuxCameraAdjust.policy
          DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CAMADJUST_BUILD_TESTS)
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
