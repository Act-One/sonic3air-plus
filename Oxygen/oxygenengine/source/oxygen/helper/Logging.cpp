/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/simulation/LemonScriptRuntime.h"

#if defined(PLATFORM_WIIU)
	#include <cstdio>
	#include <ctime>
	#include <whb/log_cafe.h>
	#include <whb/log_udp.h>
#endif


namespace
{

	class ErrorLogger : public rmx::ErrorHandling::LoggerInterface, public rmx::ErrorHandling::MessageBoxInterface
	{
	public:
		std::string mCaption;

	protected:
		void logMessage(rmx::ErrorSeverity errorSeverity, const std::string& message) override
		{
			switch (errorSeverity)
			{
				default:
				case rmx::ErrorSeverity::INFO:	  rmx::Logging::log(rmx::LogLevel::INFO, message);	  break;
				case rmx::ErrorSeverity::WARNING: rmx::Logging::log(rmx::LogLevel::WARNING, message); break;
				case rmx::ErrorSeverity::ERROR:	  rmx::Logging::log(rmx::LogLevel::ERROR, message);	  break;
			}
		}

		Result showMessageBox(rmx::ErrorHandling::MessageBoxInterface::DialogType dialogType, rmx::ErrorSeverity errorSeverity, const std::string& message, const char* filename, int line) override
		{
			std::string text = message;
		#if defined(DEBUG)
			if (nullptr != filename)
			{
				std::string name;
				std::string ext;
				rmx::FileIO::splitPath(filename, nullptr, &name, &ext);
				text = message + "\n[" + name + "." + ext + ", line " + std::to_string(line) + "]";
			}
		#endif

			// Check if it was caused inside a script function
			{
				std::string_view functionName;
				std::wstring fileName;
				uint32 lineNumber = 0;
				std::string moduleName;
				if (LemonScriptRuntime::getCurrentScriptFunction(&functionName, &fileName, &lineNumber, &moduleName))
				{
					text += "\n\nCaused during script execution in function '" + std::string(functionName) + "' at line " + std::to_string(lineNumber) + " of file '" + WString(fileName).toStdString() + "' in module '" + moduleName + "'.";
				}
			}

			std::string caption = (errorSeverity == rmx::ErrorSeverity::ERROR) ? "Error" : "Warning";
			if (!mCaption.empty())
			{
				caption = mCaption + " - " + caption;
			}

			if (rmx::ErrorHandling::isDebuggerAttached())
			{
				text += "\n\nBreak here?";
			}

			PlatformFunctions::DialogButtons dialogButtons = PlatformFunctions::DialogButtons::OK_CANCEL;
			switch (dialogType)
			{
				case rmx::ErrorHandling::MessageBoxInterface::DialogType::OK:			 dialogButtons = PlatformFunctions::DialogButtons::OK;			  break;
				case rmx::ErrorHandling::MessageBoxInterface::DialogType::OK_CANCEL:	 dialogButtons = PlatformFunctions::DialogButtons::OK_CANCEL;	  break;
				case rmx::ErrorHandling::MessageBoxInterface::DialogType::YES_NO_CANCEL: dialogButtons = PlatformFunctions::DialogButtons::YES_NO_CANCEL; break;
			}
			const PlatformFunctions::DialogResult result = PlatformFunctions::showDialogBox(errorSeverity, dialogButtons, caption, text);
			return (result == PlatformFunctions::DialogResult::CANCEL) ? Result::IGNORE : (result == PlatformFunctions::DialogResult::OK) ? Result::ACCEPT : Result::ABORT;
		}
	};

	static ErrorLogger mErrorLogger;

#if defined(PLATFORM_WIIU)
	class WiiULogFileLogger final : public rmx::LoggerBase
	{
	public:
		WiiULogFileLogger()
		{
			static const char* paths[] =
			{
				"/vol/external01/wiiu/apps/sonic3air/logfile.txt",
				"/vol/external01/wiiu/apps/sonic3air/savedata/logfile.txt",
				"logfile.txt",
			};

			for (const char* path : paths)
			{
				std::FILE* file = std::fopen(path, "wb");
				if (nullptr != file)
				{
					std::fclose(file);
					mPath = path;
					writeLine("Wii U direct file logger opened");
					break;
				}
			}
		}

		const char* getPath() const
		{
			return (nullptr != mPath) ? mPath : "<failed>";
		}

	protected:
		void log(rmx::LogLevel logLevel, const std::string& string) override
		{
			(void)logLevel;
			writeLine(string.c_str());
		}

	private:
		void writeLine(const char* text)
		{
			if (nullptr == mPath)
				return;

			std::FILE* file = std::fopen(mPath, "ab");
			if (nullptr == file)
				return;

			char timestamp[48] = {};
			const std::time_t now = std::time(nullptr);
			const std::tm* tm = std::localtime(&now);
			if (nullptr != tm)
			{
				std::strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", tm);
			}

			std::fputs(timestamp, file);
			std::fputs(text, file);
			std::fputs("\r\n", file);
			std::fflush(file);
			std::fclose(file);
		}

	private:
		const char* mPath = nullptr;
	};
#endif
}



namespace oxygen
{
	void Logging::startup(const std::wstring& filename)
	{
#if defined(PLATFORM_WIIU)
		WHBLogCafeInit();
		WHBLogUdpInit();
		WiiULogFileLogger* fileLogger = new WiiULogFileLogger();
		rmx::Logging::addLogger(*fileLogger);
		rmx::Logging::addLogger(*new rmx::StdCoutLogger());
		(void)filename;
		rmx::Logging::log(rmx::LogLevel::INFO, std::string("Wii U logging to ") + fileLogger->getPath());
#else
		rmx::Logging::addLogger(*new rmx::StdCoutLogger());
		rmx::Logging::addLogger(*new rmx::FileLogger(filename, true));
#endif

		// Register as logger and message box callback for rmx error handling
		rmx::ErrorHandling::mLogger = &mErrorLogger;
		rmx::ErrorHandling::mMessageBoxImplementation = &mErrorLogger;
	}

	void Logging::shutdown()
	{
#if defined(PLATFORM_WIIU)
		// On Wii U this can run after ProcUI has already started handing control
		// back to the shell. Leave the loggers alive for process teardown so the
		// final return path stays observable and does not hang in WHB log deinit.
		rmx::Logging::log(rmx::LogLevel::INFO, "Wii U logging left active for process exit");
#else
		rmx::Logging::clear();
#endif
	}

	void Logging::setAssertBreakCaption(const std::string& caption)
	{
		::mErrorLogger.mCaption = caption;
	}
}
