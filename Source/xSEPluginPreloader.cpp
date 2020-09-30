#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "VectoredExceptionHandler.h"
#include "ScriptExtenderDefinesBase.h"
#include <kxf/FileSystem/NativeFileSystem.h>
#include <kxf/IO/StreamReaderWriter.h>
#include <kxf/System/ShellOperations.h>
#include <kxf/System/Win32Error.h>
#include <kxf/System/NtStatus.h>
#include <kxf/Utility/Container.h>
#include <kxf/Utility/CallAtScopeExit.h>

#ifdef USE_NUKEM_DETOURS
#include "Nukem Detours/Detours.h"
#if _WIN64

namespace NukemDetours = Detours::X64;
using NukemDetoursOpt = Detours::X64Option;
#pragma comment(lib, "Nukem Detours/detours x64.lib")

#else

namespace NukemDetours = Detours::X86;
using NukemDetoursOpt = Detours::X86Option;
#pragma comment(lib, "Nukem Detours/detours x86.lib")

#endif
#endif

namespace
{
	std::unique_ptr<xSE::PreloadHandler> g_Instnace;

	using TInitialize = void(__cdecl*)(void);
	constexpr auto g_ConfigFileName = wxS("xSE PluginPreloader.xml");
	constexpr auto g_LogFileName = wxS("xSE PluginPreloader.log");

	template<class TFunc>
	__declspec(noinline) uint32_t SEHTryExcept(TFunc&& func)
	{
		__try
		{
			std::invoke(func);
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return ::GetExceptionCode();
		}
	}

	void LogLoadStatus(const kxf::FSPath& path, xSE::PluginStatus status)
	{
		using namespace xSE;

		switch (status)
		{
			case PluginStatus::Loaded:
			{
				g_Instnace->LogIndent(1, wxS("Plugin '%1' loaded successfully"), path.GetFullPath());
				break;
			}
			case PluginStatus::Initialized:
			{
				g_Instnace->LogIndent(1, wxS("Plugin '%1' loaded successfully, 'Initialize' executed successfully"), path.GetFullPath());
				break;
			}
			case PluginStatus::FailedLoad:
			{
				g_Instnace->LogIndent(1, wxS("Plugin '%1' failed to load"), path.GetFullPath());
				break;
			}
			case PluginStatus::FailedInitialize:
			{
				g_Instnace->LogIndent(1, wxS("Plugin '%1' failed to load. Exception was thrown during 'Initialize' execution"), path.GetFullPath());
				break;
			}
			default:
			{
				g_Instnace->LogIndent(1, wxS("Unknown plugin status code (%1): '%2"), static_cast<uint32_t>(status), path.GetFullPath());
				break;
			}
		};
	}
}

//////////////////////////////////////////////////////////////////////////
namespace xSE
{
	kxf::String PreloadHandler::GetLibraryName()
	{
		return wxS("xSE PluginPreloader");
	}
	kxf::String PreloadHandler::GetLibraryVersion()
	{
		return wxS("0.2");
	}

	PreloadHandler& PreloadHandler::CreateInstnace()
	{
		if (!g_Instnace)
		{
			g_Instnace = std::make_unique<xSE::PreloadHandler>();
		}
		return *g_Instnace;
	}
	void PreloadHandler::DestroyInstnace()
	{
		g_Instnace = nullptr;
	}
	PreloadHandler& PreloadHandler::GetInstance()
	{
		return *g_Instnace;
	}
	bool PreloadHandler::HasInstance()
	{
		return g_Instnace != nullptr;
	}

	kxf::FSPath PreloadHandler::GetOriginalLibraryPath() const
	{
		if (m_OriginalLibraryPath)
		{
			return m_OriginalLibraryPath;
		}
		return GetOriginalLibraryDefaultPath();
	}
	kxf::FSPath PreloadHandler::GetOriginalLibraryDefaultPath() const
	{
		#if xSE_PLATFORM_F4SE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / wxS("IpHlpAPI.dll");
		#elif xSE_PLATFORM_NVSE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / wxS("WinMM.dll");
		#endif
	}

	void PreloadHandler::DoLoadPlugins()
	{
		Log(wxS("Searching directory '%1' for plugins"), m_PluginsDirectory.GetFullPath());

		// Install exception handler and remove it after loading is done
		VectoredExceptionHandler vectoredExceptionHandler;
		vectoredExceptionHandler.Install([](_EXCEPTION_POINTERS* exceptionInfo) -> LONG
		{
			if (g_Instnace && exceptionInfo)
			{
				return g_Instnace->OnVectoredException(*exceptionInfo);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}, VectoredExceptionHandler::Mode::ExceptionHandler, VectoredExceptionHandler::Order::First);
		Log(wxS("Installing vectored exception handler: %1"), vectoredExceptionHandler.IsInstalled() ? "success" : "failed");

		// Begin loading
		kxf::NativeFileSystem fs;
		const size_t itemsScanned = fs.EnumItems(m_PluginsDirectory, [&](kxf::FileItem fileItem)
		{
			if (fileItem.IsNormalItem())
			{
				const kxf::FSPath libraryPath = m_PluginsDirectory / fileItem.GetName().BeforeLast(wxS('_')) + wxS(".dll");
				Log(wxS("Preload directive '%1' found, trying to load the corresponding library '%2'"), fileItem.GetName(), libraryPath.GetFullPath());

				PluginStatus status = DoLoadPlugin(libraryPath);
				LogLoadStatus(libraryPath, status);
			}
			return true;
		}, wxS("*_preload.txt"), kxf::FSActionFlag::LimitToFiles);

		Log(wxS("Loading finished, %1 plugins loaded, %2 items scanned"), m_LoadedLibraries.size(), itemsScanned);
		Log(wxS("Removing vectored exception handler: %1"), vectoredExceptionHandler.Remove() ? "success" : "failed");
	}
	void PreloadHandler::DoUnloadPlugins()
	{
		Log(wxS("Unloading plugins"));
		m_LoadedLibraries.clear();
	}
	PluginStatus PreloadHandler::DoLoadPlugin(const kxf::FSPath& path)
	{
		LogIndent(1, wxS("Trying to load '%1'"), path.GetFullPath());

		kxf::DynamicLibrary pluginLibrary;
		PluginStatus pluginStatus = PluginStatus::FailedLoad;

		// Load plugin library
		const kxf::NtStatus loadStatus = SEHTryExcept([&]()
		{
			if (pluginLibrary.Load(path))
			{
				pluginStatus = PluginStatus::Loaded;
			}
			else
			{
				auto lastError = kxf::Win32Error::GetLastError();
				LogIndent(1, wxS("Couldn't load plugin '%1': [Win32: %2 (%3)]"), path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
			}
		});

		if (loadStatus)
		{
			if (pluginLibrary)
			{
				LogIndent(1, wxS("Library '%1' is loaded, attempt to call initialization routine"), path.GetFullPath());

				// Call initialization routine
				const kxf::NtStatus initializeStatus = SEHTryExcept([&]()
				{
					if (auto initalize = pluginLibrary.GetFunction<TInitialize>("Initialize"))
					{
						std::invoke(*initalize);
						pluginStatus = PluginStatus::Initialized;
					}
					else
					{
						LogIndent(1, wxS("No initialization routine found for '%1'"), path.GetFullPath());
					}
				});

				if (initializeStatus)
				{
					m_LoadedLibraries.emplace_back(std::move(pluginLibrary));
				}
				else
				{
					pluginStatus = PluginStatus::FailedInitialize;
					LogIndent(1, wxS("Exception occurred inside plugin's initialization routine '%1': [NtStatus: %2 (%3)]"), path.GetFullPath(), initializeStatus.GetMessage(), initializeStatus.GetValue());
				}
			}
			else
			{
				// No exception occurred, but the library isn't loaded. Proceed.
			}
		}
		else
		{
			pluginStatus = PluginStatus::FailedLoad;
			LogIndent(1, wxS("Exception occurred while loading plugin library '%1': [NtStatus: %2 (%3)]"), path.GetFullPath(), loadStatus.GetMessage(), loadStatus.GetValue());
		}

		return pluginStatus;
	}

	bool PreloadHandler::CheckAllowedProcesses() const
	{
		const kxf::String thisExecutableName = m_ExecutablePath.GetName();

		Log(wxS("Checking process name '%1' to determine if it's allowed to preload plugins. Following process names are allowed:"), thisExecutableName);
		if (!m_AllowedProcessNames.empty())
		{
			for (const kxf::String& name: m_AllowedProcessNames)
			{
				LogNoTime(wxS("\t%1"), name);
			}
		}
		else
		{
			LogNoTime(wxS("\t<none>"));
		}

		return kxf::Utility::Container::Contains(m_AllowedProcessNames, [&](const kxf::String& name)
		{
			if (name.IsSameAs(thisExecutableName, kxf::StringOpFlag::IgnoreCase))
			{
				Log(wxS("Match found: '%1' -> '%2'"), thisExecutableName, name);
				return true;
			}
			return false;
		});
	}

	#ifdef USE_NUKEM_DETOURS
	void PreloadHandler::DetourInitFunctions()
	{
		#ifdef USE_NUKEM_DETOURS
		Log(wxS("Overriding '_initterm_e' function for delayed load"));
		void* func = nullptr;

		#if xSE_PLATFORM_F4SE

		//m_initterm_e = DetourFunctionIAT(DelayedLoad_InitTrem, "MSVCR110.dll", "_initterm_e");
		//func = m_initterm_e;

		// 0x14295BFD0 is address of 'start' function in Fallout4.exe v1.10.106
		m_start = DetourFunctionThis(DelayedLoad_start, 0x14295BFD0u);
		func = m_start;

		#elif xSE_PLATFORM_NVSE

		// 0x00ECCCF0 is address of '_initterm_e' in 'FalloutNV.exe'
		m_initterm_e = DetourFunctionThis(DelayedLoad_InitTrem, 0x00ECCCF0u);
		func = m_initterm_e;

		#endif

		if (!func)
		{
			UnloadOriginalLibrary();
			m_OriginalLibrary.Unload();

			Log(wxS("Can't override function, terminating"));
		}
		#endif
	}
	#endif

	void PreloadHandler::LoadOriginalLibrary()
	{
		kxf::FSPath path = GetOriginalLibraryPath();
		Log(wxS("Loading original library '%1'"), path.GetFullPath());

		ClearOriginalFunctions();
		if (m_OriginalLibrary.Load(path))
		{
			Log(wxS("Original library '%1' loaded successfully"), path.GetFullPath());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log(wxS("Couldn't load library '%1': [Win32: %2 (%3)]"), path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
		}
	}
	void PreloadHandler::UnloadOriginalLibrary()
	{
		Log(wxS("Unloading original library '%1'"), m_OriginalLibrary.GetFilePath().GetFullPath());

		m_OriginalLibrary.Unload();
		ClearOriginalFunctions();
	}
	void PreloadHandler::ClearOriginalFunctions()
	{
		std::fill_n(GetFunctions(), GetFunctionsCount(), nullptr);
	}

	size_t PreloadHandler::DoLog(kxf::String logString, bool addTimestamp, size_t indent) const
	{
		if (m_LogStream && !logString.IsEmptyOrWhitespace())
		{
			kxf::IO::OutputStreamWriter writer(*m_LogStream);

			if (indent != 0)
			{
				logString.Prepend(wxS(' '), indent * 4);
			}
			if (addTimestamp)
			{
				auto timeStamp = kxf::DateTime::Now();
				kxf::String timeStampFormatted = kxf::StringFormatter::Formatter(wxS("[%1:%2] "))(timeStamp.FormatISOCombined(wxS(' ')))(timeStamp.GetMillisecond(), 3, 10, wxS('0'));

				logString.Prepend(std::move(timeStampFormatted));
			}

			writer.WriteStringUTF8(logString.Append(wxS('\n')));
			m_LogStream->Flush();

			return logString.length();
		}
		return 0;
	}

	uint32_t PreloadHandler::OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log(wxS("Caught vectored exception:"));
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	uint32_t PreloadHandler::OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log(wxS("Caught vectored continue:"));
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	kxf::String PreloadHandler::DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const
	{
		kxf::String result;

		const auto& context = exceptionInfo.ContextRecord;
		#if _WIN64
		result += kxf::String::Format(wxS("ContextRecord: [RAX: %1], [RBX: %2], [RCX: %3], [RDX: %4], [RBP: %5], [RDI: %6], [RIP: %7], ")
									  wxS("[R08: %8], [R09: %9], [R10: %10], [R11: %11], [R12: %12], [R13: %13], [R14: %14], [R15: %15].\n"),
									  context->Rax,
									  context->Rbx,
									  context->Rcx,
									  context->Rdx,
									  context->Rbp,
									  context->Rdi,
									  context->Rip,
									  context->R8,
									  context->R9,
									  context->R10,
									  context->R11,
									  context->R12,
									  context->R13,
									  context->R14,
									  context->R15
		);
		#else
		result += kxf::String::Format(wxS("ContextRecord: [EAX: %1], [EBX: %2], [ECX: %3], [EDX: %4], [EBP: %5], [EDI: %6], [EIP: %7].\n"),
									  context->Eax,
									  context->Ebx,
									  context->Ecx,
									  context->Edx,
									  context->Ebp,
									  context->Edi,
									  context->Eip
		);
		#endif

		const auto& exception = exceptionInfo.ExceptionRecord;
		result += kxf::String::Format(wxS("ExceptionRecord:\n\tExceptionCode: [NtStatus: %1 \"%2\"]\n\tExceptionFlags: %3\n\tExceptionAddress: %4\n\tExceptionRecord: %5"),
									  exception->ExceptionCode, [](kxf::NtStatus status)
		{
			kxf::String exceptionCodeMessage = status.GetMessage();
			exceptionCodeMessage.Replace(wxS('\r'), wxS(' '));
			exceptionCodeMessage.Replace(wxS('\n'), wxS(' '));
			exceptionCodeMessage.Trim().Trim(kxf::StringOpFlag::FromEnd);

			return exceptionCodeMessage;
		}(exception->ExceptionCode),
									  exception->ExceptionFlags,
									  exception->ExceptionAddress,
									  exception->ExceptionRecord
		);

		return result;
	}

	PreloadHandler::PreloadHandler()
	{
		// Initialize plugins directory
		kxf::NativeFileSystem fileSystem(kxf::NativeFileSystem::GetExecutableDirectory());
		m_PluginsDirectory = fileSystem.GetCurrentDirectory() / "Data" / xSE_FOLDER_NAME_W / "Plugins";
		m_ExecutablePath = kxf::DynamicLibrary::GetExecutingModule().GetFilePath();

		// Open log
		m_LogStream = fileSystem.OpenToWrite(g_LogFileName);
		Log(wxS("Log opened"));
		Log(wxS("%1 v%2 loaded"), GetLibraryName(), GetLibraryVersion());
		Log(wxS("Script Extender platform: %1"), xSE_NAME_W);

		// Load config
		if (auto stream = fileSystem.OpenToRead(g_ConfigFileName))
		{
			m_Config.Load(*stream);
		}
		else
		{
			Log(wxS("Couldn't load configuration from '%1', default configuration will be used"), fileSystem.ResolvePath(g_ConfigFileName).GetFullPath());
		}

		m_OriginalLibraryPath = [&]()
		{
			kxf::String path = m_Config.QueryElement(wxS("xSE/PluginPreloader/General/OriginalLibrary")).GetValue();
			if (path.IsEmpty())
			{
				Log(wxS("Original library path is not set, using default '%1'"), GetOriginalLibraryDefaultPath().GetFullPath());
			}
			else
			{
				Log(wxS("Original library path is set to '%1'"), path);
			}
			return path;
		}();
		m_LoadMethod = [&]()
		{
			kxf::String methodName = m_Config.QueryElement(wxS("xSE/PluginPreloader/General/LoadMethod")).GetValue();
			if (methodName == wxS("Direct"))
			{
				Log(wxS("Load method is set to 'Direct'"));
				return LoadMethod::Direct;
			}
			else if (methodName == wxS("Delayed"))
			{
				Log(wxS("Load method is set to 'Delayed'"));
				return LoadMethod::Delayed;
			}
			else
			{
				Log(wxS("Unknown load method, using 'Direct'"));
				return LoadMethod::Direct;
			}
		}();
		m_LoadDelay = [&]()
		{
			return kxf::TimeSpan::Milliseconds(m_Config.QueryElement(wxS("xSE/PluginPreloader/General/LoadDelay")).GetValueInt());
		}();
		m_AllowedProcessNames = [&]()
		{
			std::vector<kxf::String> processes;
			m_Config.QueryElement(wxS("xSE/PluginPreloader/Processes")).EnumChildElements([&](kxf::XMLNode itemNode)
			{
				if (itemNode.GetAttributeBool(wxS("Allow")))
				{
					if (processes.emplace_back(itemNode.GetAttribute(wxS("Name"))).IsEmpty())
					{
						processes.pop_back();
					}
				}
				return true;
			}, wxS("Item"));

			return processes;
		}();

		// Check processes, if we are not allowed to preload inside this process set the flag and don't load plugins but still load the original library.
		m_PluginsLoadAllowed = CheckAllowedProcesses();
		if (!m_PluginsLoadAllowed)
		{
			Log(wxS("This process '%1' is not allowed to preload plugins"), m_ExecutablePath.GetName());
		}

		// Load
		LoadOriginalLibrary();
		if (m_OriginalLibrary)
		{
			LoadOriginalLibraryFunctions();
			
			#ifdef USE_NUKEM_DETOURS
			DetourInitFunctions();
			#endif
		}
		else
		{
			Log(wxS("Can't load original library, terminating"));
		}
	}
	PreloadHandler::~PreloadHandler()
	{
		if (m_OriginalLibrary)
		{
			DoUnloadPlugins();
			UnloadOriginalLibrary();
		}

		Log(L"Log closed");
		m_LogStream = nullptr;
	}

	void PreloadHandler::LoadPlugins()
	{
		if (!m_PluginsLoadAllowed)
		{
			Log(wxS("Plugins preload disabled for this process"));
			return;
		}

		if (!m_PluginsLoaded)
		{
			Log(wxS("Loading plugins"));
			if (m_LoadDelay.IsPositive())
			{
				Log(wxS("Loading plugins is delayed by '%1' ms, waiting"), m_LoadDelay.GetMilliseconds());
				::Sleep(m_LoadDelay.GetMilliseconds());
				Log(wxS("Wait time is out, continuing loading"));
			}

			DoLoadPlugins();
			m_PluginsLoaded = true;
		}
	}
}
