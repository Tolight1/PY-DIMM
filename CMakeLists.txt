cmake_minimum_required(VERSION 3.21)
project(KY_DIMM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Keep these paths configurable at configure time. Do not put pylon DLLs in src/.
set(KY_DIMM_QT_ROOT "E:/Softwoare/Qtool/qt/6.11.0/msvc2022_64" CACHE PATH "KY-DIMM Qt runtime root")
set(CMAKE_PREFIX_PATH "${KY_DIMM_QT_ROOT}" CACHE PATH "Qt prefix")
set(pylon_DIR "E:/Softwoare/Basler pylon/Development/CMake/pylon" CACHE PATH "Basler pylon CMake directory")
set(KY_DIMM_PYLON_RUNTIME_ROOT "E:/Softwoare/Basler pylon/Runtime/x64" CACHE PATH "KY-DIMM pylon x64 runtime root")

# Locate the non-debug x64 MSVC runtime shipped with the selected Visual
# Studio instance. This is copied beside KY_DIMM.exe so the test computer does
# not need a separate VC++ redistributable installation just to start the UI.
set(KY_DIMM_MSVC_REDIST_ROOT "" CACHE PATH "KY-DIMM x64 MSVC runtime directory")
if (MSVC AND KY_DIMM_MSVC_REDIST_ROOT STREQUAL "")
    file(GLOB KY_DIMM_MSVC_REDIST_CANDIDATES LIST_DIRECTORIES true
        "${CMAKE_GENERATOR_INSTANCE}/VC/Redist/MSVC/*/x64/Microsoft.VC*CRT")
    if (KY_DIMM_MSVC_REDIST_CANDIDATES)
        list(SORT KY_DIMM_MSVC_REDIST_CANDIDATES COMPARE NATURAL)
        list(REVERSE KY_DIMM_MSVC_REDIST_CANDIDATES)
        list(GET KY_DIMM_MSVC_REDIST_CANDIDATES 0 KY_DIMM_MSVC_REDIST_ROOT)
        set(KY_DIMM_MSVC_REDIST_ROOT "${KY_DIMM_MSVC_REDIST_ROOT}" CACHE PATH
            "KY-DIMM x64 MSVC runtime directory" FORCE)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# The OpenCVConfig.cmake currently installed under opencv4120/lib contains an
# old absolute install prefix (E:/Softwoare/include and E:/Softwoare/x64), so
# find_package(OpenCV) fails before the project can be generated.  Use the
# actual OpenCV installation layout directly and keep both configurations
# available for the Visual Studio multi-config generator.
set(KY_DIMM_OPENCV_ROOT "E:/Softwoare/OpenCV/opencv4120" CACHE PATH "KY-DIMM OpenCV installation root")
set(KY_DIMM_OPENCV_INCLUDE_DIR "${KY_DIMM_OPENCV_ROOT}/include")
set(KY_DIMM_OPENCV_LIB_DIR "${KY_DIMM_OPENCV_ROOT}/lib")
set(KY_DIMM_OPENCV_BIN_DIR "${KY_DIMM_OPENCV_ROOT}/bin")

set(KY_DIMM_OPENCV_DEBUG_LIB "${KY_DIMM_OPENCV_LIB_DIR}/opencv_world4120d.lib")
set(KY_DIMM_OPENCV_RELEASE_LIB "${KY_DIMM_OPENCV_LIB_DIR}/opencv_world4120.lib")
set(KY_DIMM_OPENCV_DEBUG_DLL "${KY_DIMM_OPENCV_BIN_DIR}/opencv_world4120d.dll")
set(KY_DIMM_OPENCV_RELEASE_DLL "${KY_DIMM_OPENCV_BIN_DIR}/opencv_world4120.dll")

foreach(KY_DIMM_OPENCV_PATH
        "${KY_DIMM_OPENCV_INCLUDE_DIR}"
        "${KY_DIMM_OPENCV_DEBUG_LIB}"
        "${KY_DIMM_OPENCV_RELEASE_LIB}"
        "${KY_DIMM_OPENCV_DEBUG_DLL}"
        "${KY_DIMM_OPENCV_RELEASE_DLL}")
    if(NOT EXISTS "${KY_DIMM_OPENCV_PATH}")
        message(FATAL_ERROR
            "KY-DIMM OpenCV installation is incomplete; missing: ${KY_DIMM_OPENCV_PATH}")
    endif()
endforeach()

add_library(KY_DIMM_opencv_world SHARED IMPORTED GLOBAL)
set_target_properties(KY_DIMM_opencv_world PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release"
    IMPORTED_IMPLIB_DEBUG "${KY_DIMM_OPENCV_DEBUG_LIB}"
    IMPORTED_LOCATION_DEBUG "${KY_DIMM_OPENCV_DEBUG_DLL}"
    IMPORTED_IMPLIB_RELEASE "${KY_DIMM_OPENCV_RELEASE_LIB}"
    IMPORTED_LOCATION_RELEASE "${KY_DIMM_OPENCV_RELEASE_DLL}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    INTERFACE_INCLUDE_DIRECTORIES "${KY_DIMM_OPENCV_INCLUDE_DIR}"
)

set(OpenCV_INCLUDE_DIRS "${KY_DIMM_OPENCV_INCLUDE_DIR}")
set(OpenCV_LIBS KY_DIMM_opencv_world)

find_package(pylon 12.2.1 REQUIRED)

set(KY_DIMM_SOURCES
    src/main.cpp
    src/CameraTypes.h
    src/ProcessingTypes.h
    src/AppConfig.h
    src/AppConfig.cpp
    src/FrameQueue.h
    src/FrameQueue.cpp
    src/DisplayMailbox.h
    src/DisplayMailbox.cpp
    src/PylonCamera.h
    src/PylonCamera.cpp
    src/CameraWorker.h
    src/CameraWorker.cpp
    src/CentroidEngine.h
    src/CentroidEngine.cpp
    src/ConnectedDomain.h
    src/StarSegmentation.h
    src/StarSegmentation.cpp
    src/TwoStarTracker.h
    src/TwoStarTracker.cpp
    src/AtmosphereCalculator.h
    src/AtmosphereCalculator.cpp
    src/MeasurementWorker.h
    src/MeasurementWorker.cpp
    src/ResultWriter.h
    src/ResultWriter.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.ui
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui
    src/UiTheme.h
    src/UiTheme.cpp
    src/ImageDisplayAdapter.h
    src/ImageDisplayAdapter.cpp
)

add_executable(KY_DIMM WIN32 ${KY_DIMM_SOURCES})

target_include_directories(KY_DIMM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(KY_DIMM PRIVATE Qt6::Widgets ${OpenCV_LIBS} pylon::pylon)

if (MSVC)
    target_compile_options(KY_DIMM PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(KY_DIMM PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Make build/Release a directly copyable test package. The deployment script
# runs after the selected configuration is linked and collects Qt, OpenCV,
# pylon GigE runtime files, and the MSVC runtime beside KY_DIMM.exe.
if (WIN32)
    set(KY_DIMM_DEPLOY_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/KY_DIMMDeploy.cmake")
    if (NOT EXISTS "${KY_DIMM_DEPLOY_SCRIPT}")
        message(FATAL_ERROR "Missing KY-DIMM deployment script: ${KY_DIMM_DEPLOY_SCRIPT}")
    endif()

    add_custom_command(TARGET KY_DIMM POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DKY_DIMM_TARGET_FILE=$<TARGET_FILE:KY_DIMM>"
            "-DKY_DIMM_CONFIG=$<CONFIG>"
            "-DKY_DIMM_QT_DEPLOY_TOOL=${KY_DIMM_QT_ROOT}/bin/windeployqt.exe"
            "-DKY_DIMM_OPENCV_DEBUG_DLL=${KY_DIMM_OPENCV_DEBUG_DLL}"
            "-DKY_DIMM_OPENCV_RELEASE_DLL=${KY_DIMM_OPENCV_RELEASE_DLL}"
            "-DKY_DIMM_PYLON_RUNTIME_ROOT=${KY_DIMM_PYLON_RUNTIME_ROOT}"
            "-DKY_DIMM_MSVC_REDIST_ROOT=${KY_DIMM_MSVC_REDIST_ROOT}"
            "-DKY_DIMM_INSTRUCTIONS=${CMAKE_CURRENT_SOURCE_DIR}/deploy/KY-DIMM-DEPLOYMENT.txt"
            -P "${KY_DIMM_DEPLOY_SCRIPT}"
        VERBATIM
        COMMENT "Deploying KY-DIMM runtime package"
    )
endif()
