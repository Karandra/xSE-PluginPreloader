#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "ScriptExtenderDefinesBase.h"
#include "Application.h"
#include "Detour.h"

#include <kxf/Application/GUIApplication.h>
#include <kxf/IO/StreamReaderWriter.h>
#include <kxf/Log/Common.h>
#include <kxf/Log/ScopedLogger.h>
#include <kxf/Log/ScopedLoggerContext.h>
#include <kxf/Localization/Locale.h>
#include <kxf/System/ExecutableVersionResource.h>
#include <kxf/System/SystemInformation.h>
#include <kxf/System/ShellOperations.h>
#include <kxf/System/Win32Error.h>
#include <kxf/System/NtStatus.h>
#include <kxf/System/NativeAPI.h>
#include <kxf/System/DynamicLibraryEvent.h>
#include <kxf/Threading/Common.h>
#include <kxf/Utility/Container.h>
#include <kxf/Utility/ScopeGuard.h>
#include <wx/module.h>

namespace
{
	std::unique_ptr<xSE::PreloadHandler> g_Instance;

	constexpr auto g_ConfigFileName = "xSE PluginPreloader.xml";
	constexpr auto g_LogFileName = "xSE PluginPreloader.log";

	void LogLoadStatus(const kxf::FSPath& path, xSE::PluginStatus status)
	{
		using namespace xSE;

		switch (status)
		{
			case PluginStatus::Loaded:
			{
				kxf::Log::InfoCategory(path.GetName(), "Plugin loaded successfully");
				break;
			}
			case PluginStatus::Initialized:
			{
				kxf::Log::InfoCategory(path.GetName(), "Plugin loaded and initialized successfully");
				break;
			}
			case PluginStatus::FailedLoad:
			{
				kxf::Log::WarningCategory(path.GetName(), "Plugin failed to load");
				break;
			}
			case PluginStatus::FailedInitialize:
			{
				kxf::Log::WarningCategory(path.GetName(), "Plugin failed to load. Exception was thrown during the initialization execution");
				break;
			}
			default:
			{
				kxf::Log::ErrorCategory(path.GetName(), "Unknown plugin status code ({})", static_cast<uint32_t>(status));
				break;
			}
		};
	}

	std::optional<xSE::LoadMethod> LoadMethodFromString(const kxf::String& name)
	{
		using namespace xSE;

		if (name == "OnProcessAttach")
		{
			return LoadMethod::OnProcessAttach;
		}
		else if (name == "OnThreadAttach")
		{
			return LoadMethod::OnThreadAttach;
		}
		else if (name == "ImportAddressHook")
		{
			return LoadMethod::ImportAddressHook;
		}
		return {};
	}
	std::optional<xSE::InitializationMethod> InitializationMethodFromString(const kxf::String& name)
	{
		using namespace xSE;

		if (name == "None")
		{
			return InitializationMethod::None;
		}
		else if (name == "Standard")
		{
			return InitializationMethod::Standard;
		}
		else if (name == "xSE-PluginPreload")
		{
			return InitializationMethod::xSEPluginPreload;
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
				return "OnProcessAttach";
			}
			case LoadMethod::OnThreadAttach:
			{
				return "OnThreadAttach";
			}
			case LoadMethod::ImportAddressHook:
			{
				return "ImportAddressHook";
			}
		};
		return "Unknown";
	}
}

namespace xSE::PluginPreloader
{
	class ImportAddressHookHandler final
	{
		private:
			static void HookCommonBefore()
			{
				KX_SCOPEDLOG_FUNC;

				g_Instance->LoadPlugins();

				KX_SCOPEDLOG.SetSuccess();
			}
			static void HookCommonAfter(const kxf::NtStatus& status)
			{
				KX_SCOPEDLOG_ARGS(status);

				if (status)
				{
					KX_SCOPEDLOG.Info(LogCategory::ImportAddressHook).Format("Original function returned successfully");
				}
				else
				{
					KX_SCOPEDLOG.Error(LogCategory::ImportAddressHook).Format("Exception occurred while executing the original function: {}", status);
				}

				// Remove exception handler if needed
				if (!g_Instance->m_KeepExceptionHandler)
				{
					g_Instance->RemoveVectoredExceptionHandler();
				}
				KX_SCOPEDLOG.SetSuccess();
			}

			template<class TRet, class... Args>
			static TRet InvokeHook(Args&&... args)
			{
				KX_SCOPEDLOG_ARGS(std::forward<Args>(args)...);

				HookCommonBefore();
				
				auto status = kxf::NtStatus::Fail();
				kxf::Utility::ScopeGuard atExit = [&]()
				{
					HookCommonAfter(status);
					KX_SCOPEDLOG.SetSuccess(status.IsSuccess());
				};
				return g_Instance->m_ImportAddressHook.CallOriginal(status, std::forward<Args>(args)...);
			}

		public:
			#if xSE_PLATFORM_SKSE64 || xSE_PLATFORM_F4SE
			static void* __cdecl HookFunc(void* a1, void* a2)
			{
				return InvokeHook<void*>(a1, a2);
			}
			#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE
			static char* __stdcall HookFunc()
			{
				return InvokeHook<char*>();
			}
			#else
				#error "Unsupported configuration"
			#endif
	};
}

namespace xSE
{
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
	
	kxf::String PreloadHandler::GetLibraryName()
	{
		return "xSE PluginPreloader";
	}
	kxf::Version PreloadHandler::GetLibraryVersion()
	{
		return "0.3";
	}
	PreloadHandler *PreloadHandler::GetInstance() noexcept
	{
		return g_Instance.get();
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
		#if xSE_PLATFORM_SKSE64 || xSE_PLATFORM_F4SE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / "WinHTTP.dll";
		#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / "WinMM.dll";
		#else
		#error "Unsupported configuration"
		#endif
	}

	void PreloadHandler::DoLoadPlugins()
	{
		KX_SCOPEDLOG_FUNC;

		// Install exception handler and remove it after loading is done
		InstallVectoredExceptionHandler();
		kxf::Utility::ScopeGuard atExit = [&]()
		{
			if (!m_KeepExceptionHandler && *m_LoadMethod != LoadMethod::ImportAddressHook)
			{
				RemoveVectoredExceptionHandler();
			}
		};

		// Begin loading
		kxf::FSPath pluginsDirectory = kxf::FSPath("Data") / xSE_FOLDER_NAME_W / "Plugins";
		KX_SCOPEDLOG.Info().Format("Searching directory '{}' for plugins", pluginsDirectory.GetFullPath());

		size_t itemsScanned = 0;
		switch (*m_InitializationMethod)
		{
			case InitializationMethod::Standard:
			{
				for (const kxf::FileItem& fileItem: m_InstallFS.EnumItems(pluginsDirectory, "*_preload.txt", kxf::FSActionFlag::LimitToFiles))
				{
					itemsScanned++;
					if (fileItem.IsNormalItem())
					{
						const kxf::FSPath libraryPath = pluginsDirectory / fileItem.GetName().BeforeLast('_') + ".dll";
						KX_SCOPEDLOG.Info().Format("Preload directive '{}' found, trying to load the corresponding library '{}'", fileItem.GetName(), libraryPath.GetFullPath());

						PluginStatus status = DoLoadSinglePlugin(libraryPath);
						LogLoadStatus(libraryPath, status);
					}
				}
				break;
			}
			case InitializationMethod::xSEPluginPreload:
			{
				const kxf::String routineName = xSE_NAME_W "Plugin_Preload";
				for (const kxf::FileItem& fileItem: m_InstallFS.EnumItems(pluginsDirectory, "*.dll", kxf::FSActionFlag::LimitToFiles))
				{
					itemsScanned++;
					if (fileItem.IsNormalItem())
					{
						const kxf::FSPath libraryPath = pluginsDirectory / fileItem.GetName();
						if (kxf::DynamicLibrary library(libraryPath, kxf::DynamicLibraryFlag::Resource); !library.IsNull() && library.ContainsExportedFunction(routineName))
						{
							KX_SCOPEDLOG.Info().Format("Preload directive '{}' found, trying to load the library '{}'", routineName, libraryPath.GetFullPath());
							library.Unload();

							PluginStatus status = DoLoadSinglePlugin(libraryPath);
							LogLoadStatus(libraryPath, status);
						}
					}
				}
				break;
			}
		};
		KX_SCOPEDLOG.Info().Format("Loading finished, {} plugins loaded, {} items scanned", m_LoadedLibraries.size(), itemsScanned);

		KX_SCOPEDLOG.SetSuccess();
	}
	void PreloadHandler::DoUnloadPlugins()
	{
		KX_SCOPEDLOG_FUNC;

		m_LoadedLibraries.clear();

		KX_SCOPEDLOG.SetSuccess();
	}
	PluginStatus PreloadHandler::DoLoadSinglePlugin(const kxf::FSPath& path)
	{
		KX_SCOPEDLOG_ARGS(path.GetName());

		kxf::DynamicLibrary pluginLibrary;
		PluginStatus pluginStatus = PluginStatus::FailedLoad;

		// Load plugin library
		const kxf::NtStatus loadStatus = Utility::SEHTryExcept([&]()
		{
			if (pluginLibrary.Load(path))
			{
				pluginStatus = PluginStatus::Loaded;
			}
			else
			{
				KX_SCOPEDLOG.Error().Format("Couldn't load plugin: {}", kxf::Win32Error::GetLastError());
				OnPluginLoadFailed(path);
			}
		});

		if (loadStatus)
		{
			if (pluginLibrary)
			{
				KX_SCOPEDLOG.Info().Format("Library is loaded, attempt to call the initialization routine");

				// Call initialization routine
				const kxf::NtStatus initializeStatus = Utility::SEHTryExcept([&]()
				{
					switch (*m_InitializationMethod)
					{
						case InitializationMethod::Standard:
						{
							using TInitialize = void(__cdecl*)(void);
							const char* routineName = "Initialize";

							if (auto initalize = pluginLibrary.GetExportedFunction<TInitialize>(routineName))
							{
								KX_SCOPEDLOG.Info().Format("Calling the initialization routine '{}'", routineName);
								
								std::invoke(*initalize);
								pluginStatus = PluginStatus::Initialized;
							}
							else
							{
								KX_SCOPEDLOG.Info().Format("No initialization routine '{}' found", routineName);
							}
							break;
						}
						case InitializationMethod::xSEPluginPreload:
						{
							using TInitialize = bool(__cdecl*)(void*);
							const char* routineName = xSE_NAME_A "Plugin_Preload";

							if (auto initalize = pluginLibrary.GetExportedFunction<TInitialize>(routineName))
							{
								KX_SCOPEDLOG.Info().Format("Calling the initialization routine '{}'", routineName);
								if (std::invoke(*initalize, nullptr))
								{
									pluginStatus = PluginStatus::Initialized;
								}
								else
								{
									KX_SCOPEDLOG.Warning().Format("Initialization routine reported false");
								}
							}
							else
							{
								KX_SCOPEDLOG.Info().Format("No initialization routine '{}' found", routineName);
							}
							break;
						}
						default:
						{
							KX_SCOPEDLOG.Critical().Format("Unknown initialization method: {}", *m_InitializationMethod);
							break;
						}
					};
				});

				if (initializeStatus)
				{
					m_LoadedLibraries.emplace_back(std::move(pluginLibrary));
				}
				else
				{
					pluginStatus = PluginStatus::FailedInitialize;
					KX_SCOPEDLOG.Error().Format("Exception occurred inside plugin's initialization routine: {}", initializeStatus);

					OnPluginLoadFailed(path);
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
			KX_SCOPEDLOG.Error().Format("Exception occurred while loading plugin library: {}", loadStatus);

			OnPluginLoadFailed(path);
		}

		KX_SCOPEDLOG.LogReturn(pluginStatus, pluginStatus == PluginStatus::Loaded || pluginStatus == PluginStatus::Initialized);
		return pluginStatus;
	}
	void PreloadHandler::OnPluginLoadFailed(const kxf::FSPath& path)
	{
		KX_SCOPEDLOG_ARGS(path.GetName());

		const kxf::NtStatus status = Utility::SEHTryExcept([&]()
		{
			KX_SCOPEDLOG.Info().Format("Trying to read library dependencies list");

			kxf::DynamicLibrary library(path, kxf::DynamicLibraryFlag::Resource);
			if (library)
			{
				KX_SCOPEDLOG.Info().Format("Dependency module names:");
				library.EnumDependencyModuleNames([&](kxf::String moduleName)
				{
					kxf::DynamicLibrary dependencyModule(moduleName, kxf::DynamicLibraryFlag::Resource);
					if (dependencyModule)
					{
						KX_SCOPEDLOG.Info().Format("Module name '{}' loaded successfully as a resource from '{}'", moduleName, dependencyModule.GetFilePath().GetFullPath());
					}
					else
					{
						auto lastError = kxf::Win32Error::GetLastError();
						KX_SCOPEDLOG.Warning().Format("Couldn't load the dependency library as a resource: {}", moduleName, lastError);

						// Try to look for recursive dependencies if the file exists but couldn't be loaded
						if (lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PATH_NOT_FOUND)
						{
							OnPluginLoadFailed(moduleName);
						}
					}
					return kxf::CallbackCommand::Continue;
				});
			}
			else
			{
				KX_SCOPEDLOG.Warning().Format("Couldn't load the library as a resource for diagnostics: {}", kxf::Win32Error::GetLastError());
			}
		});

		if (!status)
		{
			KX_SCOPEDLOG.Error().Format("Exception occurred while scanning plugin library dependencies: {}", status);
		}

		KX_SCOPEDLOG.SetSuccess(status.IsSuccess());
	}

	bool PreloadHandler::CheckAllowedProcesses() const
	{
		const kxf::String thisExecutableName = m_ExecutablePath.GetName();
		kxf::Log::Info("<{}> Checking process name to determine if it's allowed to preload plugins. Following process names are allowed: [{}]", thisExecutableName, [&]() -> kxf::String
		{
			if (!m_AllowedProcessNames.empty())
			{
				kxf::String result;
				for (const kxf::String& name: m_AllowedProcessNames)
				{
					if (!result.IsEmpty())
					{
						result += ", ";
					}

					result += '\'';
					result += name;
					result += '\'';
				}
				return result;
			}
			else
			{
				return "<none>";
			}
		}());

		return kxf::Utility::Container::Contains(m_AllowedProcessNames, [&](const kxf::String& name)
		{
			if (name.IsSameAs(thisExecutableName, kxf::StringActionFlag::IgnoreCase))
			{
				kxf::Log::Info("<{}> Match found: '{}'", thisExecutableName, name);
				return true;
			}
			return false;
		});
	}
	void PreloadHandler::LoadOriginalLibrary()
	{
		kxf::FSPath path = GetOriginalLibraryPath();
		kxf::Log::Info("<{}> Loading original library", path.GetFullPath());

		ClearOriginalFunctions();
		if (m_OriginalLibrary.Load(path))
		{
			kxf::Log::Info("<{}> Original library loaded successfully", path.GetFullPath());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			kxf::Log::Info("<{}> Couldn't load library: [Win32: {} ({})]", path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
		}
	}
	void PreloadHandler::UnloadOriginalLibrary()
	{
		kxf::Log::Info("<{}> Unloading original library", m_OriginalLibrary.GetFilePath().GetFullPath());

		m_OriginalLibrary.Unload();
		ClearOriginalFunctions();
	}
	void PreloadHandler::ClearOriginalFunctions()
	{
		std::fill_n(GetFunctions(), GetFunctionsCount(), nullptr);
	}

	bool PreloadHandler::InstallVectoredExceptionHandler()
	{
		if (m_InstallExceptionHandler)
		{
			m_VectoredExceptionHandler.Install([](_EXCEPTION_POINTERS* exceptionInfo) -> LONG
			{
				if (g_Instance && exceptionInfo)
				{
					return g_Instance->OnVectoredException(*exceptionInfo);
				}
				return EXCEPTION_CONTINUE_SEARCH;
			}, VectoredExceptionHandler::Mode::ExceptionHandler, VectoredExceptionHandler::Order::First);
			kxf::Log::Info("Installing vectored exception handler: {}", m_VectoredExceptionHandler.IsInstalled() ? "success" : "failed");

			return m_VectoredExceptionHandler.IsInstalled();
		}
		else
		{
			kxf::Log::Info("Installing vectored exception handler: disabled in the config file");
			return false;
		}
	}
	void PreloadHandler::RemoveVectoredExceptionHandler()
	{
		kxf::Log::Info("Removing vectored exception handler: {}", m_VectoredExceptionHandler.Remove() ? "success" : "failed (not installed or already removed)");
	}
	uint32_t PreloadHandler::OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		KX_SCOPEDLOG_FUNC;

		KX_SCOPEDLOG.Warning() << DumpExceptionInformation(exceptionInfo);

		return EXCEPTION_CONTINUE_SEARCH;
	}
	uint32_t PreloadHandler::OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		KX_SCOPEDLOG_FUNC;

		KX_SCOPEDLOG.Warning() << DumpExceptionInformation(exceptionInfo);

		return EXCEPTION_CONTINUE_SEARCH;
	}
	kxf::String PreloadHandler::DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const
	{
		kxf::String result;

		const auto& context = exceptionInfo.ContextRecord;
		#if _WIN64
		result.Format("ContextRecord: [RAX: {:#016x}], [RBX: {:#016x}], [RCX: {:#016x}], [RDX: {:#016x}], [RBP: {:#016x}], [RDI: {:#016x}], [RIP: {:#016x}], "
					  "[R08: {:#016x}], [R09: {:#016x}], [R10: {:#016x}], [R11: {:#016x}], [R12: {:#016x}], [R13: {:#016x}], [R14: {:#016x}], [R15: {:#016x}].\n",
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
					  context->R15);
		#else
		result.Format("ContextRecord: [EAX: {:#08x}], [EBX: {:#08x}], [ECX: {:#08x}], [EDX: {:#08x}], [EBP: {:#08x}], [EDI: {:#08x}], [EIP: {:#08x}].\n",
					  context->Eax,
					  context->Ebx,
					  context->Ecx,
					  context->Edx,
					  context->Ebp,
					  context->Edi,
					  context->Eip);
		#endif

		const auto& exception = exceptionInfo.ExceptionRecord;

		auto GetExceptionMessage = [](kxf::NtStatus status)
		{
			kxf::String exceptionCodeMessage = status.GetMessage();
			exceptionCodeMessage.Replace("\r\n", "; ");
			exceptionCodeMessage.Replace("\r", "; ");
			exceptionCodeMessage.Replace("\n", "; ");
			exceptionCodeMessage.TrimBoth();

			return exceptionCodeMessage;
		};
		result.Format("ExceptionRecord:\n\tExceptionCode: [NtStatus: ({:#08x}) '{}']\n\tExceptionFlags: {:#08x}\n\tExceptionAddress: {:{}#0x}\n\tExceptionRecord: {:{}#0x}",
					  exception->ExceptionCode,
					  GetExceptionMessage(exception->ExceptionCode),
					  exception->ExceptionFlags,
					  reinterpret_cast<size_t>(exception->ExceptionAddress), sizeof(void*),
					  reinterpret_cast<size_t>(exception->ExceptionRecord), sizeof(void*)
		);

		return result;
	}

	bool PreloadHandler::InitializeFramework()
	{
		KX_SCOPEDLOG_FUNC;

		// Register modules
		using kxf::NativeAPISet;

		auto& loader = kxf::NativeAPILoader::GetInstance();
		loader.LoadLibraries(
		{
			NativeAPISet::NtDLL,
			NativeAPISet::Kernel32,
			NativeAPISet::KernelBase,
			NativeAPISet::User32,
			NativeAPISet::ShlWAPI,
			NativeAPISet::DbgHelp
		});

		wxModule::RegisterModules();
		if (wxModule::InitializeModules() && m_Application->OnInit())
		{
			KX_SCOPEDLOG.SetSuccess();
			return true;
		}
		return false;
	}
	void PreloadHandler::LogEnvironmentInfo() const
	{
		if (const auto versionInfo = kxf::System::GetVersionInfo())
		{
			const kxf::System::KernelVersion kernel = versionInfo->Kernel;
			kxf::Log::InfoCategory(LogCategory::Environment, "Operation system: '{}' {}.{}.{}, product type: {}",
								   kxf::System::GetProductName(*versionInfo),
								   kernel.Major, kernel.Minor, kernel.Build,
								   kxf::ToInt(versionInfo->ProductType)
			);

			if (kernel.ServicePackMajor > 0)
			{
				kxf::Log::InfoCategory(LogCategory::Environment, "System service pack: '{}' {}.{}", versionInfo->ServicePack, kernel.ServicePackMajor, kernel.ServicePackMinor);
			}
		}
		else
		{
			kxf::Log::WarningCategory(LogCategory::Environment, "Couldn't query system version");
		}

		kxf::Log::InfoCategory(LogCategory::Environment, "System default/preferred locale: '{}'/'{}'",
							   kxf::Locale::GetSystemDefault().GetName(),
							   kxf::Locale::GetSystemPreferred().GetName()
		);
	}
	void PreloadHandler::LogCurrentModuleInfo() const
	{
		auto currentModule = kxf::DynamicLibrary::GetCurrentModule();

		kxf::Log::InfoCategory(LogCategory::CurrentModule, "Binary: '{}'", currentModule.GetFilePath().GetFullPath());
		kxf::Log::InfoCategory(LogCategory::CurrentModule, "{} v{} loaded", GetLibraryName(), GetLibraryVersion().ToString());
	}
	kxf::ExecutableVersionResource PreloadHandler::LogHostProcessInfo() const
	{
		kxf::Log::InfoCategory(LogCategory::HostProcess, "Binary: '{}'", m_ExecutablePath.GetFullPath());

		kxf::ExecutableVersionResource resourceInfo(m_ExecutablePath);
		if (resourceInfo)
		{
			kxf::Log::InfoCategory(LogCategory::HostProcess, "Version: {}", resourceInfo.GetAnyVersion());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			kxf::Log::InfoCategory(LogCategory::HostProcess, "Couldn't load host process binary. [Win32: '{}' ({})]", lastError.GetMessage(), lastError.GetValue());
		}

		return resourceInfo;
	}
	kxf::ExecutableVersionResource PreloadHandler::LogScriptExtenderInfo(const kxf::ExecutableVersionResource& hostResourceInfo) const
	{
		const kxf::FSPath loaderPath = m_InstallFS.ResolvePath(xSE_FOLDER_NAME_W "_Loader.exe");
		kxf::Log::InfoCategory(LogCategory::ScriptExtender, "Platform: {}", xSE_NAME_W);
		kxf::Log::InfoCategory(LogCategory::ScriptExtender, "Loader: '{}'", loaderPath.GetFullPath());
		if (!m_InstallFS.FileExist(loaderPath))
		{
			kxf::Log::WarningCategory(LogCategory::ScriptExtender, "File not found: '{}'", loaderPath.GetFullPath());
		}

		const auto versionString = [&]()
		{
			auto versionString = hostResourceInfo.GetAnyVersion();

			// Remove any version components after the third one
			size_t count = 0;
			for (size_t i = 0; i < versionString.length(); i++)
			{
				if (versionString[i] == '.')
				{
					count++;
					if (count == 3)
					{
						versionString.Truncate(i);
						break;
					}
				}
			}

			versionString.Replace('.', '_');
			return versionString;
		}();
		auto libraryPath = m_InstallFS.ResolvePath(kxf::Format("{}_{}.dll", xSE_FOLDER_NAME_W, versionString));
		kxf::Log::InfoCategory(LogCategory::ScriptExtender, "Library: '{}'", libraryPath.GetFullPath());
		if (!m_InstallFS.FileExist(libraryPath))
		{
			kxf::Log::WarningCategory(LogCategory::ScriptExtender, "File not found: '{}'", libraryPath.GetFullPath());
		}

		m_Application->Bind(kxf::DynamicLibraryEvent::EvtLoaded, [this, name = libraryPath.GetName()](kxf::DynamicLibraryEvent& event)
		{
			if (event.GetBaseName().GetName().IsSameAs(name, kxf::StringActionFlag::IgnoreCase))
			{
				kxf::Log::InfoCategory(LogCategory::ScriptExtender, "{} library loaded", xSE_NAME_W);
			}
		}, kxf::BindEventFlag::AlwaysSkip);

		kxf::ExecutableVersionResource extenderResourceInfo(loaderPath);
		if (extenderResourceInfo)
		{
			kxf::Log::InfoCategory(LogCategory::ScriptExtender, "Version: {}", extenderResourceInfo.GetAnyVersion());
		}
		else
		{
			kxf::Log::WarningCategory(LogCategory::ScriptExtender, "Couldn't load {} binary, probably not installed: {}", xSE_NAME_W, kxf::Win32Error::GetLastError());
		}

		return extenderResourceInfo;
	}

	bool PreloadHandler::OnDLLMain(HMODULE handle, uint32_t event)
	{
		switch (event)
		{
			case DLL_PROCESS_ATTACH:
			{
				if (*m_LoadMethod == LoadMethod::OnProcessAttach || *m_LoadMethod == LoadMethod::ImportAddressHook)
				{
					KX_SCOPEDLOG_ARGS(handle, event);

					DisableThreadLibraryCalls(handle);

					if (*m_LoadMethod == LoadMethod::OnProcessAttach)
					{
						LoadPlugins();
					}
					else if (*m_LoadMethod == LoadMethod::ImportAddressHook)
					{
						HookImportTable();
					}

					KX_SCOPEDLOG.SetSuccess();
				}
				else if (*m_LoadMethod == LoadMethod::OnThreadAttach)
				{
					m_WatchThreadAttach = true;
				}

				break;
			}
			case DLL_THREAD_ATTACH:
			{
				if (!m_WatchThreadAttach)
				{
					break;
				}

				if (*m_LoadMethod == LoadMethod::OnThreadAttach)
				{
					KX_SCOPEDLOG_ARGS(handle, event);

					const size_t threadCounter = ++m_ThreadAttachCount;
					kxf::Log::Info("Attached thread #{}", threadCounter);

					auto& options = GetLoadMethodOptions<LoadMethod::OnThreadAttach>();
					if (options.ThreadNumber == threadCounter)
					{
						DisableThreadLibraryCalls(handle);
						LoadPlugins();
					}

					KX_SCOPEDLOG.SetSuccess();
				}

				break;
			}
			case DLL_PROCESS_DETACH:
			{
				KX_SCOPEDLOG_ARGS(handle, event);

				PreloadHandler::DestroyInstance();

				KX_SCOPEDLOG.SetSuccess();
				break;
			}
		};

		return true;
	}
	bool PreloadHandler::DisableThreadLibraryCalls(HMODULE handle)
	{
		KX_SCOPEDLOG_ARGS(handle);

		m_WatchThreadAttach = false;
		if (::DisableThreadLibraryCalls(handle))
		{
			KX_SCOPEDLOG.SetSuccess();
			return true;
		}
		else
		{
			KX_SCOPEDLOG.Error() << kxf::Win32Error::GetLastError();
			return false;
		}
	}

	bool PreloadHandler::HookImportTable()
	{
		KX_SCOPEDLOG_FUNC;
		using namespace PluginPreloader;

		if (!m_PluginsLoadAllowed)
		{
			KX_SCOPEDLOG.Info().Format("Plugins preload disabled for this process, skipping hook installation");
			KX_SCOPEDLOG.LogReturn(false);

			return false;
		}

		if (m_HookDelay.IsPositive())
		{
			KX_SCOPEDLOG.Info().Format("Hooking is delayed by '{}' ms, waiting...", m_HookDelay.GetMilliseconds());
			::Sleep(m_HookDelay.GetMilliseconds());
			KX_SCOPEDLOG.Info().Format("Wait time is out, continuing hooking");
		}

		KX_SCOPEDLOG.Info().Format("Hooking function '{}' from library '{}'", m_ImportAddressHook.FunctionName, m_ImportAddressHook.LibraryName);
		m_ImportAddressHook.SaveOriginal(Detour::FunctionIAT(&ImportAddressHookHandler::HookFunc, m_ImportAddressHook.LibraryName.nc_str(), m_ImportAddressHook.FunctionName.nc_str()));

		if (m_ImportAddressHook.IsHooked())
		{
			KX_SCOPEDLOG.Info().Format("Success [Hooked={:#0{}x}], [Original={:#0{}x}]",
									   reinterpret_cast<size_t>(&ImportAddressHookHandler::HookFunc), sizeof(void*),
									   reinterpret_cast<size_t>(m_ImportAddressHook.GetOriginal()), sizeof(void*)
			);
			KX_SCOPEDLOG.LogReturn(true);

			return true;
		}
		else
		{
			KX_SCOPEDLOG.Error().Format("Unable to hook import table function");
			KX_SCOPEDLOG.LogReturn(false, false);

			return false;
		}
	}
	bool PreloadHandler::LoadPlugins()
	{
		KX_SCOPEDLOG_FUNC;

		if (!m_PluginsLoadAllowed)
		{
			KX_SCOPEDLOG.Info().Format("Plugins preload disabled for this process");
			KX_SCOPEDLOG.LogReturn(false);

			return false;
		}

		if (!m_PluginsLoaded)
		{
			KX_SCOPEDLOG.Info().Format("Loading plugins");
			if (m_LoadDelay.IsPositive())
			{
				KX_SCOPEDLOG.Info().Format("Loading plugins is delayed by '{}' ms, waiting...", m_LoadDelay.GetMilliseconds());
				::Sleep(m_LoadDelay.GetMilliseconds());
				KX_SCOPEDLOG.Info().Format("Wait time is out, continuing loading");
			}

			DoLoadPlugins();
			m_PluginsLoaded = true;

			KX_SCOPEDLOG.LogReturn(true);
			return true;
		}

		KX_SCOPEDLOG.LogReturn(false);
		return false;
	}

	PreloadHandler::PreloadHandler()
	{
		m_Application = std::make_shared<Application>(*this);
		m_InstallFS.SetLookupDirectory(kxf::NativeFileSystem::GetExecutingModuleRootDirectory());
		m_ConfigFS.SetLookupDirectory(kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::Documents) / "My Games" / xSE_CONFIG_FOLDER_NAME_W / xSE_FOLDER_NAME_W);

		// Initialize plugins directory
		m_ExecutablePath = kxf::DynamicLibrary::GetExecutingModule().GetFilePath();

		// Open log
		{
			using namespace kxf;

			auto stream = m_ConfigFS.OpenToWrite(g_LogFileName, IOStreamDisposition::CreateAlways, IOStreamShare::Read, FSActionFlag::CreateDirectoryTree|FSActionFlag::Recursive);
			if (!stream)
			{
				stream = m_InstallFS.OpenToWrite(g_LogFileName, IOStreamDisposition::CreateAlways, IOStreamShare::Read, FSActionFlag::CreateDirectoryTree|FSActionFlag::Recursive);
			}

			kxf::ScopedLoggerGlobalContext::Initialize(std::make_shared<kxf::ScopedLoggerSingleFileContext>(std::move(stream)));
		}

		KX_SCOPEDLOG_FUNC;
		KX_SCOPEDLOG.Info() KX_SCOPEDLOG_VALUE_AS(m_ExecutablePath, m_ExecutablePath.GetFullPath());

		// Init framework
		if (!m_Application->OnCreate() || !InitializeFramework())
		{
			kxf::Log::Info("Error occurred during the initialization process");
			return;
		}
		LogCurrentModuleInfo();
		auto hostResourceInfo = LogHostProcessInfo();
		auto extenderResourceInfo = LogScriptExtenderInfo(hostResourceInfo);
		LogEnvironmentInfo();

		// Load config
		kxf::Log::Info("Loading configuration from '{}'", m_InstallFS.ResolvePath(g_ConfigFileName).GetFullPath());
		if (auto readStream = m_InstallFS.OpenToRead(g_ConfigFileName); readStream && m_Config.Load(*readStream))
		{
			KX_SCOPEDLOG.Info().Format("Configuration file successfully loaded");
		}
		else
		{
			if (readStream)
			{
				KX_SCOPEDLOG.Warning().Format("Couldn't load configuration: {}. The file is found, but can not be loaded, default configuration will be used", readStream->GetLastError());
			}
			else
			{
				KX_SCOPEDLOG.Warning().Format("Couldn't load configuration: {}, default configuration will be used", kxf::Win32Error::GetLastError());
			}
			readStream = nullptr;

			// Restore the default config on disk and load it
			KX_SCOPEDLOG.Info().Format("Restoring default configuration");

			auto defaultXML = kxf::DynamicLibrary::GetCurrentModule().GetResource("XML", kxf::ToString(IDR_XML_DEFAULT_CONFIGURATION));
			if (m_Config.Load(std::string_view(reinterpret_cast<const char*>(defaultXML.data()), defaultXML.size())))
			{
				KX_SCOPEDLOG.Info().Format("Default configuration successfully loaded");

				auto writeStream = m_InstallFS.OpenToWrite(g_ConfigFileName);
				if (writeStream && writeStream->WriteAll(defaultXML.data(), defaultXML.size_bytes()))
				{
					KX_SCOPEDLOG.Info().Format("Default configuration successfully saved to disk");
				}
				else
				{
					KX_SCOPEDLOG.Warning().Format("Couldn't save default configuration to disk: {}", kxf::Win32Error::GetLastError());
				}
			}
			else
			{
				KX_SCOPEDLOG.Warning().Format("Couldn't load default configuration");
			}
		}

		m_OriginalLibraryPath = [&]()
		{
			kxf::String path = m_Config.QueryElement("xSE/PluginPreloader/OriginalLibrary").GetValue();
			if (path.IsEmpty())
			{
				KX_SCOPEDLOG.Info().Format("Original library path is not set, using default '{}'", GetOriginalLibraryDefaultPath().GetFullPath());
			}
			else
			{
				KX_SCOPEDLOG.Info().Format("Original library path is set to '{}'", path);
			}
			return path;
		}();

		m_InstallExceptionHandler = [&]()
		{
			return m_Config.QueryElement("xSE/PluginPreloader/InstallExceptionHandler").GetValueBool(true);
		}();
		m_KeepExceptionHandler = [&]()
		{
			return m_Config.QueryElement("xSE/PluginPreloader/KeepExceptionHandler").GetValueBool();
		}();

		m_LoadMethod = [&]() -> decltype(m_LoadMethod)
		{
			auto rootNode = m_Config.QueryElement("xSE/PluginPreloader/LoadMethod");
			auto methodName = rootNode.GetAttribute("Name");

			if (auto method = LoadMethodFromString(methodName))
			{
				kxf::Log::Info("Load method is set to '{}', loading method parameters", methodName);

				kxf::XMLNode methodNode = rootNode.GetFirstChildElement(methodName);
				switch (*method)
				{
					case LoadMethod::OnProcessAttach:
					{
						KX_SCOPEDLOG.Info().Format("No parameters");
						return *method;
					}
					case LoadMethod::OnThreadAttach:
					{
						auto value = methodNode.GetFirstChildElement("ThreadNumber").GetValueInt(2);
						if (value < 0)
						{
							value = 0;
						}
						m_OnThreadAttach.ThreadNumber = static_cast<size_t>(value);

						KX_SCOPEDLOG.Info().Format("ThreadNumber = {}", m_OnThreadAttach.ThreadNumber);
						return *method;
					}
					case LoadMethod::ImportAddressHook:
					{
						m_ImportAddressHook.LibraryName = methodNode.GetFirstChildElement("LibraryName").GetValue();
						m_ImportAddressHook.FunctionName = methodNode.GetFirstChildElement("FunctionName").GetValue();

						KX_SCOPEDLOG.Info().Format("LibraryName = {}", m_ImportAddressHook.LibraryName);
						KX_SCOPEDLOG.Info().Format("FunctionName = {}", m_ImportAddressHook.FunctionName);

						if (!m_ImportAddressHook.IsNull())
						{
							return *method;
						}
						break;
					}
				};
			}
			else
			{
				KX_SCOPEDLOG.Critical().Format("Unknown load method: '{}'", methodName);
			}
			return {};
		}();

		m_InitializationMethod = [&]() -> decltype(m_InitializationMethod)
		{
			auto rootNode = m_Config.QueryElement("xSE/PluginPreloader/InitializationMethod");
			auto methodName = rootNode.GetAttribute("Name");

			if (auto method = InitializationMethodFromString(methodName))
			{
				KX_SCOPEDLOG.Info().Format("Initialization method is set to '{}'", methodName);
				return *method;
			}
			else
			{
				KX_SCOPEDLOG.Critical().Format("Unknown initialization method: '{}'", methodName);
				return {};
			}
		}();

		m_LoadDelay = [&]()
		{
			return kxf::TimeSpan::Milliseconds(m_Config.QueryElement("xSE/PluginPreloader/LoadDelay").GetValueInt(0));
		}();
		m_HookDelay = [&]()
		{
			return kxf::TimeSpan::Milliseconds(m_Config.QueryElement("xSE/PluginPreloader/HookDelay").GetValueInt(0));
		}();

		m_AllowedProcessNames = [&]()
		{
			std::vector<kxf::String> processes;
			for (const kxf::XMLNode& itemNode: m_Config.QueryElement("xSE/PluginPreloader/Processes").EnumChildElements("Item"))
			{
				if (itemNode.GetAttributeBool("Allow"))
				{
					if (processes.emplace_back(itemNode.GetAttribute("Name")).IsEmpty())
					{
						processes.pop_back();
					}
				}
			}
			return processes;
		}();

		// Check processes, if we are not allowed to preload inside this process set the flag and don't load plugins but still load the original library.
		m_PluginsLoadAllowed = CheckAllowedProcesses();
		if (!m_PluginsLoadAllowed)
		{
			KX_SCOPEDLOG.Warning().Format("This process is not allowed to preload plugins: {}", m_ExecutablePath.GetName());
		}

		// Load the original library
		LoadOriginalLibrary();
		if (m_OriginalLibrary)
		{
			LoadOriginalLibraryFunctions();
		}
		else
		{
			KX_SCOPEDLOG.Critical().Format("Can't load original library, terminating");
		}

		KX_SCOPEDLOG.SetSuccess();
	}
	PreloadHandler::~PreloadHandler()
	{
		KX_SCOPEDLOG_FUNC;

		if (m_OriginalLibrary)
		{
			DoUnloadPlugins();
			UnloadOriginalLibrary();
		}
		RemoveVectoredExceptionHandler();
		
		KX_SCOPEDLOG.SetSuccess();
	}
}
