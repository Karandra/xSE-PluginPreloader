#pragma once

// KxFramework
#include <kxf/Common.hpp>

#include <kxf/General/Common.h>
#include <kxf/General/String.h>
#include <kxf/General/Format.h>
#include <kxf/General/Version.h>
#include <kxf/General/DateTime.h>

#include <kxf/FileSystem/Common.h>
#include <kxf/FileSystem/FileItem.h>
#include <kxf/FileSystem/FSPath.h>
#include <kxf/FileSystem/IFileSystem.h>

namespace std
{
	// Enumerations
	template<class TPointer, class TChar> requires(std::is_pointer_v<TPointer>)
	struct formatter<TPointer, TChar>: formatter<size_t, TChar>
	{
		template<class TFormatContext>
		auto format(TPointer value, TFormatContext& formatContext)
		{
			return formatter<size_t, TChar>::format(reinterpret_cast<size_t>(value), formatContext);
		}
	};
}
