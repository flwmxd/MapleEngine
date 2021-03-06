//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Engine/Core.h"
#include "Scene/System/ExecutePoint.h"

namespace maple
{
	namespace ddgi
	{
		auto registerDDGI(ExecuteQueue &begin, std::shared_ptr<ExecutePoint> point) -> void;
	}
}        // namespace maple