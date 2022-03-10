//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Component.h"
#include "Engine/Material.h"
#include "Resources/MeshResource.h"
#include <cereal/types/memory.hpp>
#include <memory>
#include <vector>

namespace maple
{
	class Mesh;
	class MeshResource;
	namespace component
	{
		enum class PrimitiveType : int32_t
		{
			Plane = 0,
			Quad = 1,
			Cube = 2,
			Pyramid = 3,
			Sphere = 4,
			Capsule = 5,
			Cylinder = 6,
			Terrain = 7,
			File = 8,
			Length
		};

		class MAPLE_EXPORT Model final : public Component
		{
		  public:
			Model() = default;
			Model(const std::string& file);

			template <class Archive>
			auto save(Archive& archive) const -> void
			{
				archive(filePath, type, entity);
			}

			template <class Archive>
			auto load(Archive& archive) -> void
			{
				archive(filePath, type, entity);
				load();
			}

			std::string                   filePath;
			PrimitiveType                 type = PrimitiveType::Length;
			std::shared_ptr<MeshResource> resource;

		  private:
			auto load() -> void;
		};

		class MAPLE_EXPORT MeshRenderer : public Component
		{
		public:
			constexpr static char* ICON = ICON_MDI_SHAPE;

			MeshRenderer() = default;
			MeshRenderer(const std::shared_ptr<Mesh>& mesh);

			template <class Archive>
			inline auto save(Archive& archive) const -> void
			{
				archive(mesh->getName(), entity, cereal::make_nvp("material", mesh->getMaterial()));
			}

			template <class Archive>
			inline auto load(Archive& archive) -> void
			{
				archive(meshName, entity, cereal::make_nvp("material", material));
			}

			auto loadFromModel() -> void;

			inline auto& getMesh()
			{
				if (mesh == nullptr)
					getMesh(meshName);
				return mesh;
			}

			auto isActive() const -> bool;

		private:
			std::shared_ptr<Mesh>     mesh;
			auto                      getMesh(const std::string& name) -> void;
			std::string               meshName;
			std::shared_ptr<Material> material;
		};
	}
};        // namespace maple
