@echo on

call ../get_msbuild_path.bat

@echo.
@echo.
@echo === Building SDL2 for UWP x64 ===

pushd SDL2\VisualC-WinRT
%msbuildPath% SDL-UWP.vcxproj /target:Build /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% SDL-UWP.vcxproj /target:Build /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
popd

call copy_sdl_uwp.bat no_pause

:: Done
echo.
if "%1"=="" pause
