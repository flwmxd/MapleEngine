//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#pragma once
#include "Engine/Core.h"
#include "MonoComponent.h"
#include "Scene/System/ExecutePoint.h"

#include <ecs/ecs.h>
#include <memory>
#include <unordered_map>

namespace maple
{
	struct MonoScriptInstance;
	static constexpr uint32_t SCRIPT_NOT_LOADED = 0;

	namespace component
	{
		struct MonoEnvironment
		{
			std::unordered_map<uint32_t, std::shared_ptr<MonoScriptInstance>> scripts;
			uint32_t                                                          scriptId         = SCRIPT_NOT_LOADED;
			bool                                                              assemblyCompiled = false;
		};

		struct RecompileEvent
		{
		};
	}        // namespace component

	namespace mono
	{
		using MonoQuery = ecs::Registry ::Modify<component::MonoComponent>::To<ecs::Group>;

		auto callMonoStart(MonoQuery query) -> void;

		auto recompile(MonoQuery query) -> void;

		auto registerMonoModule(std::shared_ptr<ExecutePoint> executePoint) -> void;
	};        // namespace mono
};            // namespace maple