from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_cmake_defines_self_contained_runtime_deployment():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    deploy_script = (ROOT / "cmake" / "KY_DIMMDeploy.cmake").read_text(encoding="utf-8")

    assert "windeployqt.exe" in cmake
    assert "--release" in deploy_script
    assert "--compiler-runtime" in deploy_script
    assert "KY_DIMM_OPENCV_RELEASE_DLL" in cmake
    assert "KY_DIMM_PYLON_RUNTIME_ROOT" in cmake
    assert "KY_DIMM_MSVC_REDIST_ROOT" in cmake
    assert "ProducerGEV.cti" in deploy_script
    assert "msvcp140.dll" in deploy_script
    assert "vcruntime140.dll" in deploy_script
    assert "vcruntime140_1.dll" in deploy_script
    assert "add_custom_command(TARGET KY_DIMM POST_BUILD" in cmake
    assert "KY-DIMM-DEPLOYMENT.txt" in cmake


def test_deployment_instructions_exist():
    instructions = ROOT / "deploy" / "KY-DIMM-DEPLOYMENT.txt"

    assert instructions.is_file()
    content = instructions.read_text(encoding="utf-8")
    assert "KY_DIMM.exe" in content
    assert "pylon" in content.lower()
    assert "Mono8" in content
