#include "stdafx.h"
#include "xSEPluginPreloader.h"

static size_t g_ThreadAttachCount = 0;
BOOL APIENTRY DllMain(HMODULE handle, DWORD event, LPVOID lpReserved)
{
	switch (event)
	{
		case DLL_PROCESS_ATTACH:
		{
			xSEPP& instance = xSEPP::CreateInstnace();
			if (instance.IsOK() && instance.ShouldUseDirectLoad())
			{
				instance.RunLoadPlugins();
			}
			return instance.IsOK();
		}
		case DLL_THREAD_ATTACH:
		{
			g_ThreadAttachCount++;
			if (g_ThreadAttachCount == 2 && xSEPP::GetInstance().ShouldUseDelayedLoad())
			{
				xSEPP::GetInstance().RunLoadPlugins();
			}
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			xSEPP::DestroyInstnace();
			break;
		}
	};
	return true;
}
