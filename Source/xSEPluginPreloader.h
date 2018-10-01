#pragma once
#include "stdafx.h"
#include "KxDynamicString.h"
#include "SimpleINI.h"

class xSEPP
{
	public:
		enum class LoadStatus
		{
			Loaded,
			LoadedInitialized,

			LoadFailed,
			InitializationFailed,
		};
		enum class LoadMethod
		{
			Direct,
			Delayed,
		};

		using InitPluginFunc = void(__cdecl*)(void);
		
		using InitTermFunc = void(__cdecl*)(void*, void*);
		using tmainCRTStartupFunc = int64_t(__cdecl*)(void);

	private:
		static xSEPP* ms_Instnace;

	public:
		const wchar_t* GetLibraryName();
		const wchar_t* GetLibraryVersion();

		static xSEPP& CreateInstnace();
		static void DestroyInstnace();
		static bool HasInstance()
		{
			return ms_Instnace != NULL;
		}
		static xSEPP& GetInstance()
		{
			return *ms_Instnace;
		}

		static constexpr size_t GetFunctionArraySize()
		{
			return 512;
		}
		static void** GetFunctions();
		
	private:
		std::vector<HMODULE> m_LoadedLibraries;
		HMODULE m_OriginalLibrary = NULL;
		
		bool m_PluginsLoaded = false;
		InitTermFunc m_initterm_e = NULL;
		tmainCRTStartupFunc m_start = NULL;

		CSimpleIniW m_Config;
		KxDynamicString m_OriginalLibraryPath;
		LoadMethod m_LoadMethod = LoadMethod::Delayed;

		const wchar_t* m_PluginsFolder = NULL;
		FILE* m_Log = NULL;

	private:
		const wchar_t* GetConfigOption(const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue = NULL) const;
		int GetConfigOptionInt(const wchar_t* section, const wchar_t* key, int defaultValue = 0) const;
		bool GetConfigOptionBool(const wchar_t* section, const wchar_t* key, bool defaultValue = false)
		{
			return GetConfigOptionInt(section, key, defaultValue);
		}

		LoadStatus LoadPlugin(const wchar_t* path);
		void LoadPlugins();
		void UnloadPlugins();
		void LogLoadStatus(const wchar_t* path, LoadStatus status) const;
		
		void DetourInitFunctions();
		static void DelayedLoad_InitTrem(void* p1, void* p2)
		{
			GetInstance().RunLoadPlugins();
			GetInstance().Call_InitTerm(p1, p2);
		}
		static int64_t DelayedLoad_start()
		{
			GetInstance().RunLoadPlugins();
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
		
		void LoadOriginalLibrary();
		void LoadOriginalLibraryFunctions();
		void UnloadOriginalLibrary();
		void ClearOriginalFunctionArray();

		template<class T> T DetourFunctionIAT(T func, const char* nameDLL, const char* nameFunc) const
		{
			uint8_t* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(NULL));
			return reinterpret_cast<T>(NukemDetours::DetourIAT(base, reinterpret_cast<uint8_t*>(func), nameDLL, nameFunc));
		}
		
		template<class T> T DetourFunctionBase(HMODULE base, T func, uintptr_t offset) const
		{
			return reinterpret_cast<T>(NukemDetours::DetourFunction(reinterpret_cast<uint8_t*>(base) + offset, reinterpret_cast<uint8_t*>(func)));
		}
		template<class T> T DetourFunctionThis(T func, uintptr_t offset) const
		{
			return DetourFunctionBase(GetModuleHandleW(NULL), func, offset);
		}
		template<class T> T DetourFunctionCRT(T func, uintptr_t offset) const
		{
			return DetourFunctionBase(GetModuleHandleW(L"MSVCR110.dll"), func, offset);
		}

	public:
		xSEPP();
		~xSEPP();

	public:
		bool IsOK() const
		{
			return m_OriginalLibrary != NULL;
		}
		void RunLoadPlugins();

		bool ShouldUseDirectLoad() const
		{
			return m_LoadMethod == LoadMethod::Direct;
		}
		bool ShouldUseDelayedLoad() const
		{
			return m_LoadMethod == LoadMethod::Delayed;
		}

		template<class ...Args> void Log(const wchar_t* format, Args... args) const
		{
			if (m_Log)
			{
				KxDynamicString string = KxDynamicString::Format(format, std::forward<Args>(args)...);
				fputws(string.data(), m_Log);
				fputws(L"\r\n", m_Log);
				fflush(m_Log);
			}
		}
		KxDynamicString GetOriginalLibraryPath() const;
};
