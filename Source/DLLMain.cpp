#include "pch.hpp"
#include "xSEPluginPreloader.h"

BOOL APIENTRY DllMain(HMODULE handle, DWORD event, LPVOID lpReserved)
{
	if (event == DLL_PROCESS_ATTACH)
	{
		auto& handler = xSE::PreloadHandler::CreateInstance();
		if (handler.IsNull())
		{
			kxf::Log::Critical("Invalid state of 'PreloadHandler' reported, terminating process");
			return FALSE;
		}

		return handler.OnDLLMain(handle, event) ? TRUE : FALSE;
	}
	else if (auto instance = xSE::PreloadHandler::GetInstance())
	{
		return instance->OnDLLMain(handle, event) ? TRUE : FALSE;
	}
	return FALSE;
}
