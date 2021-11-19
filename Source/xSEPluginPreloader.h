#pragma once
#include "Framework.hpp"
#include "VectoredExceptionHandler.h"
#include "Utility.h"
#include <kxf/IO/IStream.h>
#include <kxf/System/NtStatus.h>
#include <kxf/System/DynamicLibrary.h>
#include <kxf/FileSystem/NativeFileSystem.h>
#include <kxf/Serialization/XML.h>

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID);

namespace xSE
{
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
		ImportAddressHook,
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
			TSignature* m_UnhookedFunction = nullptr;

		public:
			kxf::String LibraryName;
			kxf::String FunctionName;

		public:
			bool IsNull() const
			{
				return LibraryName.IsEmpty() || FunctionName.IsEmpty();
			}

			template<class... Args, class R = std::invoke_result_t<TSignature, Args...>>
			R CallUnhooked(kxf::NtStatus& status, Args&&... arg)
			{
				if constexpr(std::is_void_v<R>)
				{
					status = Utility::SEHTryExcept([&]()
					{
						std::invoke(m_UnhookedFunction, std::forward<Args>(arg)...);
					});
				}
				else
				{
					R result;
					status = Utility::SEHTryExcept([&]()
					{
						result = std::invoke(m_UnhookedFunction, std::forward<Args>(arg)...);
					});

					return result;
				}
			}

			TSignature* GetUnhooked() const noexcept
			{
				return m_UnhookedFunction;
			}
			void SaveUnhooked(TSignature* func) noexcept
			{
				m_UnhookedFunction = func;
			}
			bool IsHooked() const noexcept
			{
				return m_UnhookedFunction != nullptr;
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

			static PreloadHandler& GetInstance();
			static bool HasInstance();

			static size_t GetFunctionsCount() noexcept;
			static void** GetFunctions() noexcept;

		private:
			// General
			kxf::NativeFileSystem m_FileSystem;
			kxf::DynamicLibrary m_OriginalLibrary;
			std::vector<kxf::DynamicLibrary> m_LoadedLibraries;
			VectoredExceptionHandler m_VectoredExceptionHandler;

			kxf::FSPath m_ExecutablePath;
			kxf::FSPath m_PluginsDirectory;
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

			// Log
			std::unique_ptr<kxf::IOutputStream> m_LogStream;

		private:
			kxf::FSPath GetOriginalLibraryPath() const;
			kxf::FSPath GetOriginalLibraryDefaultPath() const;

			void DoLoadPlugins();
			void DoUnloadPlugins();
			PluginStatus DoLoadSinglePlugin(const kxf::FSPath& path);
			void OnPluginLoadFailed(const kxf::FSPath& path, size_t logIndentOffset = 0);

			bool CheckAllowedProcesses() const;
			void LoadOriginalLibrary();
			void LoadOriginalLibraryFunctions();
			void UnloadOriginalLibrary();
			void ClearOriginalFunctions();

			size_t DoLog(kxf::String logString, bool addTimestamp, size_t indent = 0) const;

			bool InstallVectoredExceptionHandler();
			void RemoveVectoredExceptionHandler();
			uint32_t OnVectoredContinue(const _EXCEPTION_POINTERS& exceptionInfo);
			uint32_t OnVectoredException(const _EXCEPTION_POINTERS& exceptionInfo);
			kxf::String DumpExceptionInformation(const _EXCEPTION_POINTERS& exceptionInfo) const;

			bool InitializeFramework();
			void LogEnvironmentInfo() const;
			void LogCurrentModuleInfo() const;
			void LogHostProcessInfo() const;
			void LogScriptExtenderInfo() const;

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
				return m_OriginalLibrary.IsNull() || !m_LoadMethod.has_value();
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
					static_assert(false);
				}
			}

		public:
			size_t Log(const kxf::String& logString) const
			{
				return DoLog(logString, true);
			}
			size_t LogIndent(size_t indent, const kxf::String& logString) const
			{
				return DoLog(logString, true, indent);
			}
			size_t LogNoTime(const kxf::String& logString) const
			{
				return DoLog(logString, false);
			}

			template<class ...Args>
			size_t Log(const kxf::String& format, Args&&... args) const
			{
				return DoLog(kxf::Format(format, std::forward<Args>(args)...), true);
			}

			template<class ...Args>
			size_t LogIndent(size_t indent, const kxf::String& format, Args&&... args) const
			{
				return DoLog(kxf::Format(format, std::forward<Args>(args)...), true, indent);
			}

			template<class ...Args>
			size_t LogNoTime(const kxf::String& format, Args&&... args) const
			{
				return DoLog(kxf::Format(format, std::forward<Args>(args)...), false);
			}
	};
}
