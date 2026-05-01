@echo off

set InputDir=.
set OutputDir=..\..

echo.
echo Creating output directories...
if not exist %OutputDir%\include\zlib           mkdir %OutputDir%\include\zlib
if not exist %OutputDir%\lib\x64_uwp\zlib      mkdir %OutputDir%\lib\x64_uwp\zlib
if not exist %OutputDir%\lib\x64d_uwp\zlib     mkdir %OutputDir%\lib\x64d_uwp\zlib
if not exist %OutputDir%\include\minizip       mkdir %OutputDir%\include\minizip
if not exist %OutputDir%\lib\x64_uwp\minizip   mkdir %OutputDir%\lib\x64_uwp\minizip
if not exist %OutputDir%\lib\x64d_uwp\minizip  mkdir %OutputDir%\lib\x64d_uwp\minizip

echo.
echo Copying includes...
copy %InputDir%\zlib\*.h %OutputDir%\include\zlib
copy %InputDir%\zlib\contrib\minizip\*.h %OutputDir%\include\minizip

echo.
echo Copying LIBs...
copy %InputDir%\lib\Debug_x64\zlib.lib %OutputDir%\lib\x64d_uwp\zlib
copy %InputDir%\lib\Release_x64\zlib.lib %OutputDir%\lib\x64_uwp\zlib
copy %InputDir%\lib\Debug_x64\minizip.lib %OutputDir%\lib\x64d_uwp\minizip
copy %InputDir%\lib\Release_x64\minizip.lib %OutputDir%\lib\x64_uwp\minizip

:: Done
echo.
if "%1"=="" pause
