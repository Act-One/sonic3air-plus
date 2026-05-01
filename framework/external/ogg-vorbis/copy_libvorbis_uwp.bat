@echo off

set InputDir=libvorbis
set OutputDir=..\..

echo.
echo Creating output directories...
if not exist %OutputDir%\include\vorbis          mkdir %OutputDir%\include\vorbis
if not exist %OutputDir%\lib\x64_uwp\libvorbis   mkdir %OutputDir%\lib\x64_uwp\libvorbis
if not exist %OutputDir%\lib\x64d_uwp\libvorbis  mkdir %OutputDir%\lib\x64d_uwp\libvorbis

echo.
echo Copying includes...
copy %InputDir%\include\vorbis\* %OutputDir%\include\vorbis

echo.
echo Copying LIBs...
copy "%InputDir%\win32\VS2010\libvorbis\x64\Debug\libvorbis_static.lib" %OutputDir%\lib\x64d_uwp\libvorbis
copy "%InputDir%\win32\VS2010\libvorbisfile\x64\Debug\libvorbisfile_static.lib" %OutputDir%\lib\x64d_uwp\libvorbis
copy "%InputDir%\win32\VS2010\x64\Release\libvorbis_static.lib" %OutputDir%\lib\x64_uwp\libvorbis
copy "%InputDir%\win32\VS2010\x64\Release\libvorbisfile_static.lib" %OutputDir%\lib\x64_uwp\libvorbis

:: Done
echo.
if "%1"=="" pause
