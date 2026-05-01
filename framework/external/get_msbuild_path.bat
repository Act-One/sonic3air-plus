@echo off

set msbuildPath=

for %%P in (
	"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
	"C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"
	"C:\Program Files\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
	"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
	"C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
	"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
) do (
	if not defined msbuildPath if exist %%P set msbuildPath=%%P
)

if not defined msbuildPath (
	echo Error: No supported Visual Studio MSBuild installation found
	pause
	exit /b 1
)

if not exist %msbuildPath% (
	echo Error: No supported Visual Studio MSBuild installation found
	pause
	exit /b 1
)

echo Using Visual Studio installation: %msbuildPath%
echo.
