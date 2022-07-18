/*
* Vulkan Example - Indirect drawing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Use a device local buffer that stores draw commands for instanced rendering of different meshes stored
* in the same buffer.
*
* Indirect drawing offloads draw command generation and offers the ability to update them on the GPU
* without the CPU having to touch the buffer again, also reducing the number of drawcalls.
*
* The example shows how to setup and fill such a buffer on the CPU side, stages it to the device and
* shows how to render it using only one draw command.
*
* See readme.md for details
*
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include "frustum.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1

#if defined(__ANDROID__)
	#define ENABLE_VALIDATION false
#else
	#define ENABLE_VALIDATION true
#endif

// Number of instances per object
//#if defined(__ANDROID__)
//#define OBJECT_INSTANCE_COUNT 1024
//// Circular range of plant distribution
//#define PLANT_RADIUS 2.0f
//#define PRIMITIVE_COUNT 256
//#else
//#define OBJECT_INSTANCE_COUNT 2048
//#define PRIMITIVE_COUNT 256
//// Circular range of plant distribution
//#define PLANT_RADIUS 2.0f
//#endif

// Circular range of plant distribution
#define PLANT_RADIUS 2.0f

static const uint32_t INSTANCE_PER_PRIM_PER_MESH = 32;
static const uint32_t PRIMITIVE_COUNT = 32;
static const uint32_t PRIMITIVE_COUNT_BORDER = 4;
static const uint32_t OBJECT_INSTANCE_COUNT = INSTANCE_PER_PRIM_PER_MESH * PRIMITIVE_COUNT;
static const float PRIM_GAP = 5.0f;
static const float CULL_DISTANCE = 30.0f;

enum EAttrLocation : uint32_t
{
	Pos,
	Normal,
	UV,
	Color,
	instanceTransformRow0,
	instanceTransformRow1,
	instanceTransformRow2,
	instanceTransformRow3,
    primitiveIndex,
    pad0,
    pad1,
    pad2,
};

enum ERenderBinding: uint32_t
{
	Scene,
	PlantTextureArray,
	Texture,
	Primitives,
	Materials
};

enum class EComputeBinding : uint32_t
{
	Instances,
	OutDrawCommands,
	Scene,
	Primitives
};

struct GamePrimitiveInstance
{
	glm::mat4 transform;
};

struct GamePrimitive
{
	glm::mat4 transform;
	int32_t meshIndex;
	int32_t materialIndex;

	std::vector<GamePrimitiveInstance> instances;
};

struct GameScene
{
	std::vector<GamePrimitive> primitives;
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		vks::Texture2DArray plants;
		vks::Texture2D ground;
	} textures;

	struct {
		vkglTF::Model plants;
		vkglTF::Model ground;
		vkglTF::Model skysphere;
	} models;

	// Per-instance data block
	struct RenderInstanceData {
		glm::vec4 transRow0;
		glm::vec4 transRow1;
		glm::vec4 transRow2;
		glm::vec4 transRow3;
        int primIndex;
		int _pad0;
		int _pad1;
		int _pad2;
	};

	struct Material {
		glm::vec4 tint;
		uint32_t textureIndex;
		uint32_t padding0 = 0;
		uint32_t padding1 = 0;
		uint32_t padding2 = 0;
	};


	// Contains the instanced data
	vks::Buffer instanceBuffer;
	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	uint32_t indirectDrawCount;
	uint32_t plantTypeCount;

	struct {
		glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 cameraPos;
        glm::vec4 frustumPlanes[6];
	} renderScene;

	struct RenderPrimitiveData {
		glm::mat4 transform;
        float cullDistance;
        uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t materialIndex;
	};

	struct {
		vks::Buffer scene;
		vks::Buffer primitives;
		vks::Buffer materials;
	} uniformData;

	struct {
		VkPipeline plants;
		VkPipeline ground;
		VkPipeline skysphere;
	} pipelines;

    // View frustum for culling invisible objects
    vks::Frustum frustum;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkQueue computeQueue;
	VkPipelineLayout computePipelineLayout;
	VkDescriptorSet computeDescriptorSet;
	VkDescriptorSetLayout computeDescriptorSetLayout;
	VkPipeline computePipeline;
	VkCommandPool commputeCommandPool;
	VkCommandBuffer computeCommandBuffer;
	VkFence computeFence;
	VkSemaphore computeSemaphore;

	VkSampler samplerRepeat;

	uint32_t objectCount = 0;

	// Store the indirect draw commands containing index offsets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

	std::vector<Material> materials;

	GameScene gameScene;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Indirect rendering";
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-12.0f, 159.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.4f, 1.25f, 0.0f));
		camera.movementSpeed = 5.0f;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.plants, nullptr);
		vkDestroyPipeline(device, pipelines.ground, nullptr);
		vkDestroyPipeline(device, pipelines.skysphere, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		textures.plants.destroy();
		textures.ground.destroy();
		instanceBuffer.destroy();
		indirectCommandsBuffer.destroy();
		uniformData.scene.destroy();
		uniformData.primitives.destroy();
		uniformData.materials.destroy();

		vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
		vkDestroyPipeline(device, computePipeline, nullptr);
		vkDestroyCommandPool(device, commputeCommandPool, nullptr);
		vkDestroyFence(device, computeFence, nullptr);
		vkDestroySemaphore(device, computeSemaphore, nullptr);

        //VkDescriptorSet computeDescriptorSet;
        //VkCommandBuffer computeCommandBuffer;
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Example uses multi draw indirect if available
		if (deviceFeatures.multiDrawIndirect) {
			enabledFeatures.multiDrawIndirect = VK_TRUE;
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	void buildComputeCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VK_CHECK_RESULT(vkBeginCommandBuffer(computeCommandBuffer, &cmdBufInfo));

		{
			// acquire queue ownership of buffer
            VkBufferMemoryBarrier barrier = vks::initializers::bufferMemoryBarrier();
            barrier.buffer = indirectCommandsBuffer.buffer;
            barrier.size = indirectCommandsBuffer.descriptor.range;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
            barrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;

            vkCmdPipelineBarrier(computeCommandBuffer,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_FLAGS_NONE,
                                 // memory barrier
                                 0, nullptr,
                                 // buffer memory barrier
                                 1, &barrier,
                                 // image memory barrier
                                 0, nullptr);
		}
		

		vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
		vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
		vkCmdDispatch(computeCommandBuffer, objectCount / 16, 1, 1);

		{
			// release queue ownership of buffer
			VkBufferMemoryBarrier barrier = vks::initializers::bufferMemoryBarrier();
            barrier.buffer = indirectCommandsBuffer.buffer;
            barrier.size = indirectCommandsBuffer.descriptor.range;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = 0;
            barrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
            barrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;

            vkCmdPipelineBarrier(computeCommandBuffer,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                 VK_FLAGS_NONE,
                                 // memory barrier
                                 0, nullptr,
                                 // buffer memory barrier
                                 1, &barrier,
                                 // image memory barrier
                                 0, nullptr);
		}
		
		VK_CHECK_RESULT(vkEndCommandBuffer(computeCommandBuffer));
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.18f, 0.27f, 0.5f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			{
				// acquire queue ownership of the buffer
				VkBufferMemoryBarrier barrier = vks::initializers::bufferMemoryBarrier();
				barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                barrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
				barrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
				barrier.buffer = indirectCommandsBuffer.buffer;
				barrier.offset = 0;
				barrier.size = indirectCommandsBuffer.descriptor.range;

				vkCmdPipelineBarrier(drawCmdBuffers[i],
									 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
									 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
									 0,
									 0, nullptr,
									 1, &barrier,
									 0, nullptr);
			}

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			// Skysphere
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skysphere);
			models.skysphere.draw(drawCmdBuffers[i]);
			// Ground
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ground);
			models.ground.draw(drawCmdBuffers[i]);

			// [POI] Instanced multi draw rendering of the plants
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.plants);
			// Binding point 0 : Mesh vertex buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.plants.vertices.buffer, offsets);
			// Binding point 1 : Instance data buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.plants.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// If the multi draw feature is supported:
			// One draw call for an arbitrary number of objects
			// Index offsets and instance count are taken from the indirect buffer
			if (vulkanDevice->features.multiDrawIndirect)
			{
				vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, 0, indirectDrawCount, sizeof(VkDrawIndexedIndirectCommand));
			}
			else
			{
				// If multi draw is not available, we must issue separate draw commands
				for (size_t j = 0; j < indirectDrawCount; j++)
				{
					vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, j * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
				}
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

            {
				// release queue ownership of the buffer
                VkBufferMemoryBarrier barrier = vks::initializers::bufferMemoryBarrier();
                barrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                barrier.dstAccessMask = 0;
                barrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
                barrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
                barrier.buffer = indirectCommandsBuffer.buffer;
                barrier.offset = 0;
                barrier.size = indirectCommandsBuffer.descriptor.range;

                vkCmdPipelineBarrier(drawCmdBuffers[i],
									 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
									 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0,
                                     0, nullptr,
                                     1, &barrier,
                                     0, nullptr);
            }

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.plants.loadFromFile(getAssetPath() + "models/plants.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ground.loadFromFile(getAssetPath() + "models/plane_circle.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skysphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.plants.loadFromFile(getAssetPath() + "textures/texturearray_plants_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.ground.loadFromFile(getAssetPath() + "textures/ground_dry_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);

		plantTypeCount = 0;
		for ( auto& node : models.plants.nodes )
		{
			if ( node->mesh )
			{
				plantTypeCount++;
			}
		}
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
    {
        vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &computeQueue);

		// graphics pipeline
		{
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, ERenderBinding::Scene),
                // Binding 1: Fragment shader combined sampler (plants texture array)
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, ERenderBinding::PlantTextureArray),
                // Binding 2: Fragment shader combined sampler (ground texture)
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, ERenderBinding::Texture),
                // Binding 3: vertex shader uniform buffer primitive data
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, ERenderBinding::Primitives),
                // Binding 4
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, ERenderBinding::Materials),
            };

            VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
		}

		// compute pipeline
		{
            std::vector<VkDescriptorSetLayoutBinding> computeSetLayoutBindings = {
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, (uint32_t)EComputeBinding::Instances),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, (uint32_t)EComputeBinding::OutDrawCommands),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, (uint32_t)EComputeBinding::Scene),
                vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, (uint32_t)EComputeBinding::Primitives),
            };

			VkDescriptorSetLayoutCreateInfo createInfo = vks::initializers::descriptorSetLayoutCreateInfo(computeSetLayoutBindings);
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &computeDescriptorSetLayout));

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&computeDescriptorSetLayout, 1);
			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &computePipelineLayout));
		}
	}

	void setupDescriptorSet()
	{
        {
            VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

            std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
                // Binding 0: Vertex shader uniform buffer
                vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ERenderBinding::Scene, &uniformData.scene.descriptor),
                // Binding 1: Plants texture array combined
                vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ERenderBinding::PlantTextureArray, &textures.plants.descriptor),
                // Binding 2: Ground texture combined
                vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ERenderBinding::Texture, &textures.ground.descriptor),
                // Binding 3: Primitive Data uniform buffer
                vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ERenderBinding::Primitives, &uniformData.primitives.descriptor),
                // Binding 4
                vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ERenderBinding::Materials, &uniformData.materials.descriptor)
            };
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
		
		{
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &computeDescriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet));
            std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
                vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)EComputeBinding::Instances, &instanceBuffer.descriptor),
                vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)EComputeBinding::OutDrawCommands, &indirectCommandsBuffer.descriptor),
                vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)EComputeBinding::Scene, &uniformData.scene.descriptor),
                vks::initializers::writeDescriptorSet(computeDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)EComputeBinding::Primitives, &uniformData.primitives.descriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void prepareComputePipeline()
	{
		// create pipeline
		{
            VkComputePipelineCreateInfo createInfo = vks::initializers::computePipelineCreateInfo(computePipelineLayout, 0);
            createInfo.stage = loadShader(getShadersPath() + "indirectdraw/cull.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

            VkSpecializationMapEntry specMapEntry;
            specMapEntry.constantID = 0;
            specMapEntry.offset = 0;
            specMapEntry.size = sizeof(float);

            const float MinCullDistance = 10.0f;
            VkSpecializationInfo specializationInfo;
            specializationInfo.mapEntryCount = 1;
            specializationInfo.pMapEntries = &specMapEntry;
            specializationInfo.dataSize = sizeof(MinCullDistance);
            specializationInfo.pData = &MinCullDistance;
            createInfo.stage.pSpecializationInfo = &specializationInfo;

			VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &createInfo, nullptr, &computePipeline));
		}
		
		// create fence
		{
			VkCommandPoolCreateInfo createInfo = vks::initializers::commandPoolCreateInfo();
			createInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
			createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK_RESULT(vkCreateCommandPool(device, &createInfo, nullptr, &commputeCommandPool));

			VkCommandBufferAllocateInfo allocInfo = vks::initializers::commandBufferAllocateInfo(commputeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &computeCommandBuffer));
			
		}
		
		// create semaphore
		{
			VkFenceCreateInfo createInfo = vks::initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
			VK_CHECK_RESULT(vkCreateFence(device, &createInfo, nullptr, &computeFence));

			VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &computeSemaphore));
		}
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// This example uses two different input states, one for the instanced part and one for non-instanced rendering
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

		// Vertex input bindings
		// The instancing pipeline uses a vertex input state with two bindings
		bindingDescriptions = {
		    // Binding point 0: Mesh vertex layout description at per-vertex rate
		    vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(vkglTF::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
		    // Binding point 1: Instanced data at per-instance rate
		    vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(RenderInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
		};

		// Vertex attribute bindings
		// Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
		// instanced.vert:
		//	layout (location = 0) in vec3 inPos;		Per-Vertex
		//	...
		//	layout (location = 4) in vec3 instancePos;	Per-Instance
		attributeDescriptions = {
		    // Per-vertex attributes
		    // These are advanced for each vertex fetched by the vertex shader
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Pos, VK_FORMAT_R32G32B32_SFLOAT, 0),
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Normal, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::UV, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Color, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),
            // Per-Instance attributes
            // These are fetched for each instance rendered
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderInstanceData, transRow0)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderInstanceData, transRow1)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow2, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderInstanceData, transRow2)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderInstanceData, transRow3)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::primitiveIndex, VK_FORMAT_R32_SINT, offsetof(RenderInstanceData, primIndex)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::pad0, VK_FORMAT_R32_SINT, offsetof(RenderInstanceData, _pad0)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::pad1, VK_FORMAT_R32_SINT, offsetof(RenderInstanceData, _pad1)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::pad2, VK_FORMAT_R32_SINT, offsetof(RenderInstanceData, _pad2)),
		};
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
		inputState.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());

		pipelineCreateInfo.pVertexInputState = &inputState;

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.plants));

		// Only use non-instanced vertex attributes for models rendered without instancing
		inputState.vertexBindingDescriptionCount = 1;
		inputState.vertexAttributeDescriptionCount = 4;

		// Ground
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/ground.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/ground.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ground));

		// Skysphere
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skysphere));
	}


	void prepareDrawData()
	{
		indirectCommands.clear();
		indirectCommands.resize(objectCount);

		indirectDrawCount = 0;
		for ( auto& prim : gameScene.primitives )
        {
            const vkglTF::Mesh* mesh = models.plants.nodes[prim.meshIndex]->mesh;
			for ( size_t insIdx = 0; insIdx < prim.instances.size(); insIdx++ )
            {
                VkDrawIndexedIndirectCommand& indirectCmd = indirectCommands[indirectDrawCount];
                indirectCmd.instanceCount = 0;
                indirectCmd.firstInstance = indirectDrawCount;
                indirectCmd.firstIndex = mesh->primitives[0]->firstIndex;
                indirectCmd.indexCount = mesh->primitives[0]->indexCount;
                indirectCmd.vertexOffset = 0;

				indirectDrawCount++;
			}
		}

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand),
            indirectCommands.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &indirectCommandsBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &indirectCommandsBuffer, queue);

        stagingBuffer.destroy();

		{
			VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// release queue ownership of the buffer
			VkBufferMemoryBarrier barrier = vks::initializers::bufferMemoryBarrier();
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = 0;
			barrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
			barrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
			barrier.buffer = indirectCommandsBuffer.buffer;
			barrier.offset = 0;
			barrier.size = indirectCommandsBuffer.descriptor.range;

			vkCmdPipelineBarrier(cmdBuffer,
								 VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
								 VK_FLAGS_NONE,
								 0, nullptr,
								 1, &barrier,
								 0, nullptr);
			vulkanDevice->flushCommandBuffer(cmdBuffer, queue, true);
		}
	}


	void prepareMaterials()
    {
        VkDeviceSize minStorageBufferOffsetAlignment = vulkanDevice->properties.limits.minStorageBufferOffsetAlignment;
        uint32_t maxDescriptorSetStorageBuffers = vulkanDevice->properties.limits.maxDescriptorSetStorageBuffers;

		const size_t MaterialSize = sizeof(Material);
        std::cout << "minStorageBufferOffsetAlignment: " << minStorageBufferOffsetAlignment << std::endl;
        std::cout << "maxDescriptorSetStorageBuffers: " << maxDescriptorSetStorageBuffers << std::endl;
        std::cout << "sizeof(Material): " << MaterialSize << std::endl;

		std::vector<Material> materials;
		for ( uint32_t i = 0; i < textures.plants.layerCount; i++ )
        {
            Material mat;
            mat.tint = glm::vec4(1.0, 1.0, 1.0, 1.0);
            mat.textureIndex = i;
            mat.padding0 = 0;
            mat.padding1 = 0;
            mat.padding2 = 0;
            materials.push_back(mat);
        }
        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
			materials.size() * sizeof(Material),
			materials.data()
        ));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&uniformData.materials,
			stagingBuffer.size
		));
		
		vulkanDevice->copyBuffer(&stagingBuffer, &uniformData.materials, queue);
		stagingBuffer.destroy();
	}


    void prepareGameData()
    {
		objectCount = 0;
        gameScene.primitives.resize(PRIMITIVE_COUNT);
        for ( size_t primIdx = 0; primIdx < gameScene.primitives.size(); primIdx++ )
        {
            GamePrimitive& prim = gameScene.primitives[primIdx];
            prim.transform =
                glm::translate(glm::mat4(1.0f),
                               glm::vec3((float)(primIdx % PRIMITIVE_COUNT_BORDER) * PRIM_GAP, 0.0f, (float)(primIdx / PRIMITIVE_COUNT_BORDER) * PRIM_GAP));
            prim.meshIndex = primIdx % plantTypeCount;
            prim.materialIndex = prim.meshIndex;

            std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
            std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);
            prim.instances.resize(INSTANCE_PER_PRIM_PER_MESH);
            for ( size_t insIdx = 0; insIdx < INSTANCE_PER_PRIM_PER_MESH; insIdx++ )
            {
                float theta = 2 * float(M_PI) * uniformDist(rndEngine);
                float phi = acos(1 - 2 * uniformDist(rndEngine));
                GamePrimitiveInstance& instance = prim.instances[insIdx];

                const float scale = 2.0f;
                const glm::vec3 pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
                glm::mat4 insTransform = glm::translate(glm::mat4(1.0f), pos);
                instance.transform = insTransform;

				objectCount++;
            }
        }
    }

	void prepareRenderData()
    {
        std::vector<RenderPrimitiveData> primitives;
        primitives.resize(gameScene.primitives.size());

        size_t totalInstanceCount = 0;
		for ( size_t primIdx = 0; primIdx < primitives.size(); primIdx++ )
		{
			RenderPrimitiveData& rPrim = primitives[primIdx];
			GamePrimitive& gPrim = gameScene.primitives[primIdx];
            const vkglTF::Mesh* mesh = models.plants.nodes[gPrim.meshIndex]->mesh;

			rPrim.cullDistance = CULL_DISTANCE;
            rPrim.transform = gPrim.transform;
            rPrim.firstIndex = mesh->primitives[0]->firstIndex;
            rPrim.indexCount = mesh->primitives[0]->indexCount;
			rPrim.materialIndex = gPrim.materialIndex;

            totalInstanceCount += gPrim.instances.size();
		}

        std::vector<RenderInstanceData> instanceData;
        instanceData.resize(totalInstanceCount);

		size_t rinsIdx = 0;
		for ( size_t primIdx = 0; primIdx < gameScene.primitives.size(); primIdx++ )
		{
			const GamePrimitive& gprim = gameScene.primitives[primIdx];
			for ( size_t insIdx = 0; insIdx < gameScene.primitives[primIdx].instances.size(); insIdx++ )
			{
				const GamePrimitiveInstance& gins = gprim.instances[insIdx];
				RenderInstanceData& rins = instanceData[rinsIdx];
                rins.transRow0 = { gins.transform[0][0], gins.transform[1][0], gins.transform[2][0], gins.transform[3][0] };
                rins.transRow1 = { gins.transform[0][1], gins.transform[1][1], gins.transform[2][1], gins.transform[3][1] };
                rins.transRow2 = { gins.transform[0][2], gins.transform[1][2], gins.transform[2][2], gins.transform[3][2] };
                rins.transRow3 = { gins.transform[0][3], gins.transform[1][3], gins.transform[2][3], gins.transform[3][3] };
                rins.primIndex = (int)primIdx;
                rins._pad0 = (int)primIdx * 1;
                rins._pad1 = (int)primIdx * 2;
                rins._pad2 = (int)primIdx * 3;

				rinsIdx++;
			}
		}

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			primitives.size() * sizeof(RenderPrimitiveData),
			primitives.data()
		));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&uniformData.primitives,
			stagingBuffer.size
		));

		vulkanDevice->copyBuffer(&stagingBuffer, &uniformData.primitives, queue);
		stagingBuffer.destroy();

        // setup instance data
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            instanceData.size() * sizeof(RenderInstanceData),
            instanceData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &instanceBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &instanceBuffer, queue);

        stagingBuffer.destroy();
	}

	void prepareSceneData()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformData.scene,
			sizeof(renderScene)));

		VK_CHECK_RESULT(uniformData.scene.map());

		updateSceneData(true);
	}

	void updateSceneData(bool viewChanged)
	{
		if (viewChanged)
		{
			renderScene.projection = camera.matrices.perspective;
			renderScene.view = camera.matrices.view;
			renderScene.cameraPos = glm::vec4(camera.position, 1.0f) * -1.0f;
			frustum.update(renderScene.projection * renderScene.view);
			memcpy(renderScene.frustumPlanes, frustum.planes.data(), sizeof(glm::vec4) * 6);
		}

		memcpy(uniformData.scene.mapped, &renderScene, sizeof(renderScene));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		vkWaitForFences(device, 1, &computeFence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &computeFence);

		// submit compute commands
        {
            VkSubmitInfo computeSubmitInfo = vks::initializers::submitInfo();
            computeSubmitInfo.commandBufferCount = 1;
            computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
            computeSubmitInfo.signalSemaphoreCount = 1;
            computeSubmitInfo.pSignalSemaphores = &computeSemaphore;

            VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, VK_NULL_HANDLE));
		}
		
		// submit graphics commands
		{
			std::array<VkPipelineStageFlags, 2> stageFlags = {
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			};

            std::array<VkSemaphore, 2> semaphoresToWait = {
                semaphores.presentComplete,
				computeSemaphore,
			};

            // Command buffer to be submitted to the queue
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
			submitInfo.waitSemaphoreCount = (uint32_t)semaphoresToWait.size();
			submitInfo.pWaitSemaphores = semaphoresToWait.data();
			submitInfo.pWaitDstStageMask = stageFlags.data();

            // Submit to queue
            VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, computeFence));
		}
		

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareMaterials();
        prepareGameData();
        prepareRenderData();
        prepareSceneData();
		prepareDrawData();
		setupDescriptorSetLayout();
		prepareComputePipeline();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildComputeCommandBuffers();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (camera.updated)
		{
			updateSceneData(true);
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (!vulkanDevice->features.multiDrawIndirect) {
			if (overlay->header("Info")) {
				overlay->text("multiDrawIndirect not supported");
			}
		}
		if (overlay->header("Statistics")) {
			overlay->text("Objects: %d", objectCount);
		}
	}
};

VULKAN_EXAMPLE_MAIN()