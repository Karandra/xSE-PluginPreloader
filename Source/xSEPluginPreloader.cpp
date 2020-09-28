#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "ScriptExtenderDefinesBase.h"
#include <kxf/FileSystem/NativeFileSystem.h>
#include <kxf/System/ShellOperations.h>
#include <kxf/System/Win32Error.h>
#include <kxf/System/NtStatus.h>
#include <kxf/Utility/Container.h>

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
				g_Instnace->Log(wxS("Plugin '%1' loaded successfully"), path.GetFullPath());
				break;
			}
			case PluginStatus::Initialized:
			{
				g_Instnace->Log(wxS("Plugin '%1' loaded successfully, 'Initialize' executed successfully"), path.GetFullPath());
				break;
			}
			case PluginStatus::FailedLoad:
			{
				g_Instnace->Log(wxS("Plugin '%1' failed to load"), path.GetFullPath());
				break;
			}
			case PluginStatus::FailedInitialize:
			{
				g_Instnace->Log(wxS("Plugin '%1' failed to load. Exception was thrown during 'Initialize' execution"), path.GetFullPath());
				break;
			}
			default:
			{
				g_Instnace->Log(wxS("Unknown plugin status code (%1): '%2"), static_cast<uint32_t>(status), path.GetFullPath());
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
		Log(L"Searching directory '%1' for plugins", m_PluginsDirectory.GetFullPath());

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

		Log(L"Loading finished, %1 plugins loaded, %2 items scanned", m_LoadedLibraries.size(), itemsScanned);
	}
	void PreloadHandler::DoUnloadPlugins()
	{
		Log(wxS("Unloading plugins"));
		m_LoadedLibraries.clear();
	}
	PluginStatus PreloadHandler::DoLoadPlugin(const kxf::FSPath& path)
	{
		Log(wxS("Trying to load '%1'"), path.GetFullPath());

		kxf::DynamicLibrary* pluginLibrary = nullptr;
		PluginStatus pluginStatus = PluginStatus::FailedLoad;

		// Load plugin library
		const kxf::NtStatus loadStatus = SEHTryExcept([&]()
		{
			if (kxf::DynamicLibrary& library = m_LoadedLibraries.emplace_back(path))
			{
				pluginStatus = PluginStatus::Loaded;
				pluginLibrary = &library;
			}
			else
			{
				m_LoadedLibraries.pop_back();

				auto lastError = kxf::Win32Error::GetLastError();
				Log(wxS("Couldn't load plugin '%1': [Win32: %2 (%3)]"), path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
			}
		});

		if (loadStatus)
		{
			if (pluginLibrary)
			{
				Log(wxS("Library '%1' is loaded, attempt to call initialization routine"), path.GetFullPath());

				// Call initialization routine
				const kxf::NtStatus initializeStatus = SEHTryExcept([&]()
				{
					if (auto initalize = pluginLibrary->GetFunction<TInitialize>("Initialize"))
					{
						std::invoke(*initalize);
						pluginStatus = PluginStatus::Initialized;
					}
					else
					{
						// No initialization routine for this plugin. Proceed.
					}
				});
				if (!initializeStatus)
				{
					pluginStatus = PluginStatus::FailedInitialize;
					Log(wxS("Exception occurred inside plugin's initialization routine '%1': [NtStatus: %2 (%3)]"), path.GetFullPath(), initializeStatus.GetMessage(), initializeStatus.GetValue());
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
			Log(wxS("Exception occurred while loading plugin library '%1': [NtStatus: %2 (%3)]"), path.GetFullPath(), loadStatus.GetMessage(), loadStatus.GetValue());
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
				Log(wxS("\t%1"), name);
			}
		}
		else
		{
			Log(wxS("\t<none>"));
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

	PreloadHandler::PreloadHandler()
	{
		// Initialize plugins directory
		kxf::NativeFileSystem fileSystem(kxf::NativeFileSystem::GetExecutableDirectory());
		m_PluginsDirectory = fileSystem.GetCurrentDirectory() / "Data" / xSE_FOLDER_NAME_W / "Plugins";
		m_ExecutablePath = kxf::DynamicLibrary::GetExecutingModule().GetFilePath();

		// Open log
		_wfopen_s(&m_Log, g_LogFileName, L"wb+");
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
		if (m_Log)
		{
			fclose(m_Log);
		}
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
			DoLoadPlugins();
			m_PluginsLoaded = true;
		}
	}
}
