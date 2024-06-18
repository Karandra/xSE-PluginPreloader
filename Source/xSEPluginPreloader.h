#pragma once
#include "Common.h"
#include "VectoredExceptionHandler.h"
#include "Utility.h"
#include <kxf/IO/IStream.h>
#include <kxf/System/NtStatus.h>
#include <kxf/System/DynamicLibrary.h>
#include <kxf/Threading/ReadWriteLock.h>
#include <kxf/FileSystem/NativeFileSystem.h>
#include <kxf/Serialization/XML.h>

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID);

namespace kxf
{
	class ExecutableVersionResource;
}

namespace xSE
{
	class Application;

	enum class PluginStatus: uint32_t
	{
		Loaded,
		Initialized,

		FailedLoad,
		FailedInitialize
	};

	enum class LoadMethod
	{
		OnProcessAttach,
		OnThreadAttach,
		ImportAddressHook
	};
	enum class InitializationMethod
	{
		None,
		Standard,
		xSEPluginPreload
	};
}

namespace xSE::PluginPreloader
{
	class OnProcessAttach final
	{
	};

	class OnThreadAttach final
	{
		public:
			size_t ThreadNumber = 0;
	};

	template<class TSignature>
	class ImportAddressHook final
	{
		private:
			TSignature* m_OriginalFunction = nullptr;

		public:
			kxf::String LibraryName;
			kxf::String FunctionName;

		public:
			bool IsNull() const
			{
				return LibraryName.IsEmpty() || FunctionName.IsEmpty();
			}

			template<class... Args, class R = std::invoke_result_t<TSignature, Args...>>
			R CallOriginal(kxf::NtStatus& status, Args&&... arg)
			{
				KX_SCOPEDLOG_FUNC;
				KX_SCOPEDLOG.Info()
					KX_SCOPEDLOG_VALUE_AS(m_OriginalFunction, reinterpret_cast<void*>(m_OriginalFunction))
					KX_SCOPEDLOG_VALUE(LibraryName)
					KX_SCOPEDLOG_VALUE(FunctionName);

				if constexpr(std::is_void_v<R>)
				{
					status = Utility::SEHTryExcept([&]()
					{
						std::invoke(m_OriginalFunction, std::forward<Args>(arg)...);
					});
					KX_SCOPEDLOG.SetSuccess(status);
				}
				else
				{
					R result;
					status = Utility::SEHTryExcept([&]()
					{
						result = std::invoke(m_OriginalFunction, std::forward<Args>(arg)...);
					});

					KX_SCOPEDLOG.LogReturn(result, status.IsSuccess());
					return result;
				}
			}

			TSignature* GetOriginal() const noexcept
			{
				return m_OriginalFunction;
			}
			void SaveOriginal(TSignature* func) noexcept
			{
				m_OriginalFunction = func;
			}
			bool IsHooked() const noexcept
			{
				return m_OriginalFunction != nullptr;
			}
	};
	class ImportAddressHookHandler;
}

namespace xSE
{
	class PreloadHandler final
	{
		friend BOOL APIENTRY ::DllMain(HMODULE, DWORD, LPVOID);
		friend class PluginPreloader::ImportAddressHookHandler;

		private:
			static PreloadHandler& CreateInstance();
			static void DestroyInstance();

		public:
			static kxf::String GetLibraryName();
			static kxf::Version GetLibraryVersion();
			static PreloadHandler* GetInstance() noexcept;

			static void** GetFunctions() noexcept;
			static size_t GetFunctionsCount() noexcept;
			static size_t GetFunctionsEffectiveCount() noexcept;

		private:
			// General
			std::shared_ptr<Application> m_Application;
			kxf::NativeFileSystem m_InstallFS;
			kxf::NativeFileSystem m_ConfigFS;
			kxf::DynamicLibrary m_OriginalLibrary;
			std::vector<kxf::DynamicLibrary> m_LoadedLibraries;
			VectoredExceptionHandler m_VectoredExceptionHandler;

			kxf::FSPath m_ExecutablePath;
			bool m_PluginsLoaded = false;
			bool m_PluginsLoadAllowed = false;
			std::atomic<size_t> m_ThreadAttachCount = 0;
			bool m_WatchThreadAttach = false;

			// Config
			kxf::XMLDocument m_Config;
			kxf::FSPath m_OriginalLibraryPath;
			kxf::TimeSpan m_HookDelay;
			kxf::TimeSpan m_LoadDelay;
			bool m_InstallExceptionHandler = true;
			bool m_KeepExceptionHandler = false;
			std::vector<kxf::String> m_AllowedProcessNames;

			std::optional<LoadMethod> m_LoadMethod;
			PluginPreloader::OnProcessAttach m_OnProcessAttach;
			PluginPreloader::OnThreadAttach m_OnThreadAttach;
			#if xSE_PLATFORM_SKSE64 || xSE_PLATFORM_F4SE
			PluginPreloader::ImportAddressHook<void*(__cdecl)(void*, void*)> m_ImportAddressHook;
			#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE
			PluginPreloader::ImportAddressHook<char*(__stdcall)()> m_ImportAddressHook;
			#endif

			std::optional<InitializationMethod> m_InitializationMethod;

		private:
			kxf::FSPath GetOriginalLibraryPath() const;
			kxf::FSPath GetOriginalLibraryDefaultPath() const;

			void DoLoadPlugins();
			void DoUnloadPlugins();
			PluginStatus DoLoadSinglePlugin(const kxf::FSPath& path);
			void OnPluginLoadFailed(const kxf::FSPath& path);

			bool CheckAllowedProcesses() const;
			void LoadOriginalLibrary();
			void LoadOriginalLibraryFunctions();
			void UnloadOriginalLibrary();
			void ClearOriginalFunctions();

			bool InstallVectoredExceptionHandler();
			void RemoveVectoredExceptionHandler();
			uint32_t OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo);
			uint32_t OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo);
			kxf::String DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const;

			bool InitializeFramework();
			void LogEnvironmentInfo() const;
			void LogCurrentModuleInfo() const;
			kxf::ExecutableVersionResource LogHostProcessInfo() const;
			kxf::ExecutableVersionResource LogScriptExtenderInfo(const kxf::ExecutableVersionResource& hostResourceInfo) const;

			bool OnDLLMain(HMODULE handle, uint32_t event);
			bool DisableThreadLibraryCalls(HMODULE handle);

			bool HookImportTable();
			bool LoadPlugins();

		public:
			PreloadHandler();
			~PreloadHandler();

		public:
			bool IsNull() const
			{
				return m_OriginalLibrary.IsNull() || !m_LoadMethod.has_value() || !m_InitializationMethod.has_value();
			}
			LoadMethod GetLoadMethod() const
			{
				return *m_LoadMethod;
			}

			bool IsPluginsLoaded() const noexcept
			{
				return m_PluginsLoaded;
			}
			bool IsPluginsLoadAllowed() const noexcept
			{
				return m_PluginsLoadAllowed;
			}

			template<LoadMethod method>
			const auto& GetLoadMethodOptions() const noexcept
			{
				if constexpr(method == LoadMethod::OnProcessAttach)
				{
					return m_OnProcessAttach;
				}
				else if constexpr(method == LoadMethod::OnThreadAttach)
				{
					return m_OnThreadAttach;
				}
				else if constexpr(method == LoadMethod::ImportAddressHook)
				{
					return m_ImportAddressHook;
				}
				else
				{
					static_assert(sizeof(LoadMethod*) == nullptr);
				}
			}
	};
}
