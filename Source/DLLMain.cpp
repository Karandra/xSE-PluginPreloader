#include "pch.hpp"
#include "xSEPluginPreloader.h"

BOOL APIENTRY DllMain(HMODULE handle, DWORD event, LPVOID lpReserved)
{
	if (event == DLL_PROCESS_ATTACH)
	{
		auto& handler = xSE::PreloadHandler::CreateInstance();
		if (handler.IsNull())
		{
			handler.Log("Invalid state of PreloadHandler reported. Terminating process.");
			return FALSE;
		}

		return handler.OnDLLMain(handle, event) ? TRUE : FALSE;
	}
	else if (xSE::PreloadHandler::HasInstance())
	{
		return xSE::PreloadHandler::GetInstance().OnDLLMain(handle, event) ? TRUE : FALSE;
	}
	return FALSE;
}
