# KY-DIMM 构建交接

本文只记录当前 `UI_pylon` 项目的 Windows x64 / Qt 6 / MSVC Release 构建流程。

## 项目与工具

- 项目目录：`E:\Softwoare\visual studio\project\UI\UI_pylon`
- 源码：`src/`
- 测试：`tests/`
- 构建目录：`build/`
- Release 程序：`build\Release\KY_DIMM.exe`
- 可分发目录：`build\Release\`

固定工具路径：

```text
VS18：C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat
MSBuild：C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
CMake：E:\Softwoare\cmake18\bin\cmake.exe
Qt：E:\Softwoare\Qtool\qt\6.11.0\msvc2022_64
OpenCV：E:\Softwoare\OpenCV\opencv4120
pylon SDK：E:\Softwoare\Basler pylon\Development
pylon x64 运行库：E:\Softwoare\Basler pylon\Runtime\x64
```

必须使用 VS18 x64 MSVC。不要混用 MinGW、Qt MinGW、Debug DLL 或其他 CMake 生成器。

## 构建前

```powershell
git status --short --branch
```

保留已有修改和未跟踪文件。构建时不要执行以下操作：

- `git reset --hard` 或 `git checkout -- ...`
- 删除整个 `build/`
- 删除用户未跟踪文件
- 用 `UI_2` 的源码覆盖当前 `src/`

## 常规 Release 构建

优先复用现有 `build`，在 PowerShell 中执行：

```powershell
$vsdev = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
$cmake = 'E:\Softwoare\cmake18\bin\cmake.exe'
$line = 'set "PATH=" && call "' + $vsdev + '" -arch=x64 -host_arch=x64 && "' + $cmake + '" --build build --config Release --target KY_DIMM -- /m:1'
cmd.exe /d /c $line
```

只有在修改 `CMakeLists.txt`、增加源文件、修改生成器/平台或确认缓存损坏时，才重新配置：

```powershell
$vsdev = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
$cmake = 'E:\Softwoare\cmake18\bin\cmake.exe'
$line = 'set "PATH=" && call "' + $vsdev + '" -arch=x64 -host_arch=x64 && "' + $cmake + '" -S . -B build -G "Visual Studio 18 2026" -A x64 -T host=x64'
cmd.exe /d /c $line
```

配置完成后，再执行上面的常规构建命令。

## 构建卡住时

先检查是否确实有本次构建残留：

```powershell
Get-Process cmake,msbuild,cl,link,uic,moc -ErrorAction SilentlyContinue |
    Select-Object ProcessName,Id,CPU,StartTime,Responding
```

只结束确认属于本次构建的进程，不要结束无关进程：

```powershell
Get-Process cmake,msbuild,cl,link -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
```

如果 CMake 卡在 `Automatic MOC and UIC` 或重配置阶段，可手动生成 Qt 文件，再绕过 CMake 驱动直接构建 VS 工程：

```powershell
$cmake = 'E:\Softwoare\cmake18\bin\cmake.exe'
& $cmake -E cmake_autogen 'build\CMakeFiles\KY_DIMM_autogen.dir\AutogenInfo.json' Release
```

```powershell
$vsdev = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
$line = 'set "PATH=" && call "' + $vsdev + '" -arch=x64 -host_arch=x64 && "' + $msbuild + '" build\KY_DIMM.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal /p:PreBuildEventUseInBuild=false /p:TrackFileAccess=false /p:LinkIncremental=false'
cmd.exe /d /c $line
```

如果工程对象列表过期或链接缺少应有的 `.obj`，将 `/t:Build` 改成 `/t:Rebuild`。不要因为构建失败就删除 `build/`。

## 测试与 UI 检查

优先运行：

```powershell
python -m pytest -q
```

没有 pytest 时，可运行仓库内每个 `test_*` 函数的契约测试后备脚本；交付说明中必须注明实际使用的测试命令。

涉及 `.ui` 文件时：

```powershell
$uic = 'E:\Softwoare\Qtool\qt\6.11.0\msvc2022_64\bin\uic.exe'
$out = Join-Path $env:TEMP 'ui_SettingsDialog_verify.h'
& $uic src\SettingsDialog.ui -o $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

注意：当前 `processingConnectivityCombo` 的 4/8 语义值在 `SettingsDialog.cpp` 中显式绑定；不要只依赖 `.ui` 中的 `userData`。

## Release 验证

MSBuild 返回 0 后检查程序时间戳、哈希和运行库：

```powershell
$exe = Get-Item -LiteralPath 'build\Release\KY_DIMM.exe'
Write-Output ('exe=' + $exe.FullName)
Write-Output ('length=' + $exe.Length)
Write-Output ('lastWrite=' + $exe.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
Write-Output ('sha256=' + (Get-FileHash $exe.FullName -Algorithm SHA256).Hash)

$required = @(
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'opencv_world4120.dll',
    'PylonBase_v12.dll', 'PylonGigE_v12_TL.dll', 'PylonGtc_v12_TL.dll',
    'PylonUtility_v12.dll', 'ProducerGEV.cti', 'vcruntime140.dll',
    'vcruntime140_1.dll', 'msvcp140.dll', 'KY-DIMM-DEPLOYMENT.txt'
)
foreach ($name in $required) {
    Write-Output ($name + '=' + (Test-Path (Join-Path 'build\Release' $name)))
}
```

部署时复制整个 `build\Release` 目录，不要只复制 exe，也不要混用 `build\Debug` 的 DLL。部署逻辑在 `cmake\KY_DIMMDeploy.cmake`，说明在 `deploy\KY-DIMM-DEPLOYMENT.txt`。

无相机启动冒烟检查：

```powershell
$exe = Get-Item -LiteralPath 'build\Release\KY_DIMM.exe'
$process = Start-Process $exe.FullName -WorkingDirectory $exe.DirectoryName -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
if ($process.HasExited) { exit 1 }
Write-Output ('startup=alive pid ' + $process.Id)
Stop-Process -Id $process.Id -Force
Write-Output 'startup_cleanup=stopped'
```

这只能验证程序启动和运行库加载，不能替代真实相机采集和测量验证。

## 已知非阻塞警告

- `TwoStarTracker.cpp`、`AtmosphereCalculator.cpp` 中已有 C4189 未使用变量警告。
- `ui_SettingsDialog.h` 中已有 C4125 八进制转义警告。

交付前至少确认：工作区状态已检查、相关测试通过、`uic`（如适用）通过、Release 构建返回 0、exe 时间戳更新、Release 运行库齐全、启动冒烟检查通过。
