//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "PathIntegrator.h"
#include "Engine/CaptureGraph.h"
#include "Engine/Mesh.h"
#include "Engine/Renderer/RendererData.h"

#include "RHI/AccelerationStructure.h"
#include "RHI/DescriptorPool.h"
#include "RHI/GraphicsContext.h"
#include "RHI/Pipeline.h"
#include "RHI/StorageBuffer.h"

#include "Scene/Component/Bindless.h"
#include "Scene/Component/Light.h"
#include "Scene/Component/MeshRenderer.h"
#include "Scene/Component/Transform.h"

#include "Engine/Renderer/SkyboxRenderer.h"

#include "TracedData.h"

#include <scope_guard.hpp>

#ifdef MAPLE_VULKAN
#	include "RHI/Vulkan/Vk.h"
#endif        // MAPLE_VULKAN

namespace maple
{
	namespace
	{
		constexpr uint32_t MAX_SCENE_MESH_INSTANCE_COUNT    = 1024;
		constexpr uint32_t MAX_SCENE_LIGHT_COUNT            = 300;
		constexpr uint32_t MAX_SCENE_MATERIAL_COUNT         = 4096;
		constexpr uint32_t MAX_SCENE_MATERIAL_TEXTURE_COUNT = MAX_SCENE_MATERIAL_COUNT * 4;
	}        // namespace

	namespace component
	{
		struct PathTracePipeline
		{
			Pipeline::Ptr pipeline;
			Shader::Ptr   shader;

			StorageBuffer::Ptr  lightBuffer;
			StorageBuffer::Ptr  materialBuffer;
			StorageBuffer::Ptr  transformBuffer;
			DescriptorPool::Ptr descriptorPool;

			DescriptorSet::Ptr sceneDescriptor;
			DescriptorSet::Ptr vboDescriptor;
			DescriptorSet::Ptr iboDescriptor;
			DescriptorSet::Ptr materialDescriptor;
			DescriptorSet::Ptr textureDescriptor;

			DescriptorSet::Ptr readDescriptor;
			DescriptorSet::Ptr writeDescriptor;

			AccelerationStructure::Ptr tlas;

			struct PushConstants
			{
				glm::mat4 invViewProj;
				glm::vec4 cameraPos;
				glm::vec4 upDirection;
				glm::vec4 rightDirection;
				uint32_t  numFrames;
				uint32_t  maxBounces;
				uint32_t  numLights;
				float     accumulation;
				float     shadowRayBias;
				float     padding0;
				float     padding1;
				float     padding2;
			} constants;
		};
	}        // namespace component

	namespace init
	{
		inline auto initPathIntegrator(component::PathIntegrator &path, Entity entity, ecs::World world)
		{
			auto &pipeline  = entity.addComponent<component::PathTracePipeline>();
			pipeline.shader = Shader::create("shaders/PathTrace/PathTrace.shader", {{"Vertices", MAX_SCENE_MESH_INSTANCE_COUNT},
			                                                                        {"Indices", MAX_SCENE_MESH_INSTANCE_COUNT},
			                                                                        {"SubmeshInfo", MAX_SCENE_MESH_INSTANCE_COUNT},
			                                                                        {"uSamplers", MAX_SCENE_MATERIAL_TEXTURE_COUNT}});
			PipelineInfo info;
			info.shader = pipeline.shader;

			pipeline.pipeline        = Pipeline::get(info);
			pipeline.lightBuffer     = StorageBuffer::create();
			pipeline.materialBuffer  = StorageBuffer::create();
			pipeline.transformBuffer = StorageBuffer::create();

			pipeline.lightBuffer->setData(sizeof(component::LightData) * MAX_SCENE_LIGHT_COUNT, nullptr);
			pipeline.materialBuffer->setData(sizeof(raytracing::MaterialData) * MAX_SCENE_MATERIAL_COUNT, nullptr);
			pipeline.materialBuffer->setData(sizeof(raytracing::TransformData) * MAX_SCENE_MESH_INSTANCE_COUNT, nullptr);

			pipeline.descriptorPool = DescriptorPool::create({25,
			                                                  {{DescriptorType::UniformBufferDynamic, 10},
			                                                   {DescriptorType::ImageSampler, MAX_SCENE_MATERIAL_TEXTURE_COUNT},
			                                                   {DescriptorType::Buffer, 5 * MAX_SCENE_MESH_INSTANCE_COUNT},
			                                                   {DescriptorType::AccelerationStructure, 10}}});

			pipeline.sceneDescriptor    = DescriptorSet::create({0, pipeline.shader.get(), 1, pipeline.descriptorPool.get()});
			pipeline.vboDescriptor      = DescriptorSet::create({1, pipeline.shader.get(), 1, pipeline.descriptorPool.get(), MAX_SCENE_MESH_INSTANCE_COUNT});
			pipeline.iboDescriptor      = DescriptorSet::create({2, pipeline.shader.get(), 1, pipeline.descriptorPool.get(), MAX_SCENE_MESH_INSTANCE_COUNT});
			pipeline.materialDescriptor = DescriptorSet::create({3, pipeline.shader.get(), 1, pipeline.descriptorPool.get(), MAX_SCENE_MATERIAL_COUNT});
			pipeline.textureDescriptor  = DescriptorSet::create({4, pipeline.shader.get(), 1, pipeline.descriptorPool.get(), MAX_SCENE_MATERIAL_TEXTURE_COUNT});
			pipeline.readDescriptor     = DescriptorSet::create({5, pipeline.shader.get()});
			pipeline.writeDescriptor    = DescriptorSet::create({6, pipeline.shader.get()});

			pipeline.tlas = AccelerationStructure::createTopLevel(MAX_SCENE_MESH_INSTANCE_COUNT);

			auto & winSize = world.getComponent<component::WindowSize>();

			for (auto i = 0;i<2;i++)
			{
				path.images[i] = Texture2D::create();
				path.images[i]->buildTexture(TextureFormat::RGBA32, winSize.width, winSize.height);
			}
		}
	}        // namespace init

	namespace gather_scene
	{
		using Entity = ecs::Registry::Fetch<component::PathIntegrator>::Modify<component::PathTracePipeline>::To<ecs::Entity>;

		using LightDefine = ecs::Registry ::Fetch<component::Transform>::Modify<component::Light>;

		using LightEntity = LightDefine ::To<ecs::Entity>;

		using LightGroup = LightDefine::To<ecs::Group>;

		using MeshQuery = ecs::Registry ::Modify<component::MeshRenderer>::Modify<component::Transform>::To<ecs::Group>;

		using SkyboxGroup = ecs::Registry::Fetch<component::SkyboxData>::To<ecs::Group>;

		inline auto system(
		    Entity                                    entity,
		    LightGroup                                lightGroup,
		    MeshQuery                                 meshGroup,
		    SkyboxGroup                               skyboxGroup,
		    const maple::component::RendererData &    rendererData,
		    global::component::Bindless &             bindless,
		    global::component::GraphicsContext &      context,
		    global::component::SceneTransformChanged *sceneChanged)
		{
			auto [integrator, pipeline] = entity;
			if (sceneChanged && sceneChanged->dirty)
			{
				std::unordered_set<uint32_t>           processedMeshes;
				std::unordered_set<uint32_t>           processedMaterials;
				std::unordered_set<uint32_t>           processedTextures;
				std::unordered_map<uint32_t, uint32_t> globalMaterialIndices;
				uint32_t                               meshCounter        = 0;
				uint32_t                               gpuMaterialCounter = 0;

				std::vector<VertexBuffer::Ptr>  vbos;
				std::vector<IndexBuffer::Ptr>   ibos;
				std::vector<Texture::Ptr>       shaderTextures;
				std::vector<StorageBuffer::Ptr> materialIndices;

				context.context->waitIdle();

				auto meterialBuffer  = (raytracing::MaterialData *) pipeline.materialBuffer->map();
				auto transformBuffer = (raytracing::TransformData *) pipeline.transformBuffer->map();
				auto lightBuffer     = (component::LightData *) pipeline.lightBuffer->map();

				auto guard = sg::make_scope_guard([&]() {
					pipeline.materialBuffer->unmap();
					pipeline.lightBuffer->unmap();
					pipeline.transformBuffer->unmap();
				});

				auto tasks = BatchTask::create();

				auto instanceBuffer = pipeline.tlas->mapHost();        //Copy to CPU side.

				uint32_t meshCount = 0;
				for (auto meshEntity : meshGroup)
				{
					auto [mesh, transform] = meshGroup.convert(meshEntity);

					if (processedMeshes.count(mesh.mesh->getId()) == 0)
					{
						processedMeshes.emplace(mesh.mesh->getId());
						bindless.meshIndices[mesh.mesh->getId()] = meshCounter++;
						vbos.emplace_back(mesh.mesh->getVertexBuffer());
						ibos.emplace_back(mesh.mesh->getIndexBuffer());

						for (auto i = 0; i < mesh.mesh->getSubMeshCount(); i++)
						{
							auto material = mesh.mesh->getMaterial(i);

							if (processedMaterials.count(material->getId()) == 0)
							{
								processedMaterials.emplace(material->getId());
								auto &materialData           = meterialBuffer[gpuMaterialCounter++];
								materialData.textureIndices0 = glm::ivec4(-1);
								materialData.textureIndices1 = glm::ivec4(-1);

								materialData.albedo     = material->getProperties().albedoColor;
								materialData.roughness  = material->getProperties().roughnessColor;
								materialData.metalic    = material->getProperties().metallicColor;
								materialData.emissive   = material->getProperties().emissiveColor;
								materialData.emissive.a = material->getProperties().workflow;

								auto textures = material->getMaterialTextures();

								if (textures.albedo)
								{
									if (processedTextures.count(textures.albedo->getId()) == 0)
									{
										materialData.textureIndices0.x = shaderTextures.size();
										shaderTextures.emplace_back(textures.albedo);
									}
								}

								if (textures.normal)
								{
									if (processedTextures.count(textures.normal->getId()) == 0)
									{
										materialData.textureIndices0.y = shaderTextures.size();
										shaderTextures.emplace_back(textures.normal);
									}
								}

								if (textures.roughness)
								{
									if (processedTextures.count(textures.roughness->getId()) == 0)
									{
										materialData.textureIndices0.z = shaderTextures.size();
										shaderTextures.emplace_back(textures.roughness);
									}
								}

								if (textures.metallic)
								{
									if (processedTextures.count(textures.metallic->getId()) == 0)
									{
										materialData.textureIndices0.w = shaderTextures.size();
										shaderTextures.emplace_back(textures.metallic);
									}
								}

								if (textures.emissive)
								{
									if (processedTextures.count(textures.emissive->getId()) == 0)
									{
										materialData.textureIndices1.x = shaderTextures.size();
										shaderTextures.emplace_back(textures.emissive);
									}
								}

								if (textures.ao)
								{
									if (processedTextures.count(textures.ao->getId()) == 0)
									{
										materialData.textureIndices1.y = shaderTextures.size();
										shaderTextures.emplace_back(textures.ao);
									}
								}

								bindless.materialIndices[material->getId()] = gpuMaterialCounter - 1;
								//TODO if current is emissive, add an extra light here.
							}
						}
					}

					auto buffer = mesh.mesh->getSubMeshesBuffer();

					auto indices = (glm::uvec2 *) buffer->map();
					materialIndices.emplace_back(buffer);

					for (auto i = 0; i < mesh.mesh->getSubMeshCount(); i++)
					{
						auto        material = mesh.mesh->getMaterial(i);
						const auto &subIndex = mesh.mesh->getSubMeshIndex();
						indices[i]           = glm::uvec2(subIndex[i] / 3, bindless.materialIndices[material->getId()]);
					}

					auto blas = mesh.mesh->getAccelerationStructure(tasks);

					transformBuffer[meshCount].meshIndex    = bindless.meshIndices[mesh.mesh->getId()];
					transformBuffer[meshCount].model        = transform.getWorldMatrix();
					transformBuffer[meshCount].normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform.getWorldMatrix())));

					pipeline.tlas->updateTLAS(instanceBuffer, glm::mat3x4(glm::transpose(transform.getWorldMatrix())), meshCount++, blas->getDeviceAddress());

					auto guard = sg::make_scope_guard([&]() {
						buffer->unmap();
					});
				}

				pipeline.tlas->unmap();

				uint32_t lightIndicator = 0;

				for (auto lightEntity : lightGroup)
				{
					auto [transform, light]       = lightGroup.convert(lightEntity);
					light.lightData.position      = {transform.getWorldPosition(), 1.f};
					light.lightData.direction     = {glm::normalize(transform.getWorldOrientation() * maple::FORWARD), 1.f};
					lightBuffer[lightIndicator++] = light.lightData;
				}

				pipeline.constants.numLights  = lightIndicator;
				pipeline.constants.maxBounces = integrator.maxBounces;

				tasks->execute();

				pipeline.sceneDescriptor->setTexture("uSkyBox", rendererData.unitCube);
				if (!skyboxGroup.empty())
				{
					for (auto sky : skyboxGroup)
					{
						auto [skybox] = skyboxGroup.convert(sky);
						pipeline.sceneDescriptor->setTexture("uSkyBox", skybox.skybox);
					}
				}

				pipeline.sceneDescriptor->setStorageBuffer("MaterialBuffer", pipeline.materialBuffer);
				pipeline.sceneDescriptor->setStorageBuffer("TransformBuffer", pipeline.transformBuffer);
				pipeline.sceneDescriptor->setStorageBuffer("LightBuffer", pipeline.lightBuffer);
				pipeline.sceneDescriptor->setAccelerationStructure("uTopLevelAS", pipeline.tlas);

				pipeline.vboDescriptor->setStorageBuffer("VertexBuffer", vbos);
				pipeline.vboDescriptor->setStorageBuffer("IndexBuffer", ibos);

				pipeline.materialDescriptor->setStorageBuffer("SubmeshInfoBuffer", materialIndices);
				pipeline.textureDescriptor->setTexture("uSamplers", shaderTextures);
			}
		}
	}        // namespace gather_scene

	namespace tracing
	{
		using Entity = ecs::Registry::Modify<component::PathIntegrator>::Modify<component::PathTracePipeline>::To<ecs::Entity>;

		inline auto system(Entity                                entity,
		                   const maple::component::RendererData &rendererData,
		                   maple::component::CameraView &        cameraView,
		                   maple::component::WindowSize &        winSize,
		                   ecs::World                            world)
		{
			auto [integrator, pipeline] = entity;

			pipeline.readDescriptor->setTexture("uPreviousColor", integrator.images[integrator.readIndex]);
			pipeline.readDescriptor->setTexture("uCurrentColor", integrator.images[1 - integrator.readIndex]);

			pipeline.pipeline->bind(rendererData.commandBuffer);

			glm::vec3 right   = cameraView.cameraTransform->getRightDirection();
			glm::vec3 up      = cameraView.cameraTransform->getUpDirection();
			glm::vec3 forward = cameraView.cameraTransform->getForwardDirection();

			pipeline.constants.cameraPos      = glm::vec4{cameraView.cameraTransform->getWorldPosition(), 0};
			pipeline.constants.upDirection    = glm::vec4(up, 0.0f);
			pipeline.constants.rightDirection = glm::vec4(right, 0.0f);
			pipeline.constants.invViewProj    = glm::inverse(cameraView.projView);
			pipeline.constants.numFrames      = integrator.accumulatedSamples++;
			pipeline.constants.accumulation   = float(pipeline.constants.numFrames) / float(pipeline.constants.numFrames + 1);
			pipeline.constants.shadowRayBias  = integrator.shadowRayBias;
			for (auto &pushConsts : pipeline.pipeline->getShader()->getPushConstants())
			{
				pushConsts.setData(&pipeline.constants);
			}
			pipeline.pipeline->getShader()->bindPushConstants(rendererData.commandBuffer, pipeline.pipeline.get());

			Renderer::bindDescriptorSets(pipeline.pipeline.get(), rendererData.commandBuffer, 0,
			                             {
			                                 pipeline.sceneDescriptor,
			                                 pipeline.vboDescriptor,
			                                 pipeline.iboDescriptor,
			                                 pipeline.materialDescriptor,
			                                 pipeline.textureDescriptor,
			                                 pipeline.readDescriptor,
			                                 pipeline.writeDescriptor,
			                             });

			pipeline.pipeline->traceRays(rendererData.commandBuffer, winSize.width, winSize.height, 1);
		}
	}        // namespace tracing

	namespace path_integrator
	{
		auto registerPathIntegrator(ExecuteQueue &begin, ExecuteQueue &renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->onConstruct<component::PathIntegrator, init::initPathIntegrator>();
			executePoint->registerWithinQueue<gather_scene::system>(begin);
			executePoint->registerWithinQueue<tracing::system>(renderer);
		}
	}        // namespace path_integrator
}        // namespace maple
