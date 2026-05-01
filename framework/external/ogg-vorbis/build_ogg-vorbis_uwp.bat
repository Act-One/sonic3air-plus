@echo on

call ../get_msbuild_path.bat

@echo.
@echo.
@echo === Building libogg + libvorbis for UWP x64 ===

pushd libogg\win32\VS2015
%msbuildPath% libogg.sln /target:libogg /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDebugDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% libogg.sln /target:libogg /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
popd

pushd libvorbis\win32\VS2010
%msbuildPath% vorbis_static.sln /target:libvorbis_static /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDebugDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% vorbis_static.sln /target:libvorbisfile /property:Configuration=Debug /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDebugDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% vorbis_static.sln /target:libvorbis_static /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
%msbuildPath% vorbis_static.sln /target:libvorbisfile /property:Configuration=Release /property:Platform=x64 /property:PlatformToolset=v145 /property:RuntimeLibrary=MultiThreadedDLL /property:ApplicationType="Windows Store" /property:ApplicationTypeRevision=10.0 /property:AppContainerApplication=true /property:WindowsTargetPlatformVersion=10.0.26100.0 /property:WindowsTargetPlatformMinVersion=10.0.19041.0 -verbosity:minimal
popd

call copy_libogg_uwp.bat no_pause
call copy_libvorbis_uwp.bat no_pause

:: Done
echo.
if "%1"=="" pause
