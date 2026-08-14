cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED KY_DIMM_TARGET_FILE OR NOT EXISTS "${KY_DIMM_TARGET_FILE}")
    message(FATAL_ERROR "KY-DIMM deployment target does not exist: ${KY_DIMM_TARGET_FILE}")
endif()

get_filename_component(KY_DIMM_DEPLOY_DIR "${KY_DIMM_TARGET_FILE}" DIRECTORY)
get_filename_component(KY_DIMM_TARGET_NAME "${KY_DIMM_TARGET_FILE}" NAME)

if(NOT DEFINED KY_DIMM_CONFIG OR KY_DIMM_CONFIG STREQUAL "")
    set(KY_DIMM_CONFIG "Release")
endif()

function(kydimm_copy_runtime_file source_path)
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "Missing KY-DIMM runtime file: ${source_path}")
    endif()

    get_filename_component(file_name "${source_path}" NAME)
    file(COPY_FILE "${source_path}" "${KY_DIMM_DEPLOY_DIR}/${file_name}" ONLY_IF_DIFFERENT)
endfunction()

if(NOT DEFINED KY_DIMM_QT_DEPLOY_TOOL OR NOT EXISTS "${KY_DIMM_QT_DEPLOY_TOOL}")
    message(FATAL_ERROR "windeployqt.exe was not found: ${KY_DIMM_QT_DEPLOY_TOOL}")
endif()

execute_process(
    COMMAND "${KY_DIMM_QT_DEPLOY_TOOL}"
        --release
        --compiler-runtime
        --no-translations
        --no-system-d3d-compiler
        "${KY_DIMM_TARGET_FILE}"
    WORKING_DIRECTORY "${KY_DIMM_DEPLOY_DIR}"
    RESULT_VARIABLE qt_result
    OUTPUT_VARIABLE qt_output
    ERROR_VARIABLE qt_error
)
if(NOT qt_result EQUAL 0)
    message(FATAL_ERROR
        "windeployqt failed with code ${qt_result}\n${qt_output}\n${qt_error}")
endif()

if(KY_DIMM_CONFIG STREQUAL "Debug")
    set(KY_DIMM_OPENCV_DLL "${KY_DIMM_OPENCV_DEBUG_DLL}")
else()
    set(KY_DIMM_OPENCV_DLL "${KY_DIMM_OPENCV_RELEASE_DLL}")
endif()
kydimm_copy_runtime_file("${KY_DIMM_OPENCV_DLL}")

if(NOT DEFINED KY_DIMM_PYLON_RUNTIME_ROOT OR
   NOT IS_DIRECTORY "${KY_DIMM_PYLON_RUNTIME_ROOT}")
    message(FATAL_ERROR
        "pylon x64 runtime directory was not found: ${KY_DIMM_PYLON_RUNTIME_ROOT}")
endif()

# KY-DIMM uses the native pylon API and the GigE transport layer. The CTI file
# is required by pylon to discover and open the acA1920-40gm camera.
set(KY_DIMM_PYLON_RUNTIME_FILES
    Basler.Pylon.ExternC.dll
    GCBase_MD_VC142_v3_5_Basler_pylon_v1.dll
    GenApi_MD_VC142_v3_5_Basler_pylon_v1.dll
    gxapi_v16.dll
    log4cpp_MD_VC142_v3_5_Basler_pylon_v1.dll
    Log_MD_VC142_v3_5_Basler_pylon_v1.dll
    MathParser_MD_VC142_v3_5_Basler_pylon_v1.dll
    NodeMapData_MD_VC142_v3_5_Basler_pylon_v1.dll
    PylonBase_v12.dll
    PylonGigE_v12_TL.dll
    PylonGtc_v12_TL.dll
    PylonUtility_v12.dll
    XmlParser_MD_VC142_v3_5_Basler_pylon_v1.dll
    ProducerGEV.cti
)

foreach(pylon_file IN LISTS KY_DIMM_PYLON_RUNTIME_FILES)
    kydimm_copy_runtime_file("${KY_DIMM_PYLON_RUNTIME_ROOT}/${pylon_file}")
endforeach()

if(NOT DEFINED KY_DIMM_MSVC_REDIST_ROOT OR
   NOT IS_DIRECTORY "${KY_DIMM_MSVC_REDIST_ROOT}")
    message(FATAL_ERROR
        "x64 MSVC runtime directory was not found: ${KY_DIMM_MSVC_REDIST_ROOT}")
endif()

foreach(msvc_file IN ITEMS msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
    kydimm_copy_runtime_file("${KY_DIMM_MSVC_REDIST_ROOT}/${msvc_file}")
endforeach()

if(NOT DEFINED KY_DIMM_INSTRUCTIONS OR NOT EXISTS "${KY_DIMM_INSTRUCTIONS}")
    message(FATAL_ERROR "Missing KY-DIMM deployment instructions: ${KY_DIMM_INSTRUCTIONS}")
endif()
kydimm_copy_runtime_file("${KY_DIMM_INSTRUCTIONS}")

message(STATUS "KY-DIMM ${KY_DIMM_CONFIG} deployment package ready: ${KY_DIMM_DEPLOY_DIR}")
