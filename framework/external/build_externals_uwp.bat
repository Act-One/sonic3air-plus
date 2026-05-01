@echo on

pushd sdl
call build_sdl_uwp.bat no_pause
popd

pushd zlib
call build_zlib_uwp.bat no_pause
popd

pushd ogg-vorbis
call build_ogg-vorbis_uwp.bat no_pause
popd

:: Done
echo.
if "%1"=="" pause
