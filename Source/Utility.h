#pragma once
#include "Framework.hpp"

namespace xSE::Utility
{
	template<class TFunc>
	__declspec(noinline) uint32_t SEHTryExcept(TFunc&& func) noexcept(std::is_nothrow_invocable_v<TFunc>)
	{
		__try
		{
			std::invoke(func);
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return ::GetExceptionCode();
		}
	}
}
