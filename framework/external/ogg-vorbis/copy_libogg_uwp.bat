@echo off

set InputDir=libogg
set OutputDir=..\..

echo.
echo Creating output directories...
if not exist %OutputDir%\include\ogg          mkdir %OutputDir%\include\ogg
if not exist %OutputDir%\lib\x64_uwp\libogg  mkdir %OutputDir%\lib\x64_uwp\libogg
if not exist %OutputDir%\lib\x64d_uwp\libogg mkdir %OutputDir%\lib\x64d_uwp\libogg

echo.
echo Copying includes...
copy %InputDir%\include\ogg\* %OutputDir%\include\ogg

echo.
echo Copying LIBs...
copy "%InputDir%\win32\VS2015\x64\Debug\libogg\libogg.lib" %OutputDir%\lib\x64d_uwp\libogg
copy "%InputDir%\win32\VS2015\x64\Release\libogg\libogg.lib" %OutputDir%\lib\x64_uwp\libogg

:: Done
echo.
if "%1"=="" pause
