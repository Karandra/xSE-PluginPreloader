#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "ScriptExtenderDefinesBase.h"
#include "resource.h"
#include <kxf/IO/StreamReaderWriter.h>
#include <kxf/Localization/Locale.h>
#include <kxf/System/ExecutableVersionResource.h>
#include <kxf/System/SystemInformation.h>
#include <kxf/System/ShellOperations.h>
#include <kxf/System/Win32Error.h>
#include <kxf/System/NtStatus.h>
#include <kxf/Utility/System.h>
#include <kxf/Utility/Container.h>
#include <kxf/Utility/CallAtScopeExit.h>
#include <wx/module.h>

#include "Nukem Detours/Detours.h"
#if _WIN64

namespace NukemDetoursBase = Detours;
namespace NukemDetours = Detours::X64;
using NukemDetoursOpt = Detours::X64Option;
#pragma comment(lib, "Nukem Detours/x64/detours.lib")

#else

namespace NukemDetoursBase = Detours;
namespace NukemDetours = Detours::X86;
using NukemDetoursOpt = Detours::X86Option;
#pragma comment(lib, "Nukem Detours/x86/detours.lib")

#endif

namespace
{
	std::unique_ptr<xSE::PreloadHandler> g_Instance;

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
				g_Instance->LogIndent(1, wxS("<%1> Plugin loaded successfully"), path.GetName());
				break;
			}
			case PluginStatus::Initialized:
			{
				g_Instance->LogIndent(1, wxS("<%1> Plugin 'loaded successfully, 'Initialize' executed successfully"), path.GetName());
				break;
			}
			case PluginStatus::FailedLoad:
			{
				g_Instance->LogIndent(1, wxS("<%1> Plugin failed to load"), path.GetName());
				break;
			}
			case PluginStatus::FailedInitialize:
			{
				g_Instance->LogIndent(1, wxS("<%1> Plugin failed to load. Exception was thrown during 'Initialize' execution"), path.GetName());
				break;
			}
			default:
			{
				g_Instance->LogIndent(1, wxS("<%1> Unknown plugin status code (%2)"), path.GetName(), static_cast<uint32_t>(status));
				break;
			}
		};
	}
	
	std::optional<xSE::LoadMethod> LoadMethodFromString(const kxf::String& name)
	{
		using namespace xSE;

		if (name == wxS("OnProcessAttach"))
		{
			return LoadMethod::OnProcessAttach;
		}
		else if (name == wxS("OnThreadAttach"))
		{
			return LoadMethod::OnThreadAttach;
		}
		else if (name == wxS("ImportAddressHook"))
		{
			return LoadMethod::ImportAddressHook;
		}
		return {};
	}
	kxf::String LoadMethodToName(xSE::LoadMethod method)
	{
		using namespace xSE;

		switch (method)
		{
			case LoadMethod::OnProcessAttach:
			{
				return wxS("OnProcessAttach");
			}
			case LoadMethod::OnThreadAttach:
			{
				return wxS("OnThreadAttach");
			}
			case LoadMethod::ImportAddressHook:
			{
				return wxS("ImportAddressHook");
			}
		};
		return wxS("Unknown");
	}
}

namespace Detour
{
	template<class T>
	T FunctionIAT(T func, const char* libraryName, const char* functionName) noexcept
	{
		const uintptr_t base = reinterpret_cast<uintptr_t>(::GetModuleHandleW(nullptr));
		return reinterpret_cast<T>(NukemDetoursBase::IATHook(base, libraryName, functionName, reinterpret_cast<uintptr_t>(func)));
	}

	template<class T>
	T FunctionFromModule(HMODULE moduleBase, T func, uintptr_t offset) noexcept
	{
		return reinterpret_cast<T>(NukemDetours::DetourFunction(reinterpret_cast<uintptr_t>(moduleBase) + offset, reinterpret_cast<uintptr_t>(func)));
	}

	template<class T>
	T FunctionFromModuleByName(const wchar_t* libraryName, T func, uintptr_t offset) noexcept
	{
		return FunctionFromModule(::GetModuleHandleW(libraryName), func, offset);
	}

	template<class T>
	T FunctionFromExecutingModule(T func, uintptr_t offset) noexcept
	{
		return FunctionFromModuleByName(nullptr, func, offset);
	}
}

namespace xSE
{
	kxf::String PreloadHandler::GetLibraryName()
	{
		return wxS("xSE PluginPreloader");
	}
	kxf::Version PreloadHandler::GetLibraryVersion()
	{
		return wxS("0.2.2");
	}

	PreloadHandler& PreloadHandler::CreateInstance()
	{
		if (!g_Instance)
		{
			g_Instance = std::make_unique<xSE::PreloadHandler>();
		}
		return *g_Instance;
	}
	void PreloadHandler::DestroyInstance()
	{
		g_Instance = nullptr;
	}
	PreloadHandler& PreloadHandler::GetInstance()
	{
		return *g_Instance;
	}
	bool PreloadHandler::HasInstance()
	{
		return g_Instance != nullptr;
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
		// Install exception handler and remove it after loading is done
		InstallVectoredExceptionHandler();
		kxf::Utility::CallAtScopeExit atExit = [&]()
		{
			if (!m_KeepExceptionHandler && *m_LoadMethod != LoadMethod::ImportAddressHook)
			{
				RemoveVectoredExceptionHandler();
			}
		};

		// Begin loading
		Log(wxS("Searching directory '%1' for plugins"), m_PluginsDirectory.GetFullPath());

		const size_t itemsScanned = m_FileSystem.EnumItems(m_PluginsDirectory, [&](kxf::FileItem fileItem)
		{
			if (fileItem.IsNormalItem())
			{
				const kxf::FSPath libraryPath = m_PluginsDirectory / fileItem.GetName().BeforeLast(wxS('_')) + wxS(".dll");
				Log(wxS("Preload directive '%1' found, trying to load the corresponding library '%2'"), fileItem.GetName(), libraryPath.GetFullPath());

				PluginStatus status = DoLoadSinglePlugin(libraryPath);
				LogLoadStatus(libraryPath, status);
			}
			return true;
		}, wxS("*_preload.txt"), kxf::FSActionFlag::LimitToFiles);

		Log(wxS("Loading finished, %1 plugins loaded, %2 items scanned"), m_LoadedLibraries.size(), itemsScanned);
	}
	void PreloadHandler::DoUnloadPlugins()
	{
		Log(wxS("Unloading plugins"));
		m_LoadedLibraries.clear();
	}
	PluginStatus PreloadHandler::DoLoadSinglePlugin(const kxf::FSPath& path)
	{
		LogIndent(1, wxS("<%1> Trying to load"), path.GetName());

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
				LogIndent(1, wxS("<%1> Couldn't load plugin: [Win32: '%2' (%3)]"), path.GetName(), lastError.GetMessage(), lastError.GetValue());
			}
		});

		if (loadStatus)
		{
			if (pluginLibrary)
			{
				LogIndent(1, wxS("<%1> Library is loaded, attempt to call initialization routine"), path.GetName());

				// Call initialization routine
				const kxf::NtStatus initializeStatus = SEHTryExcept([&]()
				{
					using TInitialize = void(__cdecl*)(void);
					if (auto initalize = pluginLibrary.GetFunction<TInitialize>("Initialize"))
					{
						LogIndent(1, wxS("<%1> Calling the initialization routine"), path.GetName());
						std::invoke(*initalize);
						pluginStatus = PluginStatus::Initialized;
					}
					else
					{
						LogIndent(1, wxS("<%1> No initialization routine found"), path.GetName());
					}
				});

				if (initializeStatus)
				{
					m_LoadedLibraries.emplace_back(std::move(pluginLibrary));
				}
				else
				{
					pluginStatus = PluginStatus::FailedInitialize;
					LogIndent(1, wxS("<%1> Exception occurred inside plugin's initialization routine: [NtStatus: '%2' (%3)]"), path.GetName(), initializeStatus.GetMessage(), initializeStatus.GetValue());
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
			LogIndent(1, wxS("<%1> Exception occurred while loading plugin library: [NtStatus: '%2' (%3)]"), path.GetName(), loadStatus.GetMessage(), loadStatus.GetValue());
		}

		return pluginStatus;
	}

	bool PreloadHandler::CheckAllowedProcesses() const
	{
		const kxf::String thisExecutableName = m_ExecutablePath.GetName();
		Log(wxS("<%1> Checking process name to determine if it's allowed to preload plugins. Following process names are allowed: [%2]"), thisExecutableName, [&]() -> kxf::String
		{
			if (!m_AllowedProcessNames.empty())
			{
				kxf::String result;
				for (const kxf::String& name: m_AllowedProcessNames)
				{
					if (!result.IsEmpty())
					{
						result += wxS(", ");
					}

					result += wxS('\'');
					result += name;
					result += wxS('\'');
				}
				return result;
			}
			else
			{
				return wxS("<none>");
			}
		}());

		return kxf::Utility::Container::Contains(m_AllowedProcessNames, [&](const kxf::String& name)
		{
			if (name.IsSameAs(thisExecutableName, kxf::StringOpFlag::IgnoreCase))
			{
				Log(wxS("<%1> Match found: '%2'"), thisExecutableName, name);
				return true;
			}
			return false;
		});
	}
	void PreloadHandler::LoadOriginalLibrary()
	{
		kxf::FSPath path = GetOriginalLibraryPath();
		Log(wxS("<%1> Loading original library"), path.GetFullPath());

		ClearOriginalFunctions();
		if (m_OriginalLibrary.Load(path))
		{
			Log(wxS("<%1> Original library loaded successfully"), path.GetFullPath());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log(wxS("<%1> Couldn't load library: [Win32: %2 (%3)]"), path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
		}
	}
	void PreloadHandler::UnloadOriginalLibrary()
	{
		Log(wxS("<%1> Unloading original library"), m_OriginalLibrary.GetFilePath().GetFullPath());

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

	bool PreloadHandler::InstallVectoredExceptionHandler()
	{
		m_VectoredExceptionHandler.Install([](_EXCEPTION_POINTERS* exceptionInfo) -> LONG
		{
			if (g_Instance && exceptionInfo)
			{
				return g_Instance->OnVectoredException(*exceptionInfo);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}, VectoredExceptionHandler::Mode::ExceptionHandler, VectoredExceptionHandler::Order::First);
		Log(wxS("Installing vectored exception handler: %1"), m_VectoredExceptionHandler.IsInstalled() ? "success" : "failed");

		return m_VectoredExceptionHandler.IsInstalled();
	}
	void PreloadHandler::RemoveVectoredExceptionHandler()
	{
		Log(wxS("Removing vectored exception handler: %1"), m_VectoredExceptionHandler.Remove() ? "success" : "failed (not installed or already removed)");
	}
	uint32_t PreloadHandler::OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log(wxS("Caught vectored continue:"));
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	uint32_t PreloadHandler::OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log(wxS("Caught vectored exception:"));
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	kxf::String PreloadHandler::DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const
	{
		using kxf::StringFormatter::Formatter;
		constexpr int fieldWidth = sizeof(void*);
		constexpr int intBase = 16;
		constexpr kxf::XChar intFillChar = wxS('0');

		kxf::String result;

		const auto& context = exceptionInfo.ContextRecord;
		#if _WIN64
		result += Formatter(wxS("ContextRecord: [RAX: 0x%1], [RBX: 0x%2], [RCX: 0x%3], [RDX: 0x%4], [RBP: 0x%5], [RDI: 0x%6], [RIP: 0x%7], ")
							wxS("[R08: 0x%8], [R09: 0x%9], [R10: 0x%10], [R11: 0x%11], [R12: 0x%12], [R13: 0x%13], [R14: 0x%14], [R15: 0x%15].\n"))
			(context->Rax, fieldWidth, intBase, intFillChar)
			(context->Rbx, fieldWidth, intBase, intFillChar)
			(context->Rcx, fieldWidth, intBase, intFillChar)
			(context->Rdx, fieldWidth, intBase, intFillChar)
			(context->Rbp, fieldWidth, intBase, intFillChar)
			(context->Rdi, fieldWidth, intBase, intFillChar)
			(context->Rip, fieldWidth, intBase, intFillChar)
			(context->R8, fieldWidth, intBase, intFillChar)
			(context->R9, fieldWidth, intBase, intFillChar)
			(context->R10, fieldWidth, intBase, intFillChar)
			(context->R11, fieldWidth, intBase, intFillChar)
			(context->R12, fieldWidth, intBase, intFillChar)
			(context->R13, fieldWidth, intBase, intFillChar)
			(context->R14, fieldWidth, intBase, intFillChar)
			(context->R15, fieldWidth, intBase, intFillChar).ToString();
		#else
		result += Formatter(wxS("ContextRecord: [EAX: 0x%1], [EBX: 0x%2], [ECX: 0x%3], [EDX: 0x%4], [EBP: 0x%5], [EDI: 0x%6], [EIP: 0x%7].\n"))
			(context->Eax, fieldWidth, intBase, intFillChar)
			(context->Ebx, fieldWidth, intBase, intFillChar)
			(context->Ecx, fieldWidth, intBase, intFillChar)
			(context->Edx, fieldWidth, intBase, intFillChar)
			(context->Ebp, fieldWidth, intBase, intFillChar)
			(context->Edi, fieldWidth, intBase, intFillChar)
			(context->Eip, fieldWidth, intBase, intFillChar).ToString();
		#endif

		const auto& exception = exceptionInfo.ExceptionRecord;
		result += Formatter(wxS("ExceptionRecord:\n\tExceptionCode: [NtStatus: (0x%1) '%2']\n\tExceptionFlags: 0x%3\n\tExceptionAddress: 0x%4\n\tExceptionRecord: 0x%5"))
			(exception->ExceptionCode, fieldWidth, intBase, intFillChar)
			([](kxf::NtStatus status)
			{
				kxf::String exceptionCodeMessage = status.GetMessage();
				exceptionCodeMessage.Replace(wxS("\r\n"), wxS("; "));
				exceptionCodeMessage.Replace(wxS("\r"), wxS("; "));
				exceptionCodeMessage.Replace(wxS("\n"), wxS("; "));
				exceptionCodeMessage.Trim().Trim(kxf::StringOpFlag::FromEnd);

				return exceptionCodeMessage;
		}(exception->ExceptionCode))
			(exception->ExceptionFlags, fieldWidth, intBase, intFillChar)
			(exception->ExceptionAddress)
			(exception->ExceptionRecord).ToString();

		return result;
	}

	void PreloadHandler::LogEnvironemntInfo() const
	{
		if (const auto versionInfo = kxf::System::GetVersionInfo())
		{
			const kxf::System::KernelVersion kernel = versionInfo->Kernel;
			const bool is64Bit = kxf::System::Is64Bit();
			Log(wxS("<Environemnt> Operation system: '%1' %2.%3.%4"), kxf::System::GetProductName(*versionInfo, is64Bit), kernel.Major, kernel.Minor, kernel.Build);

			if (kernel.ServicePackMajor > 0)
			{
				Log(wxS("<Environemnt> System service pack: '%1' %2.%3"), versionInfo->ServicePack, kernel.ServicePackMajor, kernel.ServicePackMinor);
			}
			else
			{
				Log(wxS("<Environemnt> System service pack: <not applicable>"));
			}
			Log(wxS("<Environemnt> System product type: %1"), kxf::ToInt(versionInfo->ProductType));
		}
		else
		{
			Log(wxS("<Environemnt> Couldn't query system version"));
		}

		Log(wxS("<Environemnt> System default locale: '%1'"), kxf::Locale::GetSystemDefault().GetName());
		Log(wxS("<Environemnt> System preferred locale: '%1'"), kxf::Locale::GetSystemPreferred().GetName());
	}
	void PreloadHandler::LogCurrentModuleInfo() const
	{
		auto currentModule = kxf::DynamicLibrary::GetCurrentModule();

		Log(wxS("<Current module> Binary: '%1'"), currentModule.GetFilePath().GetFullPath());
		Log(wxS("<Current module> %1 v%2 loaded"), GetLibraryName(), GetLibraryVersion().ToString());
	}
	void PreloadHandler::LogHostProcessInfo() const
	{
		Log(wxS("<Host process> Binary: '%1'"), m_ExecutablePath.GetFullPath());

		const kxf::ExecutableVersionResource resourceInfo(m_ExecutablePath);
		if (resourceInfo)
		{
			Log(wxS("<Host process> Version: %1"), resourceInfo.GetAnyVersion());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log(wxS("<Host process> Couldn't load host process binary. [Win32: '%1' (%2)]"), lastError.GetMessage(), lastError.GetValue());
		}
	}
	void PreloadHandler::LogScriptExtenderInfo() const
	{
		const kxf::FSPath loaderPath = m_FileSystem.ResolvePath(xSE_FOLDER_NAME_W wxS("_Loader.exe"));
		Log(wxS("<Script Extender> Platform: %1"), xSE_NAME_W);
		Log(wxS("<Script Extender> Binary: '%1'"), loaderPath.GetFullPath());

		const kxf::ExecutableVersionResource resourceInfo(loaderPath);
		if (resourceInfo)
		{
			Log(wxS("<Script Extender> Version: %1"), resourceInfo.GetAnyVersion());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log(wxS("<Script Extender> Couldn't load %1 binary, probably not installed. [Win32: '%2' (%3)]"), xSE_NAME_W, lastError.GetMessage(), lastError.GetValue());
		}
	}

	PreloadHandler::PreloadHandler()
		:m_FileSystem(kxf::NativeFileSystem::GetExecutableDirectory())
	{
		// Initialize plugins directory
		m_PluginsDirectory = m_FileSystem.GetCurrentDirectory() / "Data" / xSE_FOLDER_NAME_W / "Plugins";
		m_ExecutablePath = kxf::DynamicLibrary::GetExecutingModule().GetFilePath();

		// Open log
		m_LogStream = m_FileSystem.OpenToWrite(g_LogFileName);
		Log(wxS("Log opened"));

		Log(wxS("Initializing framework"));
		wxModule::RegisterModules();
		if (!wxModule::InitializeModules())
		{
			LogIndent(1, wxS("Initializing framework: failed"));
		}

		LogCurrentModuleInfo();
		LogHostProcessInfo();
		LogScriptExtenderInfo();
		LogEnvironemntInfo();

		// Load config
		Log(wxS("Loading configuration from '%1'"), m_FileSystem.ResolvePath(g_ConfigFileName).GetFullPath());
		if (auto readStream = m_FileSystem.OpenToRead(g_ConfigFileName); readStream && m_Config.Load(*readStream))
		{
			LogIndent(1, wxS("Configuration file successfully loaded"));
		}
		else
		{
			if (readStream)
			{
				LogIndent(1, wxS("Couldn't load configuration. The file is found, but can not be loaded, default configuration will be used"));
			}
			else
			{
				auto lastError = kxf::Win32Error::GetLastError();
				LogIndent(1, wxS("Couldn't load configuration file: [Win32: '%1' (%2)], default configuration will be used"), lastError.GetMessage(), lastError.GetValue());
			}
			readStream = nullptr;

			// Restore the default config on disk and load it
			Log(wxS("Restoring default configuration"));

			auto defaultXML = kxf::Utility::LoadResource(kxf::DynamicLibrary::GetCurrentModule(), IDR_XML_DEFAULT_CONFIGURATION, wxS("XML"));
			if (m_Config.Load(kxf::StringViewOf(defaultXML)))
			{
				LogIndent(1, wxS("Default configuration successfully loaded"));

				auto writeStream = m_FileSystem.OpenToWrite(g_ConfigFileName);
				if (writeStream && writeStream->WriteAll(defaultXML.data(), defaultXML.length()))
				{
					LogIndent(1, wxS("Default configuration successfully saved to disk"));
				}
				else
				{
					auto lastError = kxf::Win32Error::GetLastError();
					LogIndent(1, wxS("Couldn't save default configuration to disk: [Win32: '%1' (%2)]"), lastError.GetMessage(), lastError.GetValue());
				}
			}
			else
			{
				LogIndent(1, wxS("Couldn't load default configuration"));
			}
		}

		m_OriginalLibraryPath = [&]()
		{
			kxf::String path = m_Config.QueryElement(wxS("xSE/PluginPreloader/OriginalLibrary")).GetValue();
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
		m_KeepExceptionHandler = [&]()
		{
			return m_Config.QueryElement(wxS("xSE/PluginPreloader/KeepExceptionHandler")).GetValueBool();
		}();
		m_LoadMethod = [&]() -> decltype(m_LoadMethod)
		{
			kxf::XMLNode rootNode = m_Config.QueryElement(wxS("xSE/PluginPreloader/LoadMethod"));
			kxf::String methodName = rootNode.GetAttribute(wxS("Name"));

			if (auto method = LoadMethodFromString(methodName))
			{
				Log(wxS("Load method is set to '%1', loading method parameters"), methodName);

				kxf::XMLNode methodNode = rootNode.GetFirstChildElement(methodName);
				switch (*method)
				{
					case LoadMethod::OnProcessAttach:
					{
						LogIndent(1, wxS("No parameters"));
						break;
					}
					case LoadMethod::OnThreadAttach:
					{
						auto value = methodNode.GetFirstChildElement(wxS("ThreadNumber")).GetValueInt(2);
						if (value < 0)
						{
							value = 0;
						}
						m_OnThreadAttach.ThreadNumber = static_cast<size_t>(value);

						LogIndent(1, wxS("ThreadNumber = %1"), m_OnThreadAttach.ThreadNumber);
						break;
					}
					case LoadMethod::ImportAddressHook:
					{
						m_ImportAddressHook.LibraryName = methodNode.GetFirstChildElement(wxS("LibraryName")).GetValue();
						m_ImportAddressHook.FunctionName = methodNode.GetFirstChildElement(wxS("FunctionName")).GetValue();

						LogIndent(1, wxS("LibraryName = %1"), m_ImportAddressHook.LibraryName);
						LogIndent(1, wxS("FunctionName = %1"), m_ImportAddressHook.FunctionName);

						break;
					}
				};
				return *method;
			}
			else
			{
				Log(wxS("Unknown load method: '%1'"), methodName);
				return {};
			}
		}();
		m_LoadDelay = [&]()
		{
			return kxf::TimeSpan::Milliseconds(m_Config.QueryElement(wxS("xSE/PluginPreloader/LoadDelay")).GetValueInt(0));
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
			Log(wxS("<%1> This process is not allowed to preload plugins"), m_ExecutablePath.GetName());
		}

		// Load the original library
		LoadOriginalLibrary();
		if (m_OriginalLibrary)
		{
			LoadOriginalLibraryFunctions();
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
		RemoveVectoredExceptionHandler();

		Log(L"Log closed");
		m_LogStream = nullptr;
	}

	bool PreloadHandler::HookImportTable()
	{
		if (!m_PluginsLoadAllowed)
		{
			Log(wxS("<ImportAddressHook> Plugins preload disabled for this process, skipping hook installation"));
			return false;
		}

		Log(wxS("<ImportAddressHook> Hooking function '%1' from library '%2'"), m_ImportAddressHook.FunctionName, m_ImportAddressHook.LibraryName);

		struct ImportHook final
		{
			static void HookFunc(void* a1, void* a2)
			{
				if (!g_Instance->IsPluginsLoaded())
				{
					g_Instance->Log(wxS("<ImportAddressHook> LoadPlugins"));
					g_Instance->LoadPlugins();
				}

				g_Instance->Log(wxS("<ImportAddressHook> Calling unhooked function"));
				const kxf::NtStatus status = SEHTryExcept([&]()
				{
					g_Instance->m_ImportAddressHook.CallUnhooked(a1, a2);
				});

				if (status)
				{
					g_Instance->Log(wxS("<ImportAddressHook> Unhooked function returned successfully"));
				}
				else
				{
					g_Instance->Log(wxS("<ImportAddressHook> Exception occurred: [NtStatus: '%1' (%2)]"), status.GetMessage(), status.GetValue());
				}

				// Remove exception handler if needed
				if (!g_Instance->m_KeepExceptionHandler)
				{
					g_Instance->RemoveVectoredExceptionHandler();
				}
			}
		};
		m_ImportAddressHook.SaveUnhooked(Detour::FunctionIAT(&ImportHook::HookFunc, m_ImportAddressHook.LibraryName.c_str(), m_ImportAddressHook.FunctionName.c_str()));

		if (m_ImportAddressHook.IsHooked())
		{
			LogIndent(1, wxS("Success [Hooked=0x%1], [Unhooked=0x%2]"), &ImportHook::HookFunc, m_ImportAddressHook.GetUnhooked());
			return true;
		}
		else
		{
			LogIndent(1, wxS("Unable to hook import table function"));
			return false;
		}
	}
	bool PreloadHandler::LoadPlugins()
	{
		if (!m_PluginsLoadAllowed)
		{
			Log(wxS("Plugins preload disabled for this process"));
			return false;
		}

		if (!m_PluginsLoaded)
		{
			Log(wxS("Loading plugins"));
			if (m_LoadDelay.IsPositive())
			{
				Log(wxS("<LoadDelay> Loading plugins is delayed by '%1' ms, waiting"), m_LoadDelay.GetMilliseconds());
				::Sleep(m_LoadDelay.GetMilliseconds());
				Log(wxS("<LoadDelay> Wait time is out, continuing loading"));
			}

			DoLoadPlugins();
			m_PluginsLoaded = true;

			return true;
		}
		return false;
	}

	bool PreloadHandler::DisableThreadLibraryCalls(HMODULE handle)
	{
		if (::DisableThreadLibraryCalls(handle))
		{
			Log(wxS("<DisableThreadLibraryCalls> Success"));
			return true;
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log(wxS("<DisableThreadLibraryCalls> Failed: [Win32: '%1' (%2)]"), lastError.GetMessage(), lastError.GetValue());

			return false;
		}
	}
}
