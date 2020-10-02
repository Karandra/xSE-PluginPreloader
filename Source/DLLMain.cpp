#include "pch.hpp"
#include "xSEPluginPreloader.h"

namespace
{
	std::atomic<size_t> g_ThreadAttachCount = 0;
	bool g_WatchThreadAttach = true;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD event, LPVOID lpReserved)
{
	using namespace xSE;

	switch (event)
	{
		case DLL_PROCESS_ATTACH:
		{
			PreloadHandler& handler = PreloadHandler::CreateInstance();
			if (handler.IsNull())
			{
				handler.Log(wxS("Invalid state of PreloadHandler reported. Terminating process."));
				return FALSE;
			}

			LoadMethod method = handler.GetLoadMethod();
			if (method == LoadMethod::OnProcessAttach || method == LoadMethod::ImportAddressHook)
			{
				handler.DisableThreadLibraryCalls(handle);

				if (method == LoadMethod::OnProcessAttach)
				{
					handler.Log(wxS("<OnProcessAttach> LoadPlugins"));
					handler.LoadPlugins();
				}
				else if (method == LoadMethod::ImportAddressHook)
				{
					handler.Log(wxS("<ImportAddressHook> HookImportTable"));
					handler.HookImportTable();
				}
			}
			return TRUE;
		}
		case DLL_THREAD_ATTACH:
		{
			if (!g_WatchThreadAttach)
			{
				break;
			}

			PreloadHandler& handler = PreloadHandler::GetInstance();
			if (handler.GetLoadMethod() == LoadMethod::OnThreadAttach)
			{
				const size_t threadCounter = ++g_ThreadAttachCount;
				handler.Log(wxS("<OnThreadAttach> Attached thread #%1"), threadCounter);

				decltype(auto) options = handler.GetLoadMethodOptions<LoadMethod::OnThreadAttach>();
				if (options.ThreadNumber == threadCounter)
				{
					handler.DisableThreadLibraryCalls(handle);
					g_WatchThreadAttach = false;

					handler.Log(wxS("<OnThreadAttach> LoadPlugins"));
					handler.LoadPlugins();
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
	return TRUE;
}
