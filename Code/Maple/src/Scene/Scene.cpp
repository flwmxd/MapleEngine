//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "Scene.h"
#include "Entity/Entity.h"
#include "Scene/Component/CameraControllerComponent.h"
#include "Scene/Component/Light.h"
#include "Scene/Component/MeshRenderer.h"
#include "Scene/SystemBuilder.inl"
#include "Scene/Component/Transform.h"
#include "Scene/Component/VolumetricCloud.h"
#include "Scene/Component/BoundingBox.h"
#include "Scene/Component/LightProbe.h"
#include "Scene/System/ExecutePoint.h"

#include "2d/Sprite.h"
#include "Scripts/Mono/MonoSystem.h"
#include "Scripts/Mono/MonoComponent.h"

#include "FileSystem/Skeleton.h"

#include "SceneGraph.h"

#include "Devices/Input.h"
#include "Engine/Camera.h"
#include "Engine/CameraController.h"
#include "Engine/Material.h"
#include "Engine/Profiler.h"
#include "Engine/Mesh.h"

#include "Others/Serialization.h"
#include "Others/StringUtils.h"

#include "Others/Console.h"
#include "Scripts/Mono/MonoSystem.h"
#include <filesystem>
#include <fstream>

#include <ecs/ecs.h>

#include "Application.h"

namespace maple
{
	namespace
	{
		inline auto addEntity(Scene* scene, Entity parent, Skeleton* skeleton, int32_t idx, std::vector<Entity> & outEntities) -> Entity
		{
			auto& bone = skeleton->getBone(idx);
			auto entity = scene->createEntity(bone.name);
			auto & transform = entity.addComponent<component::Transform>();
			auto& boneComp = entity.addComponent<component::BoneComponent>();

			transform.setOffsetTransform(bone.offsetMatrix);
			transform.setLocalTransform(bone.localTransform);
			boneComp.boneIndex = idx;
			boneComp.skeleton = skeleton;

			entity.setParent(parent);

			for (auto child : bone.children)
			{
				addEntity(scene, entity, skeleton, child,outEntities);
			}
			outEntities.emplace_back(entity);
			return entity;
		}

	}

	Scene::Scene(const std::string &initName) :
	    name(initName)
	{
		sceneGraph = std::make_shared<SceneGraph>();
		sceneGraph->init(Application::getExecutePoint()->getRegistry());
	}

	auto Scene::setSize(uint32_t w, uint32_t h) -> void
	{
		width  = w;
		height = h;
	}

	auto Scene::saveTo(const std::string &path, bool binary) -> void
	{
		PROFILE_FUNCTION();
		if (dirty)
		{
			LOGV("save to disk");
			if (path != "" && path != filePath)
			{
				filePath = path + StringUtils::delimiter + name + ".scene";
			}
			if (filePath == "")
			{
				filePath = name + ".scene";
			}
			Serialization::serialize(this);
			dirty = false;
		}
	}

	auto Scene::loadFrom() -> void
	{
		PROFILE_FUNCTION();
		if (filePath != "")
		{
			Application::getExecutePoint()->clear();
			sceneGraph->disconnectOnConstruct(true, Application::getExecutePoint()->getRegistry());
			Serialization::loadScene(this, filePath);
			sceneGraph->disconnectOnConstruct(false, Application::getExecutePoint()->getRegistry());
		}
	}

	auto Scene::createEntity() -> Entity
	{
		PROFILE_FUNCTION();
		dirty       = true;
		auto entity = Application::getExecutePoint()->create();
		if (onEntityAdd)
			onEntityAdd(entity);
		return entity;
	}

	auto Scene::createEntity(const std::string &name) -> Entity
	{
		PROFILE_FUNCTION();
		dirty          = true;
		int32_t i      = 0;
		auto    entity = Application::getExecutePoint()->getEntityByName(name);
		while (entity.valid())
		{
			entity = Application::getExecutePoint()->getEntityByName(name + "(" + std::to_string(i + 1) + ")");
			i++;
		}
		auto newEntity = Application::getExecutePoint()->create(i == 0 ? name : name + "(" + std::to_string(i) + ")");
		if (onEntityAdd)
			onEntityAdd(newEntity);
		return newEntity;
	}

	auto Scene::duplicateEntity(const Entity &entity, const Entity &parent) -> void
	{
		PROFILE_FUNCTION();
		dirty = true;

		Entity newEntity = Application::getExecutePoint()->create();

		if (parent)
			newEntity.setParent(parent);

		copyComponents(entity, newEntity);
	}

	auto Scene::duplicateEntity(const Entity &entity) -> void
	{
		PROFILE_FUNCTION();

		dirty            = true;
		Entity newEntity = Application::getExecutePoint()->create();
		//COPY
		copyComponents(entity, newEntity);
	}

	auto Scene::getCamera() -> std::pair<Camera *, component::Transform *>
	{
		PROFILE_FUNCTION();

		using CameraQuery = ecs::Chain
			::Write<Camera>
			::Write<component::Transform>
			::To<ecs::Query>;

		CameraQuery query{ 
			Application::getExecutePoint()->getRegistry(), 
			Application::getExecutePoint()->getGlobalEntity()
		};

		if (useSceneCamera)
		{
			for (auto entity : query)
			{
				auto & [sceneCam, sceneCamTr] = query.convert(entity);
				return { &sceneCam, &sceneCamTr };
			}
		}
		return {overrideCamera, overrideTransform};
	}

	auto Scene::removeAllChildren(entt::entity entity) -> void
	{
		PROFILE_FUNCTION();
		Application::getExecutePoint()->removeAllChildren(entity);
	}

	auto Scene::calculateBoundingBox() -> void
	{
		PROFILE_FUNCTION();
		boxDirty = false;

		sceneBox.clear();
		
		using Query = ecs::Chain
			::Write<component::MeshRenderer>
			::To<ecs::Query>;

		Query query(Application::getExecutePoint()->getRegistry(), Application::getExecutePoint()->getGlobalEntity());

		for (auto entity : query)
		{
			auto [meshRender] = query.convert(entity);
			if (auto mesh = meshRender.getMesh())
			{
				if (mesh->isActive())
					sceneBox.merge(mesh->getBoundingBox());
			}
		}
		auto & aabb = Application::getExecutePoint()->getGlobalComponent<component::BoundingBoxComponent>();
		aabb.box = &sceneBox;
	}

	auto Scene::onMeshRenderCreated() -> void
	{
		boxDirty = true;
	}

	auto Scene::addMesh(const std::string& file) -> Entity
	{
		PROFILE_FUNCTION();
		auto  name = StringUtils::getFileNameWithoutExtension(file);
		auto  modelEntity = createEntity(name);
		auto& model = modelEntity.addComponent<component::Model>(file);

		MAPLE_ASSERT(model.resource != nullptr, "load resource fail");

		if (model.resource->getMeshes().size() == 1 && model.skeleton == nullptr)
		{
			modelEntity.addComponent<component::MeshRenderer>(model.resource->getMeshes().begin()->second);
		}
		else
		{
			if (model.skeleton)
			{
				std::vector<Entity> outEntities;
				model.skeleton->buildRoot();
				auto rootEntity = addEntity(this, modelEntity, model.skeleton.get(), model.skeleton->getRoot(), outEntities);

				if (model.skeleton->isBuildOffset())
				{
					sceneGraph->updateTransform(modelEntity, Application::getExecutePoint()->getRegistry());
					for (auto entity : outEntities)
					{
						auto& transform = entity.getComponent<component::Transform>();
						transform.setOffsetTransform(transform.getWorldMatrixInverse());
					}
				}
			}

			for (auto& mesh : model.resource->getMeshes())
			{
				auto child = createEntity(mesh.first);
				if (model.skeleton)
				{
					auto & meshRenderer = child.addComponent<component::SkinnedMeshRenderer>(mesh.second);
				}
				else
				{
					child.addComponent<component::MeshRenderer>(mesh.second);
				}
				child.setParent(modelEntity);
			}
		}
		model.type = component::PrimitiveType::File;
		return modelEntity;
	}

	auto Scene::copyComponents(const Entity& from, const Entity& to) -> void
	{
		LOGW("Not implementation {0}", __FUNCTION__);
	}

	auto Scene::onInit() -> void
	{
		PROFILE_FUNCTION();
		if (initCallback != nullptr)
		{
			initCallback(this);
		}
		using MonoQuery = ecs::Chain
			::Write<component::MonoComponent>
			::To<ecs::Query>;

		MonoQuery query{ Application::getExecutePoint()->getRegistry() ,Application::getExecutePoint()->getGlobalEntity() };
		mono::recompile(query);
		mono::callMonoStart(query);

	}

	auto Scene::onClean() -> void
	{
	}
	
	using ControllerQuery = ecs::Chain
		::Write<component::CameraControllerComponent>
		::Write<component::Transform>
		::To<ecs::Query>;

	auto Scene::updateCameraController(float dt) -> void
	{
		PROFILE_FUNCTION();

		ControllerQuery query(Application::getExecutePoint()->getRegistry(), Application::getExecutePoint()->getGlobalEntity());

		for (auto entity : query)
		{
			auto [con, trans] = query.convert(entity);
			const auto mousePos = Input::getInput()->getMousePosition();
			if (Application::get()->isSceneActive() &&
				Application::get()->getEditorState() == EditorState::Play &&
				con.getController())
			{
				con.getController()->handleMouse(trans, dt, mousePos.x, mousePos.y);
				con.getController()->handleKeyboard(trans, dt);
			}
		}
	}

	auto Scene::onUpdate(float dt) -> void
	{
		PROFILE_FUNCTION();
		auto& deltaTime = Application::getExecutePoint()->getGlobalComponent<component::DeltaTime>();
		deltaTime.dt = dt;
		updateCameraController(dt);
		getBoundingBox();
		sceneGraph->update(Application::getExecutePoint()->getRegistry());
	}

	auto Scene::create() -> Entity
	{
		auto& registry = Application::getExecutePoint()->getRegistry();

		return Entity(registry.create(), registry);
	}

	auto Scene::create(const std::string& name) -> Entity
	{
		auto& registry = Application::getExecutePoint()->getRegistry();
		auto e = registry.create();
		registry.emplace<component::NameComponent>(e, name);
		return Entity(e, registry);
	}
};        // namespace maple
