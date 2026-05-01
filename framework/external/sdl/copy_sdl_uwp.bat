@echo off

set InputDir=SDL2
set OutputDir=..\..

echo.
echo Creating output directories...
if not exist %OutputDir%\include\sdl          mkdir %OutputDir%\include\sdl
if not exist %OutputDir%\lib\x64_uwp\sdl     mkdir %OutputDir%\lib\x64_uwp\sdl
if not exist %OutputDir%\lib\x64d_uwp\sdl    mkdir %OutputDir%\lib\x64d_uwp\sdl

echo.
echo Copying includes...
copy %InputDir%\include\* %OutputDir%\include\sdl

echo.
echo Copying LIBs...
if exist %OutputDir%\lib\x64d_uwp\sdl\SDL2.dll del %OutputDir%\lib\x64d_uwp\sdl\SDL2.dll
if exist %OutputDir%\lib\x64_uwp\sdl\SDL2.dll del %OutputDir%\lib\x64_uwp\sdl\SDL2.dll
copy %InputDir%\VisualC-WinRT\x64\Debug\SDL-UWP\SDL2.lib %OutputDir%\lib\x64d_uwp\sdl
copy %InputDir%\VisualC-WinRT\x64\Release\SDL-UWP\SDL2.lib %OutputDir%\lib\x64_uwp\sdl

:: Done
echo.
if "%1"=="" pause
