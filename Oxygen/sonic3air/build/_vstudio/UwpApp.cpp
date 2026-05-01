#define SDL_MAIN_HANDLED
#include <SDL_main.h>

#include "UwpBootTrace.h"

#include "../../../../framework/external/sdl/SDL2/src/core/winrt/SDL_winrtapp_common.h"
#include "../../../../framework/external/sdl/SDL2/src/core/winrt/SDL_winrtapp_direct3d.h"

using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::ViewManagement;

namespace Sonic3AIRUWP
{
	public ref class App sealed : IFrameworkViewSource
	{
	public:
		virtual IFrameworkView^ CreateView();
	};
}

Windows::ApplicationModel::Core::IFrameworkView^ Sonic3AIRUWP::App::CreateView()
{
	s3air::uwp::appendBootTrace(L"UwpApp", L"CreateView");
	return ref new SDL_WinRTApp();
}

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
	s3air::uwp::appendBootTrace(L"UwpApp", L"main enter");
	WINRT_SDLAppEntryPoint = SDL_main;
	ApplicationView::PreferredLaunchWindowingMode = ApplicationViewWindowingMode::FullScreen;
	s3air::uwp::appendBootTrace(L"UwpApp", L"PreferredLaunchWindowingMode=FullScreen");
	try
	{
		s3air::uwp::appendBootTrace(L"UwpApp", L"CoreApplication::Run begin");
		CoreApplication::Run(ref new Sonic3AIRUWP::App());
		s3air::uwp::appendBootTrace(L"UwpApp", L"CoreApplication::Run returned");
	}
	catch (Platform::Exception^ e)
	{
		s3air::uwp::appendBootTrace(L"UwpApp", std::wstring(L"Platform::Exception: ") + e->Message->Data());
		throw;
	}
	catch (...)
	{
		s3air::uwp::appendBootTrace(L"UwpApp", L"Unknown exception from CoreApplication::Run");
		throw;
	}
	s3air::uwp::appendBootTrace(L"UwpApp", L"main exit");
	return 0;
}
