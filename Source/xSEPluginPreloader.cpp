#include "stdafx.h"
#include "xSEPluginPreloader.h"
#include "ScriptExtenderDefinesBase.h"
#include "KxFileFinder.h"

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

//////////////////////////////////////////////////////////////////////////
xSEPP* xSEPP::ms_Instnace = NULL;
xSEPP& xSEPP::CreateInstnace()
{
	if (!HasInstance())
	{
		ms_Instnace = new xSEPP();
	}
	return *ms_Instnace;
}
void xSEPP::DestroyInstnace()
{
	delete ms_Instnace;
	ms_Instnace = NULL;
}

const wchar_t* xSEPP::GetConfigOption(const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue) const
{
	return m_Config.GetValue(section, key, defaultValue);
}
int xSEPP::GetConfigOptionInt(const wchar_t* section, const wchar_t* key, int defaultValue) const
{
	const wchar_t* value = GetConfigOption(section, key, NULL);
	if (value)
	{
		int valueInt = defaultValue;
		swscanf(value, L"%d", &valueInt);
		return valueInt;
	}
	return defaultValue;
}

xSEPP::LoadStatus xSEPP::LoadPlugin(const wchar_t* path)
{
	Log(L"Trying to load '%s'", path);
	LoadStatus status = LoadStatus::LoadFailed;

	__try
	{
		HMODULE handle = ::LoadLibraryW(path);
		if (handle)
		{
			status = LoadStatus::Loaded;
			m_LoadedLibraries.push_back(handle);

			InitPluginFunc initFunc = reinterpret_cast<InitPluginFunc>(::GetProcAddress(handle, "Initialize"));
			if (initFunc)
			{
				status = LoadStatus::InitializationFailed;
				initFunc();
				status = LoadStatus::LoadedInitialized;
			}
		}
		else
		{
			status = LoadStatus::LoadFailed;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return status;
}
void xSEPP::LoadPlugins()
{
	Log(L"Searching '%s' folder for plugins", m_PluginsFolder);

	KxFileFinder finder(m_PluginsFolder, L"*_preload.txt");
	KxFileFinderItem item = finder.FindNext();
	while (finder.IsOK())
	{
		if (item.IsNormalItem() && item.IsFile())
		{
			KxDynamicString pathDLL = item.GetFullPath().before_last(L'_');
			pathDLL += L".dll";

			LoadStatus status = LoadPlugin(pathDLL);
			LogLoadStatus(pathDLL, status);
		}
		item = finder.FindNext();
	}

	Log(L"Loading finished, %zu plugins loaded", m_LoadedLibraries.size());
}
void xSEPP::UnloadPlugins()
{
	Log(L"Unloading plugins");

	for (HMODULE handle : m_LoadedLibraries)
	{
		::FreeLibrary(handle);
	}
	m_LoadedLibraries.clear();
}
void xSEPP::LogLoadStatus(const wchar_t* path, LoadStatus status) const
{
	switch (status)
	{
		case LoadStatus::Loaded:
		{
			Log(L"Plugin '%s' loaded successfully", path);
			break;
		}
		case LoadStatus::LoadedInitialized:
		{
			Log(L"Plugin '%s' loaded successfully, Initialize() executed", path);
			break;
		}
		case LoadStatus::LoadFailed:
		{
			Log(L"Plugin '%s' failed to load", path);
			break;
		}
		case LoadStatus::InitializationFailed:
		{
			Log(L"Plugin '%s' failed to load. Exception was thrown during Initialize() execution", path);
			break;
		}
	};
}

void xSEPP::DetourInitFunctions()
{
	Log(L"Overriding '_initterm_e' function for delayed load");
	void* func = NULL;

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

	if (func == NULL)
	{
		UnloadOriginalLibrary();
		m_OriginalLibrary = NULL;

		Log(L"Can't override function, terminating");
	}
}

void xSEPP::LoadOriginalLibrary()
{
	KxDynamicString path = GetOriginalLibraryPath();
	Log(L"Loading original library '%s'", path.data());

	ClearOriginalFunctionArray();
	m_OriginalLibrary = ::LoadLibraryW(path);
}
void xSEPP::UnloadOriginalLibrary()
{
	Log(L"Unloading original library");

	FreeLibrary(m_OriginalLibrary);
	ClearOriginalFunctionArray();
}
void xSEPP::ClearOriginalFunctionArray()
{
	for (size_t i = 0; i < GetFunctionArraySize(); i++)
	{
		GetFunctions()[i] = NULL;
	}
}

xSEPP::xSEPP()
	:m_PluginsFolder(L"Data\\" xSE_FOLDER_NAME_W L"\\Plugins")
{
	// Load config
	m_Config.LoadFile(L"xSE PluginPreloader.ini");

	if (const wchar_t* originalLibrary = GetConfigOption(L"General", L"OriginalLibrary"))
	{
		m_OriginalLibraryPath = originalLibrary;
	}

	m_LoadMethod = (LoadMethod)GetConfigOptionInt(L"General", L"LoadMethod", (int)m_LoadMethod);

	// Open log
	_wfopen_s(&m_Log, L"xSE PluginPreloader.log", L"wb+");
	Log(L"Log opened");

	// Load
	LoadOriginalLibrary();
	if (m_OriginalLibrary)
	{
		LoadOriginalLibraryFunctions();
		
		#if 0
		DetourInitFunctions();
		#endif
	}
	else
	{
		Log(L"Can't load original library, terminating");
	}
}
xSEPP::~xSEPP()
{
	if (m_OriginalLibrary)
	{
		UnloadPlugins();
		UnloadOriginalLibrary();
	}

	Log(L"Log closed");
	if (m_Log)
	{
		fclose(m_Log);
	}
}

void xSEPP::RunLoadPlugins()
{
	if (!m_PluginsLoaded)
	{
		Log(L"Loading plugins");
		LoadPlugins();
		m_PluginsLoaded = true;
	}
}
KxDynamicString xSEPP::GetOriginalLibraryPath() const
{
	if (!m_OriginalLibraryPath.empty())
	{
		return m_OriginalLibraryPath;
	}
	else
	{
		KxDynamicString path;

		#if xSE_PLATFORM_F4SE

		path.resize(MAX_PATH);
		path.resize(::GetSystemDirectoryW(path.data(), MAX_PATH));
		path += L"\\X3DAudio1_7.dll";

		#elif xSE_PLATFORM_NVSE

		path.resize(MAX_PATH);
		path.resize(::GetSystemDirectoryW(path.data(), MAX_PATH));
		path += L"\\DInput8.dll";

		#endif

		return path;
	}
}
