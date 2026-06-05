#pragma once
#include "Framework.hpp"

namespace xSE::LogCategory
{
	kxf_DefineLogCategory(xSE);
	kxf_DefineLogCategory(ScriptExtender);

	kxf_DefineLogCategory(Environment);
	kxf_DefineLogCategory(ImportAddressHook);
	kxf_DefineLogCategory(CurrentModule);
	kxf_DefineLogCategory(HostProcess);
}
