@echo off
setlocal

set CONFIG=ReleaseUWP
set BUILD_EXTERNALS=1

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="debug" (
	set CONFIG=DebugUWP
) else if /I "%~1"=="release" (
	set CONFIG=ReleaseUWP
) else if /I "%~1"=="skip_externals" (
	set BUILD_EXTERNALS=0
) else if /I "%~1"=="no_pause" (
	set NO_PAUSE=1
) else (
	echo Unknown argument: %~1
	echo Usage: build_xbox_uwp.bat [debug^|release] [skip_externals] [no_pause]
	exit /b 1
)
shift
goto parse_args

:args_done
pushd "%~dp0..\..\..\.."
set ROOT_DIR=%CD%

call framework\external\get_msbuild_path.bat
if errorlevel 1 goto fail

if "%BUILD_EXTERNALS%"=="1" (
	pushd framework\external
	call build_externals_uwp.bat no_pause
	set EXTERNALS_ERR=%ERRORLEVEL%
	popd
	if not "%EXTERNALS_ERR%"=="0" (
		exit /b %EXTERNALS_ERR%
	)
	if errorlevel 1 goto fail
)

pushd "Oxygen\sonic3air\build\_vstudio"
%msbuildPath% sonic3air.sln /m:1 /nologo /v:minimal /p:Configuration=%CONFIG%;Platform=x64
if errorlevel 1 goto build_fail
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -File "sign_xbox_package.ps1" -Configuration %CONFIG%
if errorlevel 1 goto build_fail
popd

echo.
echo Xbox UWP package output:
if /I "%CONFIG%"=="ReleaseUWP" (
	echo   Oxygen\sonic3air\build\_vstudio\AppPackages\sonic3air\sonic3air_1.0.0.0_x64_ReleaseUWP_Test\
	echo   Signed package: sonic3air_1.0.0.0_x64_ReleaseUWP.msix
	echo   Dependency: Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx
	echo   Remote deploy to Xbox: powershell -ExecutionPolicy Bypass -File Oxygen\sonic3air\build\_vstudio\deploy_xbox_uwp.ps1
	echo   Default ROM upload source: %%USERPROFILE%%\Downloads\Sonic_Knuckles_wSonic3.bin
) else (
	echo   Oxygen\sonic3air\build\_vstudio\AppPackages\sonic3air\sonic3air_1.0.0.0_x64_DebugUWP_Test\
	echo   Signed package: sonic3air_1.0.0.0_x64_DebugUWP.msix
	echo   Dependency: Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx
	echo   Remote deploy to Xbox: powershell -ExecutionPolicy Bypass -File Oxygen\sonic3air\build\_vstudio\deploy_xbox_uwp.ps1 -Configuration DebugUWP
	echo   Default ROM upload source: %%USERPROFILE%%\Downloads\Sonic_Knuckles_wSonic3.bin
)
echo.
echo See Oxygen\sonic3air\build\_vstudio\XboxDevModeUWP.md for LocalFolder layout and install notes.
goto done

:build_fail
set ERR=%ERRORLEVEL%
popd
goto fail_with_code

:fail
set ERR=%ERRORLEVEL%

:fail_with_code
if /I not "%CD%"=="%ROOT_DIR%" popd
popd
if not defined NO_PAUSE pause
exit /b %ERR%

:done
popd
if not defined NO_PAUSE pause
exit /b 0
