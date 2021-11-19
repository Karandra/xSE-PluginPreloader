#pragma once
#include "Framework.hpp"

namespace xSE::Detour::Private
{
	uintptr_t FunctionIAT(uintptr_t func, const char* libraryName, const char* functionName) noexcept;
	uintptr_t FunctionFromModule(HMODULE moduleBase, uintptr_t func, uintptr_t offset) noexcept;
}

namespace xSE::Detour
{
	template<class T> requires(std::is_function_v<T>)
	T* FunctionIAT(T* func, const char* libraryName, const char* functionName) noexcept
	{
		return reinterpret_cast<T*>(Private::FunctionIAT(reinterpret_cast<uintptr_t>(func), libraryName, functionName));
	}

	template<class T> requires(std::is_function_v<T>)
	T FunctionFromModule(HMODULE moduleBase, T func, uintptr_t offset) noexcept
	{
		return reinterpret_cast<T*>(Private::FunctionFromModule(moduleBase, reinterpret_cast<uintptr_t>(func), offset));
	}

	template<class T> requires(std::is_function_v<T>)
	T* FunctionFromModuleByName(const wchar_t* libraryName, T* func, uintptr_t offset) noexcept
	{
		return FunctionFromModule(::GetModuleHandleW(libraryName), func, offset);
	}

	template<class T> requires(std::is_function_v<T>)
	T* FunctionFromExecutingModule(T* func, uintptr_t offset) noexcept
	{
		return FunctionFromModuleByName(nullptr, func, offset);
	}
}
