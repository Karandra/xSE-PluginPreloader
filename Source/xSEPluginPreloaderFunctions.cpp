#include "pch.hpp"
#include "xSEPluginPreloader.h"
#include "ProxyFunctions/IpHlpAPI.h"
#include "ProxyFunctions/WinHTTP.h"
#include "ProxyFunctions/WinMM.h"

namespace
{
	void* g_OriginalFunctions[1024];
}

#if _WIN64
	#define ProxyAPI __declspec(dllexport, noinline) void
#else
	#define ProxyAPI __declspec(dllexport, noinline, naked) void
#endif

//////////////////////////////////////////////////////////////////////////
// Call procedures
//////////////////////////////////////////////////////////////////////////
#if _WIN64

extern "C"
{
	void* UnconditionalJumpAddress = nullptr;
	void UnconditionalJump();
}

#define CallOriginalFunc(enumName, name)	\
UnconditionalJumpAddress = g_OriginalFunctions[enumName::name];	\
UnconditionalJump();

#else

#define CallOriginalFunc(enumName, name)	\
{	\
	void* func = g_OriginalFunctions[enumName::name];	\
	__asm	\
	{	\
		jmp dword ptr [func]	\
	}	\
}	\

#endif

#define DefineProxyFunc(API, enumName, name)	\
API name()	\
{	\
	CallOriginalFunc(xSE::PluginPreloader::Library::enumName, name);	\
}	\

//////////////////////////////////////////////////////////////////////////
// IpHlpAPI
//////////////////////////////////////////////////////////////////////////
#define LoadFunc_IpHlpAPI(name) LoadOriginalFunc(IpHlpAPI, name)
#define CallFunc_IpHlpAPI(name) CallOriginalFunc(IpHlpAPI, name)
#define DefineFunc_IpHlpAPI(name) DefineProxyFunc(ProxyAPI, IpHlpAPI, name)

//////////////////////////////////////////////////////////////////////////
// WinHTTP
//////////////////////////////////////////////////////////////////////////
#define LoadFunc_WinHTTP(name) LoadOriginalFunc(WinHTTP, name)
#define CallFunc_WinHTTP(name) CallOriginalFunc(WinHTTP, name)
#define DefineFunc_WinHTTP(name) DefineProxyFunc(ProxyAPI, WinHTTP, name)

//////////////////////////////////////////////////////////////////////////
// WinMM
//////////////////////////////////////////////////////////////////////////
#define LoadFunc_WinMM(name) LoadOriginalFunc(WinMM, name)
#define CallFunc_WinMM(name) CallOriginalFunc(WinMM, name)
#define DefineFunc_WinMM(name) DefineProxyFunc(ProxyAPI, WinMM, name)

//////////////////////////////////////////////////////////////////////////
// Loading and stubs
//////////////////////////////////////////////////////////////////////////
#define LoadOriginalFunc(enumName, name)	g_OriginalFunctions[xSE::PluginPreloader::Library::enumName::name] = m_OriginalLibrary.GetExportedFunctionAddress(#name)

namespace xSE
{
	void** PreloadHandler::GetFunctions() noexcept
	{
		return g_OriginalFunctions;
	}
	size_t PreloadHandler::GetFunctionsCount() noexcept
	{
		return std::size(g_OriginalFunctions);
	}
	size_t PreloadHandler::GetFunctionsEffectiveCount() noexcept
	{
		return std::ranges::count_if(g_OriginalFunctions, [](void* ptr)
		{
			return ptr != nullptr;
		});
	}
	void PreloadHandler::LoadOriginalLibraryFunctions()
	{
		std::ranges::fill(g_OriginalFunctions, nullptr);

		#if xSE_PLATFORM_SKSE64 || xSE_PLATFORM_F4SE 

		LoadFunc_WinHTTP(WinHttpSetSecureLegacyServersAppCompat);
		LoadFunc_WinHTTP(DllCanUnloadNow);
		LoadFunc_WinHTTP(DllGetClassObject);
		LoadFunc_WinHTTP(Private1);
		LoadFunc_WinHTTP(SvchostPushServiceGlobals);
		LoadFunc_WinHTTP(WinHttpAddRequestHeaders);
		LoadFunc_WinHTTP(WinHttpAddRequestHeadersEx);
		LoadFunc_WinHTTP(WinHttpAutoProxySvcMain);
		LoadFunc_WinHTTP(WinHttpCheckPlatform);
		LoadFunc_WinHTTP(WinHttpCloseHandle);
		LoadFunc_WinHTTP(WinHttpConnect);
		LoadFunc_WinHTTP(WinHttpConnectionDeletePolicyEntries);
		LoadFunc_WinHTTP(WinHttpConnectionDeleteProxyInfo);
		LoadFunc_WinHTTP(WinHttpConnectionFreeNameList);
		LoadFunc_WinHTTP(WinHttpConnectionFreeProxyInfo);
		LoadFunc_WinHTTP(WinHttpConnectionFreeProxyList);
		LoadFunc_WinHTTP(WinHttpConnectionGetNameList);
		LoadFunc_WinHTTP(WinHttpConnectionGetProxyInfo);
		LoadFunc_WinHTTP(WinHttpConnectionGetProxyList);
		LoadFunc_WinHTTP(WinHttpConnectionOnlyConvert);
		LoadFunc_WinHTTP(WinHttpConnectionOnlyReceive);
		LoadFunc_WinHTTP(WinHttpConnectionOnlySend);
		LoadFunc_WinHTTP(WinHttpConnectionSetPolicyEntries);
		LoadFunc_WinHTTP(WinHttpConnectionSetProxyInfo);
		LoadFunc_WinHTTP(WinHttpConnectionUpdateIfIndexTable);
		LoadFunc_WinHTTP(WinHttpCrackUrl);
		LoadFunc_WinHTTP(WinHttpCreateProxyResolver);
		LoadFunc_WinHTTP(WinHttpCreateUrl);
		LoadFunc_WinHTTP(WinHttpDetectAutoProxyConfigUrl);
		LoadFunc_WinHTTP(WinHttpFreeProxyResult);
		LoadFunc_WinHTTP(WinHttpFreeProxyResultEx);
		LoadFunc_WinHTTP(WinHttpFreeProxySettings);
		LoadFunc_WinHTTP(WinHttpFreeProxySettingsEx);
		LoadFunc_WinHTTP(WinHttpFreeQueryConnectionGroupResult);
		LoadFunc_WinHTTP(WinHttpGetDefaultProxyConfiguration);
		LoadFunc_WinHTTP(WinHttpGetIEProxyConfigForCurrentUser);
		LoadFunc_WinHTTP(WinHttpGetProxyForUrl);
		LoadFunc_WinHTTP(WinHttpGetProxyForUrlEx);
		LoadFunc_WinHTTP(WinHttpGetProxyForUrlEx2);
		LoadFunc_WinHTTP(WinHttpGetProxyForUrlHvsi);
		LoadFunc_WinHTTP(WinHttpGetProxyResult);
		LoadFunc_WinHTTP(WinHttpGetProxyResultEx);
		LoadFunc_WinHTTP(WinHttpGetProxySettingsEx);
		LoadFunc_WinHTTP(WinHttpGetProxySettingsResultEx);
		LoadFunc_WinHTTP(WinHttpGetProxySettingsVersion);
		LoadFunc_WinHTTP(WinHttpGetTunnelSocket);
		LoadFunc_WinHTTP(WinHttpOpen);
		LoadFunc_WinHTTP(WinHttpOpenRequest);
		LoadFunc_WinHTTP(WinHttpPacJsWorkerMain);
		LoadFunc_WinHTTP(WinHttpProbeConnectivity);
		LoadFunc_WinHTTP(WinHttpQueryAuthSchemes);
		LoadFunc_WinHTTP(WinHttpQueryConnectionGroup);
		LoadFunc_WinHTTP(WinHttpQueryDataAvailable);
		LoadFunc_WinHTTP(WinHttpQueryHeaders);
		LoadFunc_WinHTTP(WinHttpQueryHeadersEx);
		LoadFunc_WinHTTP(WinHttpQueryOption);
		LoadFunc_WinHTTP(WinHttpReadData);
		LoadFunc_WinHTTP(WinHttpReadDataEx);
		LoadFunc_WinHTTP(WinHttpReadProxySettings);
		LoadFunc_WinHTTP(WinHttpReadProxySettingsHvsi);
		LoadFunc_WinHTTP(WinHttpReceiveResponse);
		LoadFunc_WinHTTP(WinHttpRegisterProxyChangeNotification);
		LoadFunc_WinHTTP(WinHttpResetAutoProxy);
		LoadFunc_WinHTTP(WinHttpSaveProxyCredentials);
		LoadFunc_WinHTTP(WinHttpSendRequest);
		LoadFunc_WinHTTP(WinHttpSetCredentials);
		LoadFunc_WinHTTP(WinHttpSetDefaultProxyConfiguration);
		LoadFunc_WinHTTP(WinHttpSetOption);
		LoadFunc_WinHTTP(WinHttpSetProxySettingsPerUser);
		LoadFunc_WinHTTP(WinHttpSetStatusCallback);
		LoadFunc_WinHTTP(WinHttpSetTimeouts);
		LoadFunc_WinHTTP(WinHttpTimeFromSystemTime);
		LoadFunc_WinHTTP(WinHttpTimeToSystemTime);
		LoadFunc_WinHTTP(WinHttpUnregisterProxyChangeNotification);
		LoadFunc_WinHTTP(WinHttpWebSocketClose);
		LoadFunc_WinHTTP(WinHttpWebSocketCompleteUpgrade);
		LoadFunc_WinHTTP(WinHttpWebSocketQueryCloseStatus);
		LoadFunc_WinHTTP(WinHttpWebSocketReceive);
		LoadFunc_WinHTTP(WinHttpWebSocketSend);
		LoadFunc_WinHTTP(WinHttpWebSocketShutdown);
		LoadFunc_WinHTTP(WinHttpWriteData);
		LoadFunc_WinHTTP(WinHttpWriteProxySettings);

		#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE

		LoadFunc_WinMM(Ordinal2);
		LoadFunc_WinMM(CloseDriver);
		LoadFunc_WinMM(DefDriverProc);
		LoadFunc_WinMM(DriverCallback);
		LoadFunc_WinMM(DrvGetModuleHandle);
		LoadFunc_WinMM(GetDriverModuleHandle);
		LoadFunc_WinMM(OpenDriver);
		LoadFunc_WinMM(PlaySound);
		LoadFunc_WinMM(PlaySoundA);
		LoadFunc_WinMM(PlaySoundW);
		LoadFunc_WinMM(SendDriverMessage);
		LoadFunc_WinMM(WOWAppExit);
		LoadFunc_WinMM(auxGetDevCapsA);
		LoadFunc_WinMM(auxGetDevCapsW);
		LoadFunc_WinMM(auxGetNumDevs);
		LoadFunc_WinMM(auxGetVolume);
		LoadFunc_WinMM(auxOutMessage);
		LoadFunc_WinMM(auxSetVolume);
		LoadFunc_WinMM(joyConfigChanged);
		LoadFunc_WinMM(joyGetDevCapsA);
		LoadFunc_WinMM(joyGetDevCapsW);
		LoadFunc_WinMM(joyGetNumDevs);
		LoadFunc_WinMM(joyGetPos);
		LoadFunc_WinMM(joyGetPosEx);
		LoadFunc_WinMM(joyGetThreshold);
		LoadFunc_WinMM(joyReleaseCapture);
		LoadFunc_WinMM(joySetCapture);
		LoadFunc_WinMM(joySetThreshold);
		LoadFunc_WinMM(mciDriverNotify);
		LoadFunc_WinMM(mciDriverYield);
		LoadFunc_WinMM(mciExecute);
		LoadFunc_WinMM(mciFreeCommandResource);
		LoadFunc_WinMM(mciGetCreatorTask);
		LoadFunc_WinMM(mciGetDeviceIDA);
		LoadFunc_WinMM(mciGetDeviceIDFromElementIDA);
		LoadFunc_WinMM(mciGetDeviceIDFromElementIDW);
		LoadFunc_WinMM(mciGetDeviceIDW);
		LoadFunc_WinMM(mciGetDriverData);
		LoadFunc_WinMM(mciGetErrorStringA);
		LoadFunc_WinMM(mciGetErrorStringW);
		LoadFunc_WinMM(mciGetYieldProc);
		LoadFunc_WinMM(mciLoadCommandResource);
		LoadFunc_WinMM(mciSendCommandA);
		LoadFunc_WinMM(mciSendCommandW);
		LoadFunc_WinMM(mciSendStringA);
		LoadFunc_WinMM(mciSendStringW);
		LoadFunc_WinMM(mciSetDriverData);
		LoadFunc_WinMM(mciSetYieldProc);
		LoadFunc_WinMM(midiConnect);
		LoadFunc_WinMM(midiDisconnect);
		LoadFunc_WinMM(midiInAddBuffer);
		LoadFunc_WinMM(midiInClose);
		LoadFunc_WinMM(midiInGetDevCapsA);
		LoadFunc_WinMM(midiInGetDevCapsW);
		LoadFunc_WinMM(midiInGetErrorTextA);
		LoadFunc_WinMM(midiInGetErrorTextW);
		LoadFunc_WinMM(midiInGetID);
		LoadFunc_WinMM(midiInGetNumDevs);
		LoadFunc_WinMM(midiInMessage);
		LoadFunc_WinMM(midiInOpen);
		LoadFunc_WinMM(midiInPrepareHeader);
		LoadFunc_WinMM(midiInReset);
		LoadFunc_WinMM(midiInStart);
		LoadFunc_WinMM(midiInStop);
		LoadFunc_WinMM(midiInUnprepareHeader);
		LoadFunc_WinMM(midiOutCacheDrumPatches);
		LoadFunc_WinMM(midiOutCachePatches);
		LoadFunc_WinMM(midiOutClose);
		LoadFunc_WinMM(midiOutGetDevCapsA);
		LoadFunc_WinMM(midiOutGetDevCapsW);
		LoadFunc_WinMM(midiOutGetErrorTextA);
		LoadFunc_WinMM(midiOutGetErrorTextW);
		LoadFunc_WinMM(midiOutGetID);
		LoadFunc_WinMM(midiOutGetNumDevs);
		LoadFunc_WinMM(midiOutGetVolume);
		LoadFunc_WinMM(midiOutLongMsg);
		LoadFunc_WinMM(midiOutMessage);
		LoadFunc_WinMM(midiOutOpen);
		LoadFunc_WinMM(midiOutPrepareHeader);
		LoadFunc_WinMM(midiOutReset);
		LoadFunc_WinMM(midiOutSetVolume);
		LoadFunc_WinMM(midiOutShortMsg);
		LoadFunc_WinMM(midiOutUnprepareHeader);
		LoadFunc_WinMM(midiStreamClose);
		LoadFunc_WinMM(midiStreamOpen);
		LoadFunc_WinMM(midiStreamOut);
		LoadFunc_WinMM(midiStreamPause);
		LoadFunc_WinMM(midiStreamPosition);
		LoadFunc_WinMM(midiStreamProperty);
		LoadFunc_WinMM(midiStreamRestart);
		LoadFunc_WinMM(midiStreamStop);
		LoadFunc_WinMM(mixerClose);
		LoadFunc_WinMM(mixerGetControlDetailsA);
		LoadFunc_WinMM(mixerGetControlDetailsW);
		LoadFunc_WinMM(mixerGetDevCapsA);
		LoadFunc_WinMM(mixerGetDevCapsW);
		LoadFunc_WinMM(mixerGetID);
		LoadFunc_WinMM(mixerGetLineControlsA);
		LoadFunc_WinMM(mixerGetLineControlsW);
		LoadFunc_WinMM(mixerGetLineInfoA);
		LoadFunc_WinMM(mixerGetLineInfoW);
		LoadFunc_WinMM(mixerGetNumDevs);
		LoadFunc_WinMM(mixerMessage);
		LoadFunc_WinMM(mixerOpen);
		LoadFunc_WinMM(mixerSetControlDetails);
		LoadFunc_WinMM(mmDrvInstall);
		LoadFunc_WinMM(mmGetCurrentTask);
		LoadFunc_WinMM(mmTaskBlock);
		LoadFunc_WinMM(mmTaskCreate);
		LoadFunc_WinMM(mmTaskSignal);
		LoadFunc_WinMM(mmTaskYield);
		LoadFunc_WinMM(mmioAdvance);
		LoadFunc_WinMM(mmioAscend);
		LoadFunc_WinMM(mmioClose);
		LoadFunc_WinMM(mmioCreateChunk);
		LoadFunc_WinMM(mmioDescend);
		LoadFunc_WinMM(mmioFlush);
		LoadFunc_WinMM(mmioGetInfo);
		LoadFunc_WinMM(mmioInstallIOProcA);
		LoadFunc_WinMM(mmioInstallIOProcW);
		LoadFunc_WinMM(mmioOpenA);
		LoadFunc_WinMM(mmioOpenW);
		LoadFunc_WinMM(mmioRead);
		LoadFunc_WinMM(mmioRenameA);
		LoadFunc_WinMM(mmioRenameW);
		LoadFunc_WinMM(mmioSeek);
		LoadFunc_WinMM(mmioSendMessage);
		LoadFunc_WinMM(mmioSetBuffer);
		LoadFunc_WinMM(mmioSetInfo);
		LoadFunc_WinMM(mmioStringToFOURCCA);
		LoadFunc_WinMM(mmioStringToFOURCCW);
		LoadFunc_WinMM(mmioWrite);
		LoadFunc_WinMM(mmsystemGetVersion);
		LoadFunc_WinMM(sndPlaySoundA);
		LoadFunc_WinMM(sndPlaySoundW);
		LoadFunc_WinMM(timeBeginPeriod);
		LoadFunc_WinMM(timeEndPeriod);
		LoadFunc_WinMM(timeGetDevCaps);
		LoadFunc_WinMM(timeGetSystemTime);
		LoadFunc_WinMM(timeGetTime);
		LoadFunc_WinMM(timeKillEvent);
		LoadFunc_WinMM(timeSetEvent);
		LoadFunc_WinMM(waveInAddBuffer);
		LoadFunc_WinMM(waveInClose);
		LoadFunc_WinMM(waveInGetDevCapsA);
		LoadFunc_WinMM(waveInGetDevCapsW);
		LoadFunc_WinMM(waveInGetErrorTextA);
		LoadFunc_WinMM(waveInGetErrorTextW);
		LoadFunc_WinMM(waveInGetID);
		LoadFunc_WinMM(waveInGetNumDevs);
		LoadFunc_WinMM(waveInGetPosition);
		LoadFunc_WinMM(waveInMessage);
		LoadFunc_WinMM(waveInOpen);
		LoadFunc_WinMM(waveInPrepareHeader);
		LoadFunc_WinMM(waveInReset);
		LoadFunc_WinMM(waveInStart);
		LoadFunc_WinMM(waveInStop);
		LoadFunc_WinMM(waveInUnprepareHeader);
		LoadFunc_WinMM(waveOutBreakLoop);
		LoadFunc_WinMM(waveOutClose);
		LoadFunc_WinMM(waveOutGetDevCapsA);
		LoadFunc_WinMM(waveOutGetDevCapsW);
		LoadFunc_WinMM(waveOutGetErrorTextA);
		LoadFunc_WinMM(waveOutGetErrorTextW);
		LoadFunc_WinMM(waveOutGetID);
		LoadFunc_WinMM(waveOutGetNumDevs);
		LoadFunc_WinMM(waveOutGetPitch);
		LoadFunc_WinMM(waveOutGetPlaybackRate);
		LoadFunc_WinMM(waveOutGetPosition);
		LoadFunc_WinMM(waveOutGetVolume);
		LoadFunc_WinMM(waveOutMessage);
		LoadFunc_WinMM(waveOutOpen);
		LoadFunc_WinMM(waveOutPause);
		LoadFunc_WinMM(waveOutPrepareHeader);
		LoadFunc_WinMM(waveOutReset);
		LoadFunc_WinMM(waveOutRestart);
		LoadFunc_WinMM(waveOutSetPitch);
		LoadFunc_WinMM(waveOutSetPlaybackRate);
		LoadFunc_WinMM(waveOutSetVolume);
		LoadFunc_WinMM(waveOutUnprepareHeader);
		LoadFunc_WinMM(waveOutWrite);

		#else

		#error "Unsupported configuration"

		#endif
	}
}

extern "C"
{
	#if xSE_PLATFORM_F4SE || xSE_PLATFORM_SKSE64
	
	DefineFunc_WinHTTP(WinHttpSetSecureLegacyServersAppCompat);
	DefineFunc_WinHTTP(DllCanUnloadNow);
	DefineFunc_WinHTTP(DllGetClassObject);
	DefineFunc_WinHTTP(Private1);
	DefineFunc_WinHTTP(SvchostPushServiceGlobals);
	DefineFunc_WinHTTP(WinHttpAddRequestHeaders);
	DefineFunc_WinHTTP(WinHttpAddRequestHeadersEx);
	DefineFunc_WinHTTP(WinHttpAutoProxySvcMain);
	DefineFunc_WinHTTP(WinHttpCheckPlatform);
	DefineFunc_WinHTTP(WinHttpCloseHandle);
	DefineFunc_WinHTTP(WinHttpConnect);
	DefineFunc_WinHTTP(WinHttpConnectionDeletePolicyEntries);
	DefineFunc_WinHTTP(WinHttpConnectionDeleteProxyInfo);
	DefineFunc_WinHTTP(WinHttpConnectionFreeNameList);
	DefineFunc_WinHTTP(WinHttpConnectionFreeProxyInfo);
	DefineFunc_WinHTTP(WinHttpConnectionFreeProxyList);
	DefineFunc_WinHTTP(WinHttpConnectionGetNameList);
	DefineFunc_WinHTTP(WinHttpConnectionGetProxyInfo);
	DefineFunc_WinHTTP(WinHttpConnectionGetProxyList);
	DefineFunc_WinHTTP(WinHttpConnectionOnlyConvert);
	DefineFunc_WinHTTP(WinHttpConnectionOnlyReceive);
	DefineFunc_WinHTTP(WinHttpConnectionOnlySend);
	DefineFunc_WinHTTP(WinHttpConnectionSetPolicyEntries);
	DefineFunc_WinHTTP(WinHttpConnectionSetProxyInfo);
	DefineFunc_WinHTTP(WinHttpConnectionUpdateIfIndexTable);
	DefineFunc_WinHTTP(WinHttpCrackUrl);
	DefineFunc_WinHTTP(WinHttpCreateProxyResolver);
	DefineFunc_WinHTTP(WinHttpCreateUrl);
	DefineFunc_WinHTTP(WinHttpDetectAutoProxyConfigUrl);
	DefineFunc_WinHTTP(WinHttpFreeProxyResult);
	DefineFunc_WinHTTP(WinHttpFreeProxyResultEx);
	DefineFunc_WinHTTP(WinHttpFreeProxySettings);
	DefineFunc_WinHTTP(WinHttpFreeProxySettingsEx);
	DefineFunc_WinHTTP(WinHttpFreeQueryConnectionGroupResult);
	DefineFunc_WinHTTP(WinHttpGetDefaultProxyConfiguration);
	DefineFunc_WinHTTP(WinHttpGetIEProxyConfigForCurrentUser);
	DefineFunc_WinHTTP(WinHttpGetProxyForUrl);
	DefineFunc_WinHTTP(WinHttpGetProxyForUrlEx);
	DefineFunc_WinHTTP(WinHttpGetProxyForUrlEx2);
	DefineFunc_WinHTTP(WinHttpGetProxyForUrlHvsi);
	DefineFunc_WinHTTP(WinHttpGetProxyResult);
	DefineFunc_WinHTTP(WinHttpGetProxyResultEx);
	DefineFunc_WinHTTP(WinHttpGetProxySettingsEx);
	DefineFunc_WinHTTP(WinHttpGetProxySettingsResultEx);
	DefineFunc_WinHTTP(WinHttpGetProxySettingsVersion);
	DefineFunc_WinHTTP(WinHttpGetTunnelSocket);
	DefineFunc_WinHTTP(WinHttpOpen);
	DefineFunc_WinHTTP(WinHttpOpenRequest);
	DefineFunc_WinHTTP(WinHttpPacJsWorkerMain);
	DefineFunc_WinHTTP(WinHttpProbeConnectivity);
	DefineFunc_WinHTTP(WinHttpQueryAuthSchemes);
	DefineFunc_WinHTTP(WinHttpQueryConnectionGroup);
	DefineFunc_WinHTTP(WinHttpQueryDataAvailable);
	DefineFunc_WinHTTP(WinHttpQueryHeaders);
	DefineFunc_WinHTTP(WinHttpQueryHeadersEx);
	DefineFunc_WinHTTP(WinHttpQueryOption);
	DefineFunc_WinHTTP(WinHttpReadData);
	DefineFunc_WinHTTP(WinHttpReadDataEx);
	DefineFunc_WinHTTP(WinHttpReadProxySettings);
	DefineFunc_WinHTTP(WinHttpReadProxySettingsHvsi);
	DefineFunc_WinHTTP(WinHttpReceiveResponse);
	DefineFunc_WinHTTP(WinHttpRegisterProxyChangeNotification);
	DefineFunc_WinHTTP(WinHttpResetAutoProxy);
	DefineFunc_WinHTTP(WinHttpSaveProxyCredentials);
	DefineFunc_WinHTTP(WinHttpSendRequest);
	DefineFunc_WinHTTP(WinHttpSetCredentials);
	DefineFunc_WinHTTP(WinHttpSetDefaultProxyConfiguration);
	DefineFunc_WinHTTP(WinHttpSetOption);
	DefineFunc_WinHTTP(WinHttpSetProxySettingsPerUser);
	DefineFunc_WinHTTP(WinHttpSetStatusCallback);
	DefineFunc_WinHTTP(WinHttpSetTimeouts);
	DefineFunc_WinHTTP(WinHttpTimeFromSystemTime);
	DefineFunc_WinHTTP(WinHttpTimeToSystemTime);
	DefineFunc_WinHTTP(WinHttpUnregisterProxyChangeNotification);
	DefineFunc_WinHTTP(WinHttpWebSocketClose);
	DefineFunc_WinHTTP(WinHttpWebSocketCompleteUpgrade);
	DefineFunc_WinHTTP(WinHttpWebSocketQueryCloseStatus);
	DefineFunc_WinHTTP(WinHttpWebSocketReceive);
	DefineFunc_WinHTTP(WinHttpWebSocketSend);
	DefineFunc_WinHTTP(WinHttpWebSocketShutdown);
	DefineFunc_WinHTTP(WinHttpWriteData);
	DefineFunc_WinHTTP(WinHttpWriteProxySettings);

	#elif xSE_PLATFORM_SKSE || xSE_PLATFORM_NVSE

	DefineFunc_WinMM(Ordinal2);
	DefineFunc_WinMM(CloseDriver);
	DefineFunc_WinMM(DefDriverProc);
	DefineFunc_WinMM(DriverCallback);
	DefineFunc_WinMM(DrvGetModuleHandle);
	DefineFunc_WinMM(GetDriverModuleHandle);
	DefineFunc_WinMM(OpenDriver);
	DefineFunc_WinMM(PlaySound);
	DefineFunc_WinMM(PlaySoundA);
	DefineFunc_WinMM(PlaySoundW);
	DefineFunc_WinMM(SendDriverMessage);
	DefineFunc_WinMM(WOWAppExit);
	DefineFunc_WinMM(auxGetDevCapsA);
	DefineFunc_WinMM(auxGetDevCapsW);
	DefineFunc_WinMM(auxGetNumDevs);
	DefineFunc_WinMM(auxGetVolume);
	DefineFunc_WinMM(auxOutMessage);
	DefineFunc_WinMM(auxSetVolume);
	DefineFunc_WinMM(joyConfigChanged);
	DefineFunc_WinMM(joyGetDevCapsA);
	DefineFunc_WinMM(joyGetDevCapsW);
	DefineFunc_WinMM(joyGetNumDevs);
	DefineFunc_WinMM(joyGetPos);
	DefineFunc_WinMM(joyGetPosEx);
	DefineFunc_WinMM(joyGetThreshold);
	DefineFunc_WinMM(joyReleaseCapture);
	DefineFunc_WinMM(joySetCapture);
	DefineFunc_WinMM(joySetThreshold);
	DefineFunc_WinMM(mciDriverNotify);
	DefineFunc_WinMM(mciDriverYield);
	DefineFunc_WinMM(mciExecute);
	DefineFunc_WinMM(mciFreeCommandResource);
	DefineFunc_WinMM(mciGetCreatorTask);
	DefineFunc_WinMM(mciGetDeviceIDA);
	DefineFunc_WinMM(mciGetDeviceIDFromElementIDA);
	DefineFunc_WinMM(mciGetDeviceIDFromElementIDW);
	DefineFunc_WinMM(mciGetDeviceIDW);
	DefineFunc_WinMM(mciGetDriverData);
	DefineFunc_WinMM(mciGetErrorStringA);
	DefineFunc_WinMM(mciGetErrorStringW);
	DefineFunc_WinMM(mciGetYieldProc);
	DefineFunc_WinMM(mciLoadCommandResource);
	DefineFunc_WinMM(mciSendCommandA);
	DefineFunc_WinMM(mciSendCommandW);
	DefineFunc_WinMM(mciSendStringA);
	DefineFunc_WinMM(mciSendStringW);
	DefineFunc_WinMM(mciSetDriverData);
	DefineFunc_WinMM(mciSetYieldProc);
	DefineFunc_WinMM(midiConnect);
	DefineFunc_WinMM(midiDisconnect);
	DefineFunc_WinMM(midiInAddBuffer);
	DefineFunc_WinMM(midiInClose);
	DefineFunc_WinMM(midiInGetDevCapsA);
	DefineFunc_WinMM(midiInGetDevCapsW);
	DefineFunc_WinMM(midiInGetErrorTextA);
	DefineFunc_WinMM(midiInGetErrorTextW);
	DefineFunc_WinMM(midiInGetID);
	DefineFunc_WinMM(midiInGetNumDevs);
	DefineFunc_WinMM(midiInMessage);
	DefineFunc_WinMM(midiInOpen);
	DefineFunc_WinMM(midiInPrepareHeader);
	DefineFunc_WinMM(midiInReset);
	DefineFunc_WinMM(midiInStart);
	DefineFunc_WinMM(midiInStop);
	DefineFunc_WinMM(midiInUnprepareHeader);
	DefineFunc_WinMM(midiOutCacheDrumPatches);
	DefineFunc_WinMM(midiOutCachePatches);
	DefineFunc_WinMM(midiOutClose);
	DefineFunc_WinMM(midiOutGetDevCapsA);
	DefineFunc_WinMM(midiOutGetDevCapsW);
	DefineFunc_WinMM(midiOutGetErrorTextA);
	DefineFunc_WinMM(midiOutGetErrorTextW);
	DefineFunc_WinMM(midiOutGetID);
	DefineFunc_WinMM(midiOutGetNumDevs);
	DefineFunc_WinMM(midiOutGetVolume);
	DefineFunc_WinMM(midiOutLongMsg);
	DefineFunc_WinMM(midiOutMessage);
	DefineFunc_WinMM(midiOutOpen);
	DefineFunc_WinMM(midiOutPrepareHeader);
	DefineFunc_WinMM(midiOutReset);
	DefineFunc_WinMM(midiOutSetVolume);
	DefineFunc_WinMM(midiOutShortMsg);
	DefineFunc_WinMM(midiOutUnprepareHeader);
	DefineFunc_WinMM(midiStreamClose);
	DefineFunc_WinMM(midiStreamOpen);
	DefineFunc_WinMM(midiStreamOut);
	DefineFunc_WinMM(midiStreamPause);
	DefineFunc_WinMM(midiStreamPosition);
	DefineFunc_WinMM(midiStreamProperty);
	DefineFunc_WinMM(midiStreamRestart);
	DefineFunc_WinMM(midiStreamStop);
	DefineFunc_WinMM(mixerClose);
	DefineFunc_WinMM(mixerGetControlDetailsA);
	DefineFunc_WinMM(mixerGetControlDetailsW);
	DefineFunc_WinMM(mixerGetDevCapsA);
	DefineFunc_WinMM(mixerGetDevCapsW);
	DefineFunc_WinMM(mixerGetID);
	DefineFunc_WinMM(mixerGetLineControlsA);
	DefineFunc_WinMM(mixerGetLineControlsW);
	DefineFunc_WinMM(mixerGetLineInfoA);
	DefineFunc_WinMM(mixerGetLineInfoW);
	DefineFunc_WinMM(mixerGetNumDevs);
	DefineFunc_WinMM(mixerMessage);
	DefineFunc_WinMM(mixerOpen);
	DefineFunc_WinMM(mixerSetControlDetails);
	DefineFunc_WinMM(mmDrvInstall);
	DefineFunc_WinMM(mmGetCurrentTask);
	DefineFunc_WinMM(mmTaskBlock);
	DefineFunc_WinMM(mmTaskCreate);
	DefineFunc_WinMM(mmTaskSignal);
	DefineFunc_WinMM(mmTaskYield);
	DefineFunc_WinMM(mmioAdvance);
	DefineFunc_WinMM(mmioAscend);
	DefineFunc_WinMM(mmioClose);
	DefineFunc_WinMM(mmioCreateChunk);
	DefineFunc_WinMM(mmioDescend);
	DefineFunc_WinMM(mmioFlush);
	DefineFunc_WinMM(mmioGetInfo);
	DefineFunc_WinMM(mmioInstallIOProcA);
	DefineFunc_WinMM(mmioInstallIOProcW);
	DefineFunc_WinMM(mmioOpenA);
	DefineFunc_WinMM(mmioOpenW);
	DefineFunc_WinMM(mmioRead);
	DefineFunc_WinMM(mmioRenameA);
	DefineFunc_WinMM(mmioRenameW);
	DefineFunc_WinMM(mmioSeek);
	DefineFunc_WinMM(mmioSendMessage);
	DefineFunc_WinMM(mmioSetBuffer);
	DefineFunc_WinMM(mmioSetInfo);
	DefineFunc_WinMM(mmioStringToFOURCCA);
	DefineFunc_WinMM(mmioStringToFOURCCW);
	DefineFunc_WinMM(mmioWrite);
	DefineFunc_WinMM(mmsystemGetVersion);
	DefineFunc_WinMM(sndPlaySoundA);
	DefineFunc_WinMM(sndPlaySoundW);
	DefineFunc_WinMM(timeBeginPeriod);
	DefineFunc_WinMM(timeEndPeriod);
	DefineFunc_WinMM(timeGetDevCaps);
	DefineFunc_WinMM(timeGetSystemTime);
	DefineFunc_WinMM(timeGetTime);
	DefineFunc_WinMM(timeKillEvent);
	DefineFunc_WinMM(timeSetEvent);
	DefineFunc_WinMM(waveInAddBuffer);
	DefineFunc_WinMM(waveInClose);
	DefineFunc_WinMM(waveInGetDevCapsA);
	DefineFunc_WinMM(waveInGetDevCapsW);
	DefineFunc_WinMM(waveInGetErrorTextA);
	DefineFunc_WinMM(waveInGetErrorTextW);
	DefineFunc_WinMM(waveInGetID);
	DefineFunc_WinMM(waveInGetNumDevs);
	DefineFunc_WinMM(waveInGetPosition);
	DefineFunc_WinMM(waveInMessage);
	DefineFunc_WinMM(waveInOpen);
	DefineFunc_WinMM(waveInPrepareHeader);
	DefineFunc_WinMM(waveInReset);
	DefineFunc_WinMM(waveInStart);
	DefineFunc_WinMM(waveInStop);
	DefineFunc_WinMM(waveInUnprepareHeader);
	DefineFunc_WinMM(waveOutBreakLoop);
	DefineFunc_WinMM(waveOutClose);
	DefineFunc_WinMM(waveOutGetDevCapsA);
	DefineFunc_WinMM(waveOutGetDevCapsW);
	DefineFunc_WinMM(waveOutGetErrorTextA);
	DefineFunc_WinMM(waveOutGetErrorTextW);
	DefineFunc_WinMM(waveOutGetID);
	DefineFunc_WinMM(waveOutGetNumDevs);
	DefineFunc_WinMM(waveOutGetPitch);
	DefineFunc_WinMM(waveOutGetPlaybackRate);
	DefineFunc_WinMM(waveOutGetPosition);
	DefineFunc_WinMM(waveOutGetVolume);
	DefineFunc_WinMM(waveOutMessage);
	DefineFunc_WinMM(waveOutOpen);
	DefineFunc_WinMM(waveOutPause);
	DefineFunc_WinMM(waveOutPrepareHeader);
	DefineFunc_WinMM(waveOutReset);
	DefineFunc_WinMM(waveOutRestart);
	DefineFunc_WinMM(waveOutSetPitch);
	DefineFunc_WinMM(waveOutSetPlaybackRate);
	DefineFunc_WinMM(waveOutSetVolume);
	DefineFunc_WinMM(waveOutUnprepareHeader);
	DefineFunc_WinMM(waveOutWrite);

	#else

	#error "Unsupported configuration"

	#endif
};
