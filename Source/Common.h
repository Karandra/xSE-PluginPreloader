#pragma once
#include "Framework.hpp"

namespace xSE::LogCategory
{
	KX_DefineLogCategory(xSE);
	KX_DefineLogCategory(ScriptExtender);

	KX_DefineLogCategory(Environment);
	KX_DefineLogCategory(ImportAddressHook);
	KX_DefineLogCategory(CurrentModule);
	KX_DefineLogCategory(HostProcess);
}
