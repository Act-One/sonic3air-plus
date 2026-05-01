#pragma once

#if defined(PLATFORM_UWP) && defined(_DEBUG)

#include <windows.h>
#include <cstdio>
#include <string>
#include <sstream>

#include <winrt/Windows.Storage.h>

namespace s3air
{
	namespace uwp
	{
		inline std::wstring getBootTracePath()
		{
			try
			{
				std::wstring path = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
				if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
					path += L'\\';
				path += L"uwp_boot_trace.txt";
				return path;
			}
			catch (...)
			{
				return L"";
			}
		}

		inline void appendBootTrace(const wchar_t* source, const std::wstring& message)
		{
			std::wstringstream stream;
			stream << L"[" << GetTickCount64() << L"][" << source << L"] " << message << L"\r\n";
			const std::wstring line = stream.str();
			OutputDebugStringW(line.c_str());

			const std::wstring path = getBootTracePath();
			if (path.empty())
				return;

			FILE* file = nullptr;
			if (_wfopen_s(&file, path.c_str(), L"a+, ccs=UNICODE") != 0 || nullptr == file)
				return;

			fputws(line.c_str(), file);
			fflush(file);
			fclose(file);
		}

		inline void appendBootTrace(const wchar_t* source, const wchar_t* message)
		{
			appendBootTrace(source, std::wstring(message));
		}
	}
}

#else

namespace s3air
{
	namespace uwp
	{
		inline void appendBootTrace(const wchar_t*, const std::wstring&) {}
		inline void appendBootTrace(const wchar_t*, const wchar_t*) {}
	}
}

#endif
