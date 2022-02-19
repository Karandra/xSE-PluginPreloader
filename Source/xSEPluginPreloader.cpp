#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "ScriptExtenderDefinesBase.h"
#include "Application.h"
#include "Detour.h"

#include <kxf/Application/GUIApplication.h>
#include <kxf/IO/StreamReaderWriter.h>
#include <kxf/Log/Common.h>
#include <kxf/Localization/Locale.h>
#include <kxf/System/ExecutableVersionResource.h>
#include <kxf/System/SystemInformation.h>
#include <kxf/System/ShellOperations.h>
#include <kxf/System/Win32Error.h>
#include <kxf/System/NtStatus.h>
#include <kxf/System/NativeAPI.h>
#include <kxf/System/DynamicLibraryEvent.h>
#include <kxf/Threading/Common.h>
#include <kxf/Utility/System.h>
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
				g_Instance->LogIndent(1, "<{}> Plugin loaded successfully", path.GetName());
				break;
			}
			case PluginStatus::Initialized:
			{
				g_Instance->LogIndent(1, "<{}> Plugin 'loaded successfully, 'Initialize' executed successfully", path.GetName());
				break;
			}
			case PluginStatus::FailedLoad:
			{
				g_Instance->LogIndent(1, "<{}> Plugin failed to load", path.GetName());
				break;
			}
			case PluginStatus::FailedInitialize:
			{
				g_Instance->LogIndent(1, "<{}> Plugin failed to load. Exception was thrown during 'Initialize' execution", path.GetName());
				break;
			}
			default:
			{
				g_Instance->LogIndent(1, "<{}> Unknown plugin status code ({})", path.GetName(), static_cast<uint32_t>(status));
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
				g_Instance->Log("<ImportAddressHook> Enter hooked function");

				if (!g_Instance->IsPluginsLoaded())
				{
					g_Instance->Log("<ImportAddressHook> LoadPlugins");
					g_Instance->LoadPlugins();
				}
				else
				{
					g_Instance->Log("<ImportAddressHook> Plugins are already loaded");
				}

				g_Instance->Log("<ImportAddressHook> Calling unhooked function");
			}
			static void HookCommonAfter(const kxf::NtStatus& status)
			{
				if (status)
				{
					g_Instance->Log("<ImportAddressHook> Unhooked function returned successfully");
				}
				else
				{
					g_Instance->Log("<ImportAddressHook> Exception occurred while executing the unhooked function: [NtStatus: '{}' ({})]", status.GetMessage(), status.GetValue());
				}

				// Remove exception handler if needed
				if (!g_Instance->m_KeepExceptionHandler)
				{
					g_Instance->RemoveVectoredExceptionHandler();
				}

				g_Instance->Log("<ImportAddressHook> Leave hooked function");
			}

			template<class TRet, class... Args>
			static TRet InvokeHook(Args&&... args)
			{
				HookCommonBefore();
				
				auto status = kxf::NtStatus::Fail();
				kxf::Utility::ScopeGuard atExit = [&]()
				{
					HookCommonAfter(status);
				};
				return g_Instance->m_ImportAddressHook.CallUnhooked(status, std::forward<Args>(args)...);
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
		return "0.2.5.2";
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
		#if xSE_PLATFORM_SKSE64 || xSE_PLATFORM_F4SE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / "IpHlpAPI.dll";
		#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE
		return kxf::Shell::GetKnownDirectory(kxf::KnownDirectoryID::System) / "WinMM.dll";
		#else
		#error "Unsupported configuration"
		#endif
	}

	void PreloadHandler::DoLoadPlugins()
	{
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
		Log("Searching directory '{}' for plugins", m_PluginsDirectory.GetFullPath());

		size_t itemsScanned = 0;
		for (const kxf::FileItem& fileItem: m_FileSystem.EnumItems(m_PluginsDirectory, "*_preload.txt", kxf::FSActionFlag::LimitToFiles))
		{
			itemsScanned++;
			if (fileItem.IsNormalItem())
			{
				const kxf::FSPath libraryPath = m_PluginsDirectory / fileItem.GetName().BeforeLast('_') + ".dll";
				Log("Preload directive '{}' found, trying to load the corresponding library '{}'", fileItem.GetName(), libraryPath.GetFullPath());

				PluginStatus status = DoLoadSinglePlugin(libraryPath);
				LogLoadStatus(libraryPath, status);
			}
		}

		Log("Loading finished, {} plugins loaded, {} items scanned", m_LoadedLibraries.size(), itemsScanned);
	}
	void PreloadHandler::DoUnloadPlugins()
	{
		Log("Unloading plugins");
		m_LoadedLibraries.clear();
	}
	PluginStatus PreloadHandler::DoLoadSinglePlugin(const kxf::FSPath& path)
	{
		LogIndent(1,"<{}> Trying to load", path.GetName());

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
				auto lastError = kxf::Win32Error::GetLastError();
				LogIndent(1, "<{}> Couldn't load plugin: [Win32: '{}' ({})]", path.GetName(), lastError.GetMessage(), lastError.GetValue());

				OnPluginLoadFailed(path);
			}
		});

		if (loadStatus)
		{
			if (pluginLibrary)
			{
				LogIndent(1, "<{}> Library is loaded, attempt to call the initialization routine", path.GetName());

				// Call initialization routine
				const kxf::NtStatus initializeStatus = Utility::SEHTryExcept([&]()
				{
					using TInitialize = void(__cdecl*)(void);
					if (auto initalize = pluginLibrary.GetExportedFunction<TInitialize>("Initialize"))
					{
						LogIndent(1, "<{}> Calling the initialization routine", path.GetName());
						std::invoke(*initalize);
						pluginStatus = PluginStatus::Initialized;
					}
					else
					{
						LogIndent(1, "<{}> No initialization routine found", path.GetName());
					}
				});

				if (initializeStatus)
				{
					m_LoadedLibraries.emplace_back(std::move(pluginLibrary));
				}
				else
				{
					pluginStatus = PluginStatus::FailedInitialize;
					LogIndent(1, "<{}> Exception occurred inside plugin's initialization routine: [NtStatus: '{}' ({})]", path.GetName(), initializeStatus.GetMessage(), initializeStatus.GetValue());

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
			LogIndent(1, "<{}> Exception occurred while loading plugin library: [NtStatus: '{}' ({})]", path.GetName(), loadStatus.GetMessage(), loadStatus.GetValue());

			OnPluginLoadFailed(path);
		}

		return pluginStatus;
	}
	void PreloadHandler::OnPluginLoadFailed(const kxf::FSPath& path, size_t logIndentOffset)
	{
		const kxf::NtStatus status = Utility::SEHTryExcept([&]()
		{
			LogIndent(logIndentOffset + 1, "<{}> Trying to read library dependencies list", path.GetName());

			kxf::DynamicLibrary library(path, kxf::DynamicLibraryFlag::Resource);
			if (library)
			{
				LogIndent(logIndentOffset + 1, "<{}> Dependency module names:", path.GetName());
				library.EnumDependencyModuleNames([&](kxf::String moduleName)
				{
					kxf::DynamicLibrary dependencyModule(moduleName, kxf::DynamicLibraryFlag::Resource);
					if (dependencyModule)
					{
						LogIndent(logIndentOffset + 2, "<{}> Module name '{}' loaded successfully as a resource from '{}'", path.GetName(), moduleName, dependencyModule.GetFilePath().GetFullPath());
					}
					else
					{
						LogIndent(logIndentOffset + 2, "<{}> Module name '{}' couldn't be loaded", path.GetName(), moduleName);

						auto lastError = kxf::Win32Error::GetLastError();
						LogIndent(logIndentOffset + 3, "<{}> Couldn't load the dependency library as a resource: [Win32: '{}' ({})]", moduleName, lastError.GetMessage(), lastError.GetValue());

						// Try to look for recursive dependencies if the file exists but couldn't be loaded
						if (lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PATH_NOT_FOUND)
						{
							OnPluginLoadFailed(moduleName, logIndentOffset + 2);
						}
					}
					return true;
				});
			}
			else
			{
				auto lastError = kxf::Win32Error::GetLastError();
				LogIndent(logIndentOffset + 1, "<{}> Couldn't load the library as a resource for diagnostics: [Win32: '{}' ({})]", path.GetName(), lastError.GetMessage(), lastError.GetValue());
			}
		});

		if (!status)
		{
			LogIndent(logIndentOffset + 1, "<{}> Exception occurred while scanning plugin library dependencies: [NtStatus: '{}' ({})]", path.GetName(), status.GetMessage(), status.GetValue());
		}
	}

	bool PreloadHandler::CheckAllowedProcesses() const
	{
		const kxf::String thisExecutableName = m_ExecutablePath.GetName();
		Log("<{}> Checking process name to determine if it's allowed to preload plugins. Following process names are allowed: [{}]", thisExecutableName, [&]() -> kxf::String
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
				Log("<{}> Match found: '{}'", thisExecutableName, name);
				return true;
			}
			return false;
		});
	}
	void PreloadHandler::LoadOriginalLibrary()
	{
		kxf::FSPath path = GetOriginalLibraryPath();
		Log("<{}> Loading original library", path.GetFullPath());

		ClearOriginalFunctions();
		if (m_OriginalLibrary.Load(path))
		{
			Log("<{}> Original library loaded successfully", path.GetFullPath());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log("<{}> Couldn't load library: [Win32: {} ({})]", path.GetFullPath(), lastError.GetMessage(), lastError.GetValue());
		}
	}
	void PreloadHandler::UnloadOriginalLibrary()
	{
		Log("<{}> Unloading original library", m_OriginalLibrary.GetFilePath().GetFullPath());

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
				kxf::String timeStampFormatted = kxf::Format("[{}:{:0>3}|TID:{:0>9}] ", timeStamp.FormatISOCombined(' '), timeStamp.GetMillisecond(), kxf::Threading::GetCurrentThreadID());

				logString.Prepend(std::move(timeStampFormatted));
			}
			logString += '\n';

			if (kxf::WriteLockGuard lock(m_LogLock); true)
			{
				writer.WriteStringUTF8(logString);
				m_LogStream->Flush();
			}
			return logString.length();
		}
		return 0;
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
			Log("Installing vectored exception handler: {}", m_VectoredExceptionHandler.IsInstalled() ? "success" : "failed");

			return m_VectoredExceptionHandler.IsInstalled();
		}
		else
		{
			Log("Installing vectored exception handler: disabled in the config file");
			return false;
		}
	}
	void PreloadHandler::RemoveVectoredExceptionHandler()
	{
		Log("Removing vectored exception handler: {}", m_VectoredExceptionHandler.Remove() ? "success" : "failed (not installed or already removed)");
	}
	uint32_t PreloadHandler::OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log("Caught vectored continue:");
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	uint32_t PreloadHandler::OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo)
	{
		Log("Caught vectored exception:");
		LogNoTime(DumpExceptionInformation(exceptionInfo));

		return EXCEPTION_CONTINUE_SEARCH;
	}
	kxf::String PreloadHandler::DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const
	{
		kxf::String result;

		const auto& context = exceptionInfo.ContextRecord;
		#if _WIN64
		result += kxf::Format("ContextRecord: [RAX: {:#016x}], [RBX: {:#016x}], [RCX: {:#016x}], [RDX: {:#016x}], [RBP: {:#016x}], [RDI: {:#016x}], [RIP: {:#016x}], "
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
		result += kxf::Format("ContextRecord: [EAX: {:#08x}], [EBX: {:#08x}], [ECX: {:#08x}], [EDX: {:#08x}], [EBP: {:#08x}], [EDI: {:#08x}], [EIP: {:#08x}].\n",
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
			exceptionCodeMessage.Trim().Trim(kxf::StringActionFlag::FromEnd);

			return exceptionCodeMessage;
		};
		result += kxf::Format("ExceptionRecord:\n\tExceptionCode: [NtStatus: ({:#08x}) '{}']\n\tExceptionFlags: {:#08x}\n\tExceptionAddress: {:{}#0x}\n\tExceptionRecord: {:{}#0x}",
							  exception->ExceptionCode,
							  GetExceptionMessage(exception->ExceptionCode),
							  exception->ExceptionFlags,
							  reinterpret_cast<size_t>(exception->ExceptionAddress), sizeof(void*),
							  reinterpret_cast<size_t>(exception->ExceptionRecord), sizeof(void*));

		return result;
	}

	bool PreloadHandler::InitializeFramework()
	{
		Log("Initializing framework");

		// Disable asserts as they're not useful here
		wxLog::DontCreateOnDemand();
		kxf::Log::EnableAsserts(false);

		// Redirect the framework log to our own log file
		if (m_LogStream)
		{
			class LogTarget final: public wxLog
			{
				private:
					PreloadHandler& m_Instance;

				protected:
					void DoLogRecord(wxLogLevel level, const wxString& message, const wxLogRecordInfo& info) override
					{
						m_Instance.Log("<Framework> {}", message);
					}

				public:
					LogTarget(PreloadHandler& instance)
						:m_Instance(instance)
					{
					}
			};
			kxf::Log::SetActiveTarget(std::make_unique<LogTarget>(*this));
		}
		else
		{
			kxf::Log::Enable(false);
		}

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
		if (!wxModule::InitializeModules())
		{
			LogIndent(1, "Initializing framework: failed");
			return false;
		}
		return m_Application->OnInit();
	}
	void PreloadHandler::LogEnvironmentInfo() const
	{
		if (const auto versionInfo = kxf::System::GetVersionInfo())
		{
			const kxf::System::KernelVersion kernel = versionInfo->Kernel;
			const bool is64Bit = kxf::System::Is64Bit();
			Log("<Environment> Operation system: '{}' {}.{}.{}", kxf::System::GetProductName(*versionInfo, is64Bit), kernel.Major, kernel.Minor, kernel.Build);

			if (kernel.ServicePackMajor > 0)
			{
				Log("<Environment> System service pack: '{}' {}.{}", versionInfo->ServicePack, kernel.ServicePackMajor, kernel.ServicePackMinor);
			}
			Log("<Environment> System product type: {}", kxf::ToInt(versionInfo->ProductType));
		}
		else
		{
			Log("<Environment> Couldn't query system version");
		}

		Log("<Environment> System default locale: '{}'", kxf::Locale::GetSystemDefault().GetName());
		Log("<Environment> System preferred locale: '{}'", kxf::Locale::GetSystemPreferred().GetName());
	}
	void PreloadHandler::LogCurrentModuleInfo() const
	{
		auto currentModule = kxf::DynamicLibrary::GetCurrentModule();

		Log("<Current module> Binary: '{}'", currentModule.GetFilePath().GetFullPath());
		Log("<Current module> {} v{} loaded", GetLibraryName(), GetLibraryVersion().ToString());
	}
	kxf::ExecutableVersionResource PreloadHandler::LogHostProcessInfo() const
	{
		Log("<Host process> Binary: '{}'", m_ExecutablePath.GetFullPath());

		kxf::ExecutableVersionResource resourceInfo(m_ExecutablePath);
		if (resourceInfo)
		{
			Log("<Host process> Version: {}", resourceInfo.GetAnyVersion());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log("<Host process> Couldn't load host process binary. [Win32: '{}' ({})]", lastError.GetMessage(), lastError.GetValue());
		}

		return resourceInfo;
	}
	kxf::ExecutableVersionResource PreloadHandler::LogScriptExtenderInfo(const kxf::ExecutableVersionResource& hostResourceInfo) const
	{
		const kxf::FSPath loaderPath = m_FileSystem.ResolvePath(xSE_FOLDER_NAME_W "_Loader.exe");
		Log("<Script Extender> Platform: {}", xSE_NAME_W);
		Log("<Script Extender> Loader: '{}'", loaderPath.GetFullPath());
		if (!m_FileSystem.FileExist(loaderPath))
		{
			LogIndent(1, "File not found: '{}'", loaderPath.GetFullPath());
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
		auto libraryPath = m_FileSystem.ResolvePath(kxf::Format("{}_{}.dll", xSE_FOLDER_NAME_W, versionString));
		Log("<Script Extender> Library: '{}'", libraryPath.GetFullPath());
		if (!m_FileSystem.FileExist(libraryPath))
		{
			LogIndent(1, "File not found: '{}'", libraryPath.GetFullPath());
		}

		m_Application->Bind(kxf::DynamicLibraryEvent::EvtLoaded, [this, libraryPath](kxf::DynamicLibraryEvent& event)
		{
			if (event.GetBaseName().GetName().IsSameAs(libraryPath.GetName(), kxf::StringActionFlag::IgnoreCase))
			{
				Log("<Script Extender> {} library loaded", xSE_NAME_W);
			}
		}, kxf::BindEventFlag::AlwaysSkip);

		kxf::ExecutableVersionResource extenderResourceInfo(loaderPath);
		if (extenderResourceInfo)
		{
			Log("<Script Extender> Version: {}", extenderResourceInfo.GetAnyVersion());
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log("<Script Extender> Couldn't load {} binary, probably not installed: [Win32: '{}' ({})]", xSE_NAME_W, lastError.GetMessage(), lastError.GetValue());
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
					DisableThreadLibraryCalls(handle);

					if (*m_LoadMethod == LoadMethod::OnProcessAttach)
					{
						Log("<OnProcessAttach> LoadPlugins");
						LoadPlugins();
					}
					else if (*m_LoadMethod == LoadMethod::ImportAddressHook)
					{
						Log("<ImportAddressHook> HookImportTable");
						HookImportTable();
					}
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
					const size_t threadCounter = ++m_ThreadAttachCount;
					Log("<OnThreadAttach> Attached thread {}", threadCounter);

					auto& options = GetLoadMethodOptions<LoadMethod::OnThreadAttach>();
					if (options.ThreadNumber == threadCounter)
					{
						DisableThreadLibraryCalls(handle);

						Log("<OnThreadAttach> LoadPlugins");
						LoadPlugins();
					}
				}

				break;
			}
			case DLL_PROCESS_DETACH:
			{
				PreloadHandler::DestroyInstance();
				break;
			}
		};
		return true;
	}
	bool PreloadHandler::DisableThreadLibraryCalls(HMODULE handle)
	{
		m_WatchThreadAttach = false;
		if (::DisableThreadLibraryCalls(handle))
		{
			Log("<DisableThreadLibraryCalls> Success");
			return true;
		}
		else
		{
			auto lastError = kxf::Win32Error::GetLastError();
			Log("<DisableThreadLibraryCalls> Failed: [Win32: '{}' ({})]", lastError.GetMessage(), lastError.GetValue());

			return false;
		}
	}

	bool PreloadHandler::HookImportTable()
	{
		using namespace PluginPreloader;

		if (!m_PluginsLoadAllowed)
		{
			Log("<ImportAddressHook> Plugins preload disabled for this process, skipping hook installation");
			return false;
		}

		if (m_HookDelay.IsPositive())
		{
			Log("<HookDelay> Hooking is delayed by '{}' ms, waiting...", m_HookDelay.GetMilliseconds());
			::Sleep(m_HookDelay.GetMilliseconds());
			Log("<HookDelay> Wait time is out, continuing hooking");
		}

		Log("<ImportAddressHook> Hooking function '{}' from library '{}'", m_ImportAddressHook.FunctionName, m_ImportAddressHook.LibraryName);
		m_ImportAddressHook.SaveUnhooked(Detour::FunctionIAT(&ImportAddressHookHandler::HookFunc, m_ImportAddressHook.LibraryName.nc_str(), m_ImportAddressHook.FunctionName.nc_str()));

		if (m_ImportAddressHook.IsHooked())
		{
			LogIndent(1, "Success [Hooked={:#0{}x}], [Unhooked={:#0{}x}]",
					  reinterpret_cast<size_t>(&ImportAddressHookHandler::HookFunc), sizeof(void*),
					  reinterpret_cast<size_t>(m_ImportAddressHook.GetUnhooked()), sizeof(void*)
			);
			return true;
		}
		else
		{
			LogIndent(1, "Unable to hook import table function");
			return false;
		}
	}
	bool PreloadHandler::LoadPlugins()
	{
		if (!m_PluginsLoadAllowed)
		{
			Log("Plugins preload disabled for this process");
			return false;
		}

		if (!m_PluginsLoaded)
		{
			Log("Loading plugins");
			if (m_LoadDelay.IsPositive())
			{
				Log("<LoadDelay> Loading plugins is delayed by '{}' ms, waiting...", m_LoadDelay.GetMilliseconds());
				::Sleep(m_LoadDelay.GetMilliseconds());
				Log("<LoadDelay> Wait time is out, continuing loading");
			}

			DoLoadPlugins();
			m_PluginsLoaded = true;

			return true;
		}
		return false;
	}

	PreloadHandler::PreloadHandler()
		:m_FileSystem(kxf::NativeFileSystem::GetExecutingModuleRootDirectory())
	{
		m_Application = std::make_shared<Application>(*this);

		// Initialize plugins directory
		m_PluginsDirectory = m_FileSystem.GetLookupDirectory() / "Data" / xSE_FOLDER_NAME_W / "Plugins";
		m_ExecutablePath = kxf::DynamicLibrary::GetExecutingModule().GetFilePath();

		// Open log
		m_LogStream = m_FileSystem.OpenToWrite(g_LogFileName);
		Log("Log opened");

		// Init framework
		if (!m_Application->OnCreate() || !InitializeFramework())
		{
			Log("Error occurred during the initialization process");
			return;
		}
		LogCurrentModuleInfo();
		auto hostResourceInfo = LogHostProcessInfo();
		auto extenderResourceInfo = LogScriptExtenderInfo(hostResourceInfo);
		LogEnvironmentInfo();

		// Load config
		Log("Loading configuration from '{}'", m_FileSystem.ResolvePath(g_ConfigFileName).GetFullPath());
		if (auto readStream = m_FileSystem.OpenToRead(g_ConfigFileName); readStream && m_Config.Load(*readStream))
		{
			LogIndent(1, "Configuration file successfully loaded");
		}
		else
		{
			if (readStream)
			{
				LogIndent(1, "Couldn't load configuration. The file is found, but can not be loaded, default configuration will be used");
			}
			else
			{
				auto lastError = kxf::Win32Error::GetLastError();
				LogIndent(1, "Couldn't load configuration file: [Win32: '{}' ({})], default configuration will be used", lastError.GetMessage(), lastError.GetValue());
			}
			readStream = nullptr;

			// Restore the default config on disk and load it
			Log("Restoring default configuration");

			auto defaultXML = kxf::DynamicLibrary::GetCurrentModule().GetResource("XML", kxf::ToString(IDR_XML_DEFAULT_CONFIGURATION));
			if (m_Config.Load(std::string_view(reinterpret_cast<const char*>(defaultXML.data()), defaultXML.size())))
			{
				LogIndent(1, "Default configuration successfully loaded");

				auto writeStream = m_FileSystem.OpenToWrite(g_ConfigFileName);
				if (writeStream && writeStream->WriteAll(defaultXML.data(), defaultXML.size_bytes()))
				{
					LogIndent(1, "Default configuration successfully saved to disk");
				}
				else
				{
					auto lastError = kxf::Win32Error::GetLastError();
					LogIndent(1, "Couldn't save default configuration to disk: [Win32: '{}' ({})]", lastError.GetMessage(), lastError.GetValue());
				}
			}
			else
			{
				LogIndent(1, "Couldn't load default configuration");
			}
		}

		m_OriginalLibraryPath = [&]()
		{
			kxf::String path = m_Config.QueryElement("xSE/PluginPreloader/OriginalLibrary").GetValue();
			if (path.IsEmpty())
			{
				Log("Original library path is not set, using default '{}'", GetOriginalLibraryDefaultPath().GetFullPath());
			}
			else
			{
				Log("Original library path is set to '{}'", path);
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
			kxf::XMLNode rootNode = m_Config.QueryElement("xSE/PluginPreloader/LoadMethod");
			kxf::String methodName = rootNode.GetAttribute("Name");

			if (auto method = LoadMethodFromString(methodName))
			{
				Log("Load method is set to '{}', loading method parameters", methodName);

				kxf::XMLNode methodNode = rootNode.GetFirstChildElement(methodName);
				switch (*method)
				{
					case LoadMethod::OnProcessAttach:
					{
						LogIndent(1, "No parameters");
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

						LogIndent(1, "ThreadNumber = {}", m_OnThreadAttach.ThreadNumber);
						return *method;
					}
					case LoadMethod::ImportAddressHook:
					{
						m_ImportAddressHook.LibraryName = methodNode.GetFirstChildElement("LibraryName").GetValue();
						m_ImportAddressHook.FunctionName = methodNode.GetFirstChildElement("FunctionName").GetValue();

						LogIndent(1, "LibraryName = {}", m_ImportAddressHook.LibraryName);
						LogIndent(1, "FunctionName = {}", m_ImportAddressHook.FunctionName);

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
				Log("Unknown load method: '{}'", methodName);
			}
			return {};
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
			Log("<{}> This process is not allowed to preload plugins", m_ExecutablePath.GetName());
		}

		// Load the original library
		LoadOriginalLibrary();
		if (m_OriginalLibrary)
		{
			LoadOriginalLibraryFunctions();
		}
		else
		{
			Log("Can't load original library, terminating");
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

		// Close the log
		Log(L"Log closed");
		kxf::Log::SetActiveTarget(nullptr);
		kxf::Log::Enable(false);
		m_LogStream = nullptr;
	}
}
