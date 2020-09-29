#include "pch.hpp"
#include "VectoredExceptionHandler.h"

namespace xSE
{
	bool VectoredExceptionHandler::Install(LONG(NTAPI *func)(_EXCEPTION_POINTERS*), Mode mode, Order order) noexcept
	{
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

			return m_Handle != nullptr;
		}
		return false;
	}
	bool VectoredExceptionHandler::Remove() noexcept
	{
		if (m_Handle)
		{
			void* handle = m_Handle;
			m_Handle = nullptr;

			if (m_Mode == Mode::ExceptionHandler)
			{
				return ::RemoveVectoredExceptionHandler(handle) != 0;
			}
			else if (m_Mode == Mode::ContinueHandler)
			{
				return ::RemoveVectoredContinueHandler(handle) != 0;
			}
		}
		return false;
	}
}
