#pragma once
#include "Framework.hpp"
#include "VectoredExceptionHandler.h"
#include <kxf/IO/IStream.h>
#include <kxf/Serialization/XML.h>
#include <kxf/System/DynamicLibrary.h>

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

	class ImportAddressHook final
	{
		private:
			void(__cdecl* m_UnhookedFunction)(void*, void*) = nullptr;

		public:
			kxf::String LibraryName;
			kxf::String FunctionName;

		public:
			template<class... Args>
			decltype(auto) CallUnhooked(Args&&... arg) noexcept
			{
				return std::invoke(m_UnhookedFunction, std::forward<Args>(arg)...);
			}

			void* GetUnhooked() const noexcept
			{
				return m_UnhookedFunction;
			}
			void SaveUnhooked(decltype(m_UnhookedFunction) func) noexcept
			{
				m_UnhookedFunction = func;
			}
			bool IsHooked() const noexcept
			{
				return m_UnhookedFunction != nullptr;
			}
	};
}

namespace xSE
{
	class PreloadHandler final
	{
		public:
			static kxf::String GetLibraryName();
			static kxf::Version GetLibraryVersion();

			static PreloadHandler& CreateInstance();
			static void DestroyInstance();
			static PreloadHandler& GetInstance();
			static bool HasInstance();

			static size_t GetFunctionsCount() noexcept;
			static void** GetFunctions() noexcept;
			
		private:
			// General
			kxf::DynamicLibrary m_OriginalLibrary;
			std::vector<kxf::DynamicLibrary> m_LoadedLibraries;
			VectoredExceptionHandler m_VectoredExceptionHandler;

			kxf::FSPath m_ExecutablePath;
			kxf::FSPath m_PluginsDirectory;
			bool m_PluginsLoaded = false;
			bool m_PluginsLoadAllowed = false;

			// Config
			kxf::XMLDocument m_Config;
			kxf::FSPath m_OriginalLibraryPath;
			kxf::TimeSpan m_LoadDelay;
			bool m_KeepExceptionHandler = false;
			std::vector<kxf::String> m_AllowedProcessNames;

			std::optional<LoadMethod> m_LoadMethod;
			PluginPreloader::OnProcessAttach m_OnProcessAttach;
			PluginPreloader::OnThreadAttach m_OnThreadAttach;
			PluginPreloader::ImportAddressHook m_ImportAddressHook;

			// Log
			std::unique_ptr<kxf::IOutputStream> m_LogStream;

		private:
			kxf::FSPath GetOriginalLibraryPath() const;
			kxf::FSPath GetOriginalLibraryDefaultPath() const;

			void DoLoadPlugins();
			void DoUnloadPlugins();
			PluginStatus DoLoadSinglePlugin(const kxf::FSPath& path);

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

		public:
			PreloadHandler();
			~PreloadHandler();

		public:
			bool IsNull() const
			{
				return m_OriginalLibrary.IsNull() || !m_LoadMethod.has_value();
			}
			bool HookImportTable();
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
			bool LoadPlugins();
			
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

			bool DisableThreadLibraryCalls(HMODULE handle);

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
				return DoLog(kxf::String::Format(format, std::forward<Args>(args)...), true);
			}

			template<class ...Args>
			size_t LogIndent(size_t indent, const kxf::String& format, Args&&... args) const
			{
				return DoLog(kxf::String::Format(format, std::forward<Args>(args)...), true, indent);
			}

			template<class ...Args>
			size_t LogNoTime(const kxf::String& format, Args&&... args) const
			{
				return DoLog(kxf::String::Format(format, std::forward<Args>(args)...), false);
			}
	};
}
