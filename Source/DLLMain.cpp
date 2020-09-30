#include "pch.hpp"
#include "xSEPluginPreloader.h"

namespace
{
	size_t g_ThreadAttachCount = 0;
}

BOOL APIENTRY DllMain(HMODULE handle, DWORD event, LPVOID lpReserved)
{
	using namespace xSE;

	switch (event)
	{
		case DLL_PROCESS_ATTACH:
		{
			PreloadHandler& handler = PreloadHandler::CreateInstnace();
			if (!handler.IsNull() && handler.ShouldUseDirectLoad())
			{
				handler.LoadPlugins();
			}

			if (handler.IsNull())
			{
				handler.Log(wxS("Invalid state of PreloadHandler reported. Terminating process."));
				return FALSE;
			}
			return TRUE;
		}
		case DLL_THREAD_ATTACH:
		{
			g_ThreadAttachCount++;
			if (g_ThreadAttachCount == 2 && PreloadHandler::GetInstance().ShouldUseDelayedLoad())
			{
				::DisableThreadLibraryCalls(handle);
				PreloadHandler::GetInstance().LoadPlugins();
			}
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			PreloadHandler::DestroyInstnace();
			break;
		}
	};
	return true;
}
