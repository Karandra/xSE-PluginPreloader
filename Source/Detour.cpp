#include "pch.hpp"
#include "Detour.h"

#include "Nukem Detours/Detours.h"
#if _WIN64

namespace NukemDetoursBase = Detours;
namespace NukemDetours = Detours::X64;
using NukemDetoursOpt = Detours::X64Option;
#pragma comment(lib, "Nukem Detours/x64/detours.lib")

#else

namespace NukemDetoursBase = Detours;
namespace NukemDetours = Detours::X86;
using NukemDetoursOpt = Detours::X86Option;
#pragma comment(lib, "Nukem Detours/x86/detours.lib")

#endif

namespace xSE::Detour::Private
{
	uintptr_t FunctionIAT(uintptr_t func, const char* libraryName, const char* functionName) noexcept
	{
		uintptr_t base = reinterpret_cast<uintptr_t>(::GetModuleHandleW(nullptr));
		return NukemDetoursBase::IATHook(base, libraryName, functionName, func);
	}
	uintptr_t FunctionFromModule(HMODULE moduleBase, uintptr_t func, uintptr_t offset) noexcept
	{
		return NukemDetours::DetourFunction(reinterpret_cast<uintptr_t>(moduleBase) + offset, func);
	}
}
