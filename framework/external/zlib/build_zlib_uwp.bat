@echo on

call ../get_msbuild_path.bat

@echo.
@echo.
@echo === Building zlib + minizip for UWP x64 ===

pushd _vstudio
%msbuildPath% zlib.sln /target:zlib /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDebugDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% zlib.sln /target:minizip /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDebugDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% zlib.sln /target:zlib /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% zlib.sln /target:minizip /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
popd

call copy_zlib_uwp.bat no_pause

:: Done
echo.
if "%1"=="" pause
