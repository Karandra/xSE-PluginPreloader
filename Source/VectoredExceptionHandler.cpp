#include "pch.hpp"
#include "VectoredExceptionHandler.h"

namespace xSE
{
	bool VectoredExceptionHandler::Install(LONG(NTAPI *func)(_EXCEPTION_POINTERS*), Mode mode, Order order) noexcept
	{
		KX_SCOPEDLOG_ARGS(func, mode, order);

		if (!m_Handle)
		{
			m_Mode = mode;
			if (mode == Mode::ExceptionHandler)
			{
				m_Handle = ::AddVectoredExceptionHandler(order == Order::First, func);
			}
			else if (mode == Mode::ContinueHandler)
			{
				m_Handle = ::AddVectoredContinueHandler(order == Order::First, func);
			}

			KX_SCOPEDLOG.SetSuccess();
			return m_Handle != nullptr;
		}
		return false;
	}
	bool VectoredExceptionHandler::Remove() noexcept
	{
		KX_SCOPEDLOG_FUNC;
		KX_SCOPEDLOG.Info() KX_SCOPEDLOG_VALUE(m_Handle);

		if (m_Handle)
		{
			void* handle = m_Handle;
			m_Handle = nullptr;

			ULONG result = 0;
			if (m_Mode == Mode::ExceptionHandler)
			{
				result = ::RemoveVectoredExceptionHandler(handle);
			}
			else if (m_Mode == Mode::ContinueHandler)
			{
				result = ::RemoveVectoredContinueHandler(handle);
			}
			bool success = result != 0;

			KX_SCOPEDLOG.LogReturn(success, success);
			return success;
		}

		KX_SCOPEDLOG.LogReturn(false);
		return false;
	}
}
