#pragma once
#include "Framework.hpp"
#include <kxf/Serialization/XML.h>
#include <kxf/System/DynamicLibrary.h>

namespace xSE
{
	enum class PluginStatus: uint32_t
	{
		Loaded,
		Initialized,

		FailedLoad,
		FailedInitialize,
	};

	enum class LoadMethod
	{
		Direct,
		Delayed,
	};
}

namespace xSE
{
	class PreloadHandler final
	{
		public:
			using InitTermFunc = void(__cdecl*)(void*, void*);
			using tmainCRTStartupFunc = int64_t(__cdecl*)(void);

		public:
			static kxf::String GetLibraryName();
			static kxf::String GetLibraryVersion();

			static PreloadHandler& CreateInstnace();
			static void DestroyInstnace();
			static PreloadHandler& GetInstance();
			static bool HasInstance();

			static size_t GetFunctionsCount() noexcept;
			static void** GetFunctions() noexcept;
		
		private:
			std::vector<kxf::DynamicLibrary> m_LoadedLibraries;
			kxf::DynamicLibrary m_OriginalLibrary;

			kxf::FSPath m_ExecutablePath;
			kxf::FSPath m_PluginsDirectory;
			bool m_PluginsLoadAllowed = false;
			bool m_PluginsLoaded = false;

			kxf::XMLDocument m_Config;
			kxf::FSPath m_OriginalLibraryPath;
			std::vector<kxf::String> m_AllowedProcessNames;
			LoadMethod m_LoadMethod = LoadMethod::Delayed;

			FILE* m_Log = nullptr;

			#ifdef USE_NUKEM_DETOURS
			InitTermFunc m_initterm_e = nullptr;
			tmainCRTStartupFunc m_start = nullptr;
			#endif

		private:
			kxf::FSPath GetOriginalLibraryPath() const;
			kxf::FSPath GetOriginalLibraryDefaultPath() const;

			void DoLoadPlugins();
			void DoUnloadPlugins();
			PluginStatus DoLoadPlugin(const kxf::FSPath& path);

			bool CheckAllowedProcesses() const;

			void LoadOriginalLibrary();
			void LoadOriginalLibraryFunctions();
			void UnloadOriginalLibrary();
			void ClearOriginalFunctions();

			#ifdef USE_NUKEM_DETOURS
			void DetourInitFunctions();
			static void DelayedLoad_InitTrem(void* p1, void* p2)
			{
				GetInstance().LoadPlugins();
				GetInstance().Call_InitTerm(p1, p2);
			}
			static int64_t DelayedLoad_start()
			{
				GetInstance().LoadPlugins();
				return GetInstance().Call_start();
			}
			void Call_InitTerm(void* p1, void* p2) const
			{
				(*m_initterm_e)(p1, p2);
			}
			int64_t Call_start() const
			{
				return (*m_start)();
			}

			template<class T>
			T DetourFunctionIAT(T func, const char* nameDLL, const char* nameFunc) const
			{
				uint8_t* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
				return reinterpret_cast<T>(NukemDetours::DetourIAT(base, reinterpret_cast<uint8_t*>(func), nameDLL, nameFunc));
			}
		
			template<class T>
			T DetourFunctionBase(HMODULE base, T func, uintptr_t offset) const
			{
				return reinterpret_cast<T>(NukemDetours::DetourFunction(reinterpret_cast<uint8_t*>(base) + offset, reinterpret_cast<uint8_t*>(func)));
			}

			template<class T>
			T DetourFunctionThis(T func, uintptr_t offset) const
			{
				return DetourFunctionBase(GetModuleHandleW(nullptr), func, offset);
			}

			template<class T>
			T DetourFunctionCRT(T func, uintptr_t offset) const
			{
				return DetourFunctionBase(GetModuleHandleW(L"MSVCR110.dll"), func, offset);
			}
			#endif

		public:
			PreloadHandler();
			~PreloadHandler();

		public:
			bool IsNull() const
			{
				return m_OriginalLibrary.IsNull();
			}
			void LoadPlugins();

			bool ShouldUseDirectLoad() const
			{
				return m_LoadMethod == LoadMethod::Direct;
			}
			bool ShouldUseDelayedLoad() const
			{
				return m_LoadMethod == LoadMethod::Delayed;
			}

			template<class ...Args>
			void Log(const kxf::String& format, Args&&... args) const
			{
				if (m_Log)
				{
					kxf::String string = kxf::String::Format(format, std::forward<Args>(args)...);
					fputws(string.wx_str(), m_Log);
					fputws(L"\r\n", m_Log);
					fflush(m_Log);
				}
			}
	};
}
