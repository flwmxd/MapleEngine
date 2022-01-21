//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Component.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace maple
{
	class VolumetricCloud : public Component
	{
	  public:
		constexpr static char *ICON = ICON_MDI_WEATHER_CLOUDY;
		
		float cloudSpeed = 450.0;
		float coverage   = 0.45;
		float crispiness = 40.;
		float curliness  = .1;
		float density    = 0.02;
		float absorption = 0.35;

		float earthRadius  = 600000.0;
		float sphereInnerRadius = 5000.0;
		float sphereOuterRadius = 17000.0;

		float perlinFrequency = 0.8;

		bool  enableGodRays;
		bool  enablePowder;
		bool  postProcess;


		std::shared_ptr<Texture> perlinNoise;
		std::shared_ptr<Texture> worley32;

		bool weathDirty = true;
	};
};        // namespace maple
