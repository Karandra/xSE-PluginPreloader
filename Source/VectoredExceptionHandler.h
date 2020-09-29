#pragma once
#include "Framework.hpp"

namespace xSE
{
	class VectoredExceptionHandler final
	{
		public:
			enum class Mode
			{
				ContinueHandler,
				ExceptionHandler
			};
			enum class Order
			{
				First,
				Last
			};
			enum class Result: uint32_t
			{
				ContinueSearch,
				ContinueExecution
			};

		private:
			Mode m_Mode = Mode::ContinueHandler;
			void* m_Handle = nullptr;

		public:
			VectoredExceptionHandler() noexcept = default;
			VectoredExceptionHandler(const VectoredExceptionHandler&) = delete;
			~VectoredExceptionHandler()
			{
				Remove();
			}

		public:
			bool IsInstalled() const noexcept
			{
				return m_Handle != nullptr;
			}
			Mode GetMode() const noexcept
			{
				return m_Mode;
			}

			bool Install(LONG(NTAPI *func)(_EXCEPTION_POINTERS*), Mode mode, Order order) noexcept;
			bool Remove() noexcept;

		public:
			VectoredExceptionHandler& operator=(const VectoredExceptionHandler&) = delete;
	};
}
