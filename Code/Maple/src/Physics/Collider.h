//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "Math/BoundingBox.h"
#include <glm/glm.hpp>

class btCollisionShape;

namespace maple
{
	namespace physics
	{
		enum class ColliderType
		{
			BoxCollider,
			SphereCollider,
			CapsuleCollider,
			CycleCollider,
			PolygonCollider
		};

		namespace component
		{
			struct Collider
			{
				ColliderType      type;
				btCollisionShape *shape = nullptr;
				BoundingBox       box;
				float             radius;
				float             height;

				BoundingBox originalBox;
			};
		}        // namespace component

		inline auto getNameByType(ColliderType type)
		{
			switch (type)
			{
#define STR(r) \
	case r:    \
		return #r
				STR(ColliderType::BoxCollider);
				STR(ColliderType::SphereCollider);
				STR(ColliderType::CapsuleCollider);
				STR(ColliderType::CycleCollider);
				STR(ColliderType::PolygonCollider);
#undef STR
				default:
					return "UNKNOWN_ERROR";
			}
		}
	}        // namespace physics
}        // namespace maple