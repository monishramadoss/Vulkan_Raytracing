#include "rtPipe.h"
#include <stdlib.h>
#include <exception>
#include "shared_with_shaders.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

static const String sShadersFolder = "_data/shaders/";
static const String sScenesFolder = "_data/scenes/";

static const float sMoveSpeed = 2.0f;
static const float sAccelMult = 5.0f;
static const float sRotateSpeed = 0.25f;

static vec3 sSunPos = vec3(1.0f, 1.0f, 1.0f);
static const float sAmbientLight = 0.1f;


struct VkGeometryInstance {
	float transform[12];
	uint32_t instanceId : 24;
	uint32_t mask : 8;
	uint32_t instanceOffset : 24;
	uint32_t flags : 8;
	uint64_t accelerationStructureHandle;
};



RtxApp::RtxApp()
	: vulkanapp()
	, _RTXPipelineLayout(VK_NULL_HANDLE)
	, _RTXPipeline(VK_NULL_HANDLE)
	, _RTXDescriptorPool(VK_NULL_HANDLE)
	, WKeyDown(false)
	, AKeyDown(false)
	, SKeyDown(false)
	, DKeyDown(false)
	, ShiftDown(false)
	, LMBDown(false)
{
}
RtxApp::~RtxApp() {

}


void RtxApp::InitSettings() {
	_Settings.name = "rtxON";
	_Settings.enableValidation = true;
	_Settings.enableVSync = false;
	_Settings.supportRaytracing = true;
	_Settings.supportDescriptorIndexing = true;
}

void RtxApp::InitApp() {
	LoadSceneGeometry();
	CreateScene();
	CreateCamera();
	CreateDescriptorSetsLayouts();
	CreateRaytracingPipelineAndSBT();
	UpdateDescriptorSets();
}

void RtxApp::FreeResources() {
	for (RTMesh& mesh : _Scene.meshes) {
		vkDestroyAccelerationStructureNV(_Device, mesh.blas.accelerationStructure, nullptr);
		vkFreeMemory(_Device, mesh.blas.memory, nullptr);
	}
	_Scene.meshes.clear();
	_Scene.materials.clear();

	if (_Scene.topLevelAS.accelerationStructure) {
		vkDestroyAccelerationStructureNV(_Device, _Scene.topLevelAS.accelerationStructure, nullptr);
		_Scene.topLevelAS.accelerationStructure = VK_NULL_HANDLE;
	}
	if (_Scene.topLevelAS.memory) {
		vkFreeMemory(_Device, _Scene.topLevelAS.memory, nullptr);
		_Scene.topLevelAS.memory = VK_NULL_HANDLE;
	}

	if (_RTXDescriptorPool) {
		vkDestroyDescriptorPool(_Device, _RTXDescriptorPool, nullptr);
		_RTXDescriptorPool = VK_NULL_HANDLE;
	}

	rtxHelper.Destroy();

	if (_RTXPipeline) {
		vkDestroyPipeline(_Device, _RTXPipeline, nullptr);
		_RTXPipeline = VK_NULL_HANDLE;
	}

	if (_RTXPipelineLayout) {
		vkDestroyPipelineLayout(_Device, _RTXPipelineLayout, nullptr);
		_RTXPipelineLayout = VK_NULL_HANDLE;
	}

	for (VkDescriptorSetLayout& dsl : _RTXDescriptorSetsLayouts) {
		vkDestroyDescriptorSetLayout(_Device, dsl, nullptr);
	}
	_RTXDescriptorSetsLayouts.clear();
}

void RtxApp::FillCommandBuffer(VkCommandBuffer commandBuffer, const size_t imageIndex) {
	vkCmdBindPipeline(commandBuffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		_RTXPipeline);

	vkCmdBindDescriptorSets(commandBuffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		_RTXPipelineLayout, 0,
		static_cast<uint32_t>(_RTXDescriptorSets.size()), _RTXDescriptorSets.data(),
		0, 0);

	vkCmdTraceRaysNV(commandBuffer,
		rtxHelper.GetSBTBuffer(), rtxHelper.GetRaygenOffset(),
		rtxHelper.GetSBTBuffer(), rtxHelper.GetMissGroupsOffset(), rtxHelper.GetGroupsStride(),
		rtxHelper.GetSBTBuffer(), rtxHelper.GetHitGroupsOffset(), rtxHelper.GetGroupsStride(),
		VK_NULL_HANDLE, 0, 0,
		_Settings.resolutionX, _Settings.resolutionY, 1u);
}

void RtxApp::OnMouseMove(const float x, const float y) {
	vec2 newPos(x, y);
	vec2 delta = CursorPos - newPos;

	if (LMBDown) {
		_Camera.Rotate(delta.x * sRotateSpeed, delta.y * sRotateSpeed);
	}

	CursorPos = newPos;
}

void RtxApp::OnMouseButton(const int button, const int action, const int mods) {
	if (0 == button && GLFW_PRESS == action) {
		LMBDown = true;
	}
	else if (0 == button && GLFW_RELEASE == action) {
		LMBDown = false;
	}
}

void RtxApp::OnKey(const int key, const int scancode, const int action, const int mods) {
	if (GLFW_PRESS == action) {
		switch (key) {
		case GLFW_KEY_W: WKeyDown = true; break;
		case GLFW_KEY_A: AKeyDown = true; break;
		case GLFW_KEY_S: SKeyDown = true; break;
		case GLFW_KEY_D: DKeyDown = true; break;

		case GLFW_KEY_LEFT_SHIFT:
		case GLFW_KEY_RIGHT_SHIFT:
			ShiftDown = true;
			break;
		}
	}
	else if (GLFW_RELEASE == action) {
		switch (key) {
		case GLFW_KEY_W: WKeyDown = false; break;
		case GLFW_KEY_A: AKeyDown = false; break;
		case GLFW_KEY_S: SKeyDown = false; break;
		case GLFW_KEY_D: DKeyDown = false; break;

		case GLFW_KEY_LEFT_SHIFT:
		case GLFW_KEY_RIGHT_SHIFT:
			ShiftDown = false;
			break;
		}
	}
}

void RtxApp::Update(const size_t, const float dt) {
	String frameStats = ToString(fpsMeter.GetFPS(), 1) + " FPS (" + ToString(fpsMeter.GetFrameTime(), 1) + " ms)";
	String fullTitle = _Settings.name + "  " + frameStats;
	glfwSetWindowTitle(_Window, fullTitle.c_str());
	UniformParams* params = reinterpret_cast<UniformParams*>(_CameraBuffer.Map());

	params->sunPosAndAmbient = vec4(sSunPos, sAmbientLight);

	UpdateCameraParams(params, dt);

	_CameraBuffer.Unmap();
}



bool RtxApp::CreateAS(const VkAccelerationStructureTypeNV type,
	const uint32_t geometryCount,
	const VkGeometryNV* geometries,
	const uint32_t instanceCount,
	RTAccelerationStructure& _as) {

	VkAccelerationStructureInfoNV& accelerationStructureInfo = _as.accelerationStructureInfo;
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.pNext = nullptr;
	accelerationStructureInfo.type = type;
	accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
	accelerationStructureInfo.geometryCount = geometryCount;
	accelerationStructureInfo.instanceCount = instanceCount;
	accelerationStructureInfo.pGeometries = geometries;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo;
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.pNext = nullptr;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	accelerationStructureCreateInfo.compactedSize = 0;

	VkResult error = vkCreateAccelerationStructureNV(_Device, &accelerationStructureCreateInfo, nullptr, &_as.accelerationStructure);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkCreateAccelerationStructureNV");
		return false;
	}

	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo;
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.pNext = nullptr;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
	memoryRequirementsInfo.accelerationStructure = _as.accelerationStructure;

	VkMemoryRequirements2 memoryRequirements;
	vkGetAccelerationStructureMemoryRequirementsNV(_Device, &memoryRequirementsInfo, &memoryRequirements);

	VkMemoryAllocateInfo memoryAllocateInfo;
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = nullptr;
	memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = helpers::GetMemoryType(memoryRequirements.memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	error = vkAllocateMemory(_Device, &memoryAllocateInfo, nullptr, &_as.memory);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkAllocateMemory for AS");
		return false;
	}

	VkBindAccelerationStructureMemoryInfoNV bindInfo;
	bindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	bindInfo.pNext = nullptr;
	bindInfo.accelerationStructure = _as.accelerationStructure;
	bindInfo.memory = _as.memory;
	bindInfo.memoryOffset = 0;
	bindInfo.deviceIndexCount = 0;
	bindInfo.pDeviceIndices = nullptr;

	error = vkBindAccelerationStructureMemoryNV(_Device, 1, &bindInfo);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkBindAccelerationStructureMemoryNVX");
		return false;
	}

	error = vkGetAccelerationStructureHandleNV(_Device, _as.accelerationStructure, sizeof(uint64_t), &_as.handle);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkGetAccelerationStructureHandleNVX");
		return false;
	}

	return true;
}

void RtxApp::LoadSceneGeometry() {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	String warn, error;

	String fileName = sScenesFolder + "cornell_box/CornellBox.obj";//"fake_whitted/fake_whitted.obj";
	String baseDir = fileName;
	const size_t slash = baseDir.find_last_of('/');
	if (slash != String::npos) {
		baseDir.erase(slash);
	}

	const bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, fileName.c_str(), baseDir.c_str(), true);
	if (result) {
		_Scene.meshes.resize(shapes.size());
		_Scene.materials.resize(materials.size());
		
		for (size_t meshIdx = 0; meshIdx < shapes.size(); ++meshIdx) {
			RTMesh& mesh = _Scene.meshes[meshIdx];
			const tinyobj::shape_t& shape = shapes[meshIdx];

			const size_t numFaces = shape.mesh.num_face_vertices.size();
			const size_t numVertices = numFaces * 3;

			mesh.numVertices = static_cast<uint32_t>(numVertices);
			mesh.numFaces = static_cast<uint32_t>(numFaces);

			const size_t positionsBufferSize = numVertices * sizeof(vec3);
			const size_t indicesBufferSize = numFaces * 3 * sizeof(uint32_t);
			const size_t facesBufferSize = numFaces * 4 * sizeof(uint32_t);
			const size_t attribsBufferSize = numVertices * sizeof(VertexAttribute);
			const size_t matIDsBufferSize = numFaces * sizeof(uint32_t);

			VkResult error = mesh.positions.Create(positionsBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			CHECK_VK_ERROR(error, "mesh.positions.Create");

			error = mesh.indices.Create(indicesBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			CHECK_VK_ERROR(error, "mesh.indices.Create");

			error = mesh.faces.Create(facesBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			CHECK_VK_ERROR(error, "mesh.faces.Create");

			error = mesh.attribs.Create(attribsBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			CHECK_VK_ERROR(error, "mesh.attribs.Create");

			error = mesh.matIDs.Create(matIDsBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			CHECK_VK_ERROR(error, "mesh.matIDs.Create");

			vec3* positions = reinterpret_cast<vec3*>(mesh.positions.Map());
			VertexAttribute* attribs = reinterpret_cast<VertexAttribute*>(mesh.attribs.Map());
			uint32_t* indices = reinterpret_cast<uint32_t*>(mesh.indices.Map());
			uint32_t* faces = reinterpret_cast<uint32_t*>(mesh.faces.Map());
			uint32_t* matIDs = reinterpret_cast<uint32_t*>(mesh.matIDs.Map());

			size_t vIdx = 0;
			for (size_t f = 0; f < numFaces; ++f) {
				if (shape.mesh.num_face_vertices[f] == 3) {
				
					for (size_t j = 0; j < 3; ++j, ++vIdx) {
						const tinyobj::index_t& i = shape.mesh.indices[vIdx];							
						vec3& pos = positions[vIdx];
						vec4& normal = attribs[vIdx].normal;								
						pos.x = attrib.vertices[3 * i.vertex_index + 0];
						pos.y = attrib.vertices[3 * i.vertex_index + 1];
						pos.z = attrib.vertices[3 * i.vertex_index + 2];
						normal.x = attrib.normals[3 * i.normal_index + 0];
						normal.y = attrib.normals[3 * i.normal_index + 1];
						normal.z = attrib.normals[3 * i.normal_index + 2];										
					}

					const uint32_t a = static_cast<uint32_t>(3 * f + 0);
					const uint32_t b = static_cast<uint32_t>(3 * f + 1);
					const uint32_t c = static_cast<uint32_t>(3 * f + 2);
					indices[a] = a;
					indices[b] = b;
					indices[c] = c;
					faces[4 * f + 0] = a;
					faces[4 * f + 1] = b;
					faces[4 * f + 2] = c;
					matIDs[f] = static_cast<uint32_t>(shape.mesh.material_ids[f]);
				}
				else {
					printf("%d %d", meshIdx, f);
				}
			}

			mesh.matIDs.Unmap();
			mesh.indices.Unmap();
			mesh.faces.Unmap();
			mesh.attribs.Unmap();
			mesh.positions.Unmap();
		}

		VkImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

		
	}

	// prepare shader resources infos
	const size_t numMeshes = _Scene.meshes.size();
	const size_t numMaterials = _Scene.materials.size();

	_Scene.matIDsBufferInfos.resize(numMeshes);
	_Scene.attribsBufferInfos.resize(numMeshes);
	_Scene.facesBufferInfos.resize(numMeshes);
	for (size_t i = 0; i < numMeshes; ++i) {
		const RTMesh& mesh = _Scene.meshes[i];
		VkDescriptorBufferInfo& matIDsInfo = _Scene.matIDsBufferInfos[i];
		VkDescriptorBufferInfo& attribsInfo = _Scene.attribsBufferInfos[i];
		VkDescriptorBufferInfo& facesInfo = _Scene.facesBufferInfos[i];

		matIDsInfo.buffer = mesh.matIDs.GetBuffer();
		matIDsInfo.offset = 0;
		matIDsInfo.range = mesh.matIDs.GetSize();

		attribsInfo.buffer = mesh.attribs.GetBuffer();
		attribsInfo.offset = 0;
		attribsInfo.range = mesh.attribs.GetSize();

		facesInfo.buffer = mesh.faces.GetBuffer();
		facesInfo.offset = 0;
		facesInfo.range = mesh.faces.GetSize();
	}

	_Scene.texturesInfos.resize(numMaterials);
	for (size_t i = 0; i < numMaterials; ++i) {
		const RTMaterial& mat = _Scene.materials[i];
		VkDescriptorImageInfo& textureInfo = _Scene.texturesInfos[i];
		textureInfo.sampler = mat.texture.GetSampler();
		textureInfo.imageView = mat.texture.GetImageView();
		textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
}

void RtxApp::CreateScene() {
	const float transform[12] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
	};

	const size_t numMeshes = _Scene.meshes.size();

	Array<VkGeometryNV> geometries(numMeshes);
	Array<VkGeometryInstance> instances(numMeshes);

	for (size_t i = 0; i < numMeshes; ++i) {
		RTMesh& mesh = _Scene.meshes[i];
		VkGeometryNV& geometry = geometries[i];

		geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
		geometry.pNext = nullptr;
		geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
		geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
		geometry.geometry.triangles.pNext = nullptr;
		geometry.geometry.triangles.vertexData = mesh.positions.GetBuffer();
		geometry.geometry.triangles.vertexOffset = 0;
		geometry.geometry.triangles.vertexCount = mesh.numVertices;
		geometry.geometry.triangles.vertexStride = sizeof(vec3);
		geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry.geometry.triangles.indexData = mesh.indices.GetBuffer();
		geometry.geometry.triangles.indexOffset = 0;
		geometry.geometry.triangles.indexCount = mesh.numFaces * 3;
		geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
		geometry.geometry.triangles.transformOffset = 0;
		geometry.geometry.aabbs = { };
		geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

		CreateAS(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV, 1, &geometry, 0, mesh.blas);

		VkGeometryInstance& instance = instances[i];
		std::memcpy(instance.transform, transform, sizeof(transform));
		instance.instanceId = static_cast<uint32_t>(i);
		instance.mask = 0xff;
		instance.instanceOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
		instance.accelerationStructureHandle = mesh.blas.handle;
	}

	helpers::Buffer instancesBuffer;
	VkResult error = instancesBuffer.Create(instances.size() * sizeof(VkGeometryInstance), VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	CHECK_VK_ERROR(error, "instancesBuffer.Create");

	if (!instancesBuffer.UploadData(instances.data(), instancesBuffer.GetSize())) {
		assert(false && "Failed to upload instances buffer");
	}

	CreateAS(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV, 0, nullptr, 1, _Scene.topLevelAS);

	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo;
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.pNext = nullptr;

	VkDeviceSize maximumBlasSize = 0;
	for (const RTMesh& mesh : _Scene.meshes) {
		memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
		memoryRequirementsInfo.accelerationStructure = mesh.blas.accelerationStructure;

		VkMemoryRequirements2 memReqBLAS;
		vkGetAccelerationStructureMemoryRequirementsNV(_Device, &memoryRequirementsInfo, &memReqBLAS);

		maximumBlasSize = Max(maximumBlasSize, memReqBLAS.memoryRequirements.size);
	}

	VkMemoryRequirements2 memReqTLAS;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
	memoryRequirementsInfo.accelerationStructure = _Scene.topLevelAS.accelerationStructure;
	vkGetAccelerationStructureMemoryRequirementsNV(_Device, &memoryRequirementsInfo, &memReqTLAS);

	const VkDeviceSize scratchBufferSize = Max(maximumBlasSize, memReqTLAS.memoryRequirements.size);

	helpers::Buffer scratchBuffer;
	error = scratchBuffer.Create(scratchBufferSize, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CHECK_VK_ERROR(error, "scratchBuffer.Create");

	VkCommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = nullptr;
	commandBufferAllocateInfo.commandPool = _CommandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	error = vkAllocateCommandBuffers(_Device, &commandBufferAllocateInfo, &commandBuffer);
	CHECK_VK_ERROR(error, "vkAllocateCommandBuffers");

	VkCommandBufferBeginInfo beginInfo;
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = nullptr;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkMemoryBarrier memoryBarrier;
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.pNext = nullptr;
	memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

	for (size_t i = 0; i < numMeshes; ++i) {
		_Scene.meshes[i].blas.accelerationStructureInfo.instanceCount = 0;
		_Scene.meshes[i].blas.accelerationStructureInfo.geometryCount = 1;
		_Scene.meshes[i].blas.accelerationStructureInfo.pGeometries = &geometries[i];
		vkCmdBuildAccelerationStructureNV(commandBuffer, &_Scene.meshes[i].blas.accelerationStructureInfo,
			VK_NULL_HANDLE, 0, VK_FALSE,
			_Scene.meshes[i].blas.accelerationStructure, VK_NULL_HANDLE,
			scratchBuffer.GetBuffer(), 0);

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);
	}

	
	_Scene.topLevelAS.accelerationStructureInfo.instanceCount = static_cast<uint32_t>(instances.size());
	_Scene.topLevelAS.accelerationStructureInfo.geometryCount = 0;
	_Scene.topLevelAS.accelerationStructureInfo.pGeometries = nullptr;
	vkCmdBuildAccelerationStructureNV(commandBuffer, &_Scene.topLevelAS.accelerationStructureInfo,
		instancesBuffer.GetBuffer(), 0, VK_FALSE,
		_Scene.topLevelAS.accelerationStructure, VK_NULL_HANDLE,
		scratchBuffer.GetBuffer(), 0);

	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo;
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;

	vkQueueSubmit(_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(_GraphicsQueue);
	vkFreeCommandBuffers(_Device, _CommandPool, 1, &commandBuffer);
}

void RtxApp::CreateCamera() {
	VkResult error = _CameraBuffer.Create(sizeof(UniformParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	CHECK_VK_ERROR(error, "_CameraBuffer.Create");

	_Camera.SetViewport({ 0, 0, static_cast<int>(_Settings.resolutionX), static_cast<int>(_Settings.resolutionY) });
	_Camera.SetViewPlanes(0.1f, 100.0f);
	_Camera.SetFovY(45.0f);
	_Camera.LookAt(vec3(0.25f, 3.20f, 6.15f), vec3(0.25f, 2.75f, 5.25f));
}

void RtxApp::UpdateCameraParams(UniformParams* params, const float dt) {
	vec2 moveDelta(0.0f, 0.0f);
	if (WKeyDown) {
		moveDelta.y += 1.0f;
	}
	if (SKeyDown) {
		moveDelta.y -= 1.0f;
	}
	if (AKeyDown) {
		moveDelta.x -= 1.0f;
	}
	if (DKeyDown) {
		moveDelta.x += 1.0f;
	}

	moveDelta *= sMoveSpeed * dt * (ShiftDown ? sAccelMult : 1.0f);
	_Camera.Move(moveDelta.x, moveDelta.y);

	params->camPos = vec4(_Camera.GetPosition(), 0.0f);
	params->camDir = vec4(_Camera.GetDirection(), 0.0f);
	params->camUp = vec4(_Camera.GetUp(), 0.0f);
	params->camSide = vec4(_Camera.GetSide(), 0.0f);
	params->camNearFarFov = vec4(_Camera.GetNearPlane(), _Camera.GetFarPlane(), Deg2Rad(_Camera.GetFovY()), 0.0f);
}

void RtxApp::CreateDescriptorSetsLayouts() {
	const uint32_t numMeshes = static_cast<uint32_t>(_Scene.meshes.size());
	const uint32_t numMaterials = static_cast<uint32_t>(_Scene.materials.size());

	_RTXDescriptorSetsLayouts.resize(SWS_NUM_SETS);

	VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding;
	accelerationStructureLayoutBinding.binding = SWS_SCENE_AS_BINDING;
	accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
	accelerationStructureLayoutBinding.descriptorCount = 1;
	accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	accelerationStructureLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding resultImageLayoutBinding;
	resultImageLayoutBinding.binding = SWS_RESULT_IMAGE_BINDING;
	resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageLayoutBinding.descriptorCount = 1;
	resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	resultImageLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding camdataBufferBinding;
	camdataBufferBinding.binding = SWS_CAMDATA_BINDING;
	camdataBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	camdataBufferBinding.descriptorCount = 1;
	camdataBufferBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	camdataBufferBinding.pImmutableSamplers = nullptr;

	std::vector<VkDescriptorSetLayoutBinding> bindings({
		accelerationStructureLayoutBinding,
		resultImageLayoutBinding,
		camdataBufferBinding
		});

	VkDescriptorSetLayoutCreateInfo set0LayoutInfo;
	set0LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set0LayoutInfo.pNext = nullptr;
	set0LayoutInfo.flags = 0;
	set0LayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	set0LayoutInfo.pBindings = bindings.data();

	VkResult error = vkCreateDescriptorSetLayout(_Device, &set0LayoutInfo, nullptr, &_RTXDescriptorSetsLayouts[SWS_SCENE_AS_SET]);
	CHECK_VK_ERROR(error, "vkCreateDescriptorSetLayout");
	const VkDescriptorBindingFlagsEXT flag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags;
	bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	bindingFlags.pNext = nullptr;
	bindingFlags.pBindingFlags = &flag;
	bindingFlags.bindingCount = 1;

	VkDescriptorSetLayoutBinding ssboBinding;
	ssboBinding.binding = 0;
	ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ssboBinding.descriptorCount = numMeshes;
	ssboBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	ssboBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo set1LayoutInfo;
	set1LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set1LayoutInfo.pNext = &bindingFlags;
	set1LayoutInfo.flags = 0;
	set1LayoutInfo.bindingCount = 1;
	set1LayoutInfo.pBindings = &ssboBinding;

	error = vkCreateDescriptorSetLayout(_Device, &set1LayoutInfo, nullptr, &_RTXDescriptorSetsLayouts[SWS_MATIDS_SET]);
	CHECK_VK_ERROR(error, L"vkCreateDescriptorSetLayout");
	error = vkCreateDescriptorSetLayout(_Device, &set1LayoutInfo, nullptr, &_RTXDescriptorSetsLayouts[SWS_ATTRIBS_SET]);
	CHECK_VK_ERROR(error, L"vkCreateDescriptorSetLayout");
	error = vkCreateDescriptorSetLayout(_Device, &set1LayoutInfo, nullptr, &_RTXDescriptorSetsLayouts[SWS_FACES_SET]);
	CHECK_VK_ERROR(error, L"vkCreateDescriptorSetLayout");
	VkDescriptorSetLayoutBinding textureBinding;
	textureBinding.binding = 0;
	textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureBinding.descriptorCount = numMaterials;
	textureBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	textureBinding.pImmutableSamplers = nullptr;

	set1LayoutInfo.pBindings = &textureBinding;

	error = vkCreateDescriptorSetLayout(_Device, &set1LayoutInfo, nullptr, &_RTXDescriptorSetsLayouts[SWS_TEXTURES_SET]);
	CHECK_VK_ERROR(error, L"vkCreateDescriptorSetLayout");
}

void RtxApp::CreateRaytracingPipelineAndSBT() {
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.setLayoutCount = SWS_NUM_SETS;
	pipelineLayoutCreateInfo.pSetLayouts = _RTXDescriptorSetsLayouts.data();
	pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
	pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

	VkResult error = vkCreatePipelineLayout(_Device, &pipelineLayoutCreateInfo, nullptr, &_RTXPipelineLayout);
	CHECK_VK_ERROR(error, "vkCreatePipelineLayout");


	helpers::Shader rayGenShader, rayChitShader, rayMissShader, shadowChit, shadowMiss;
	rayGenShader.LoadFromFile((sShadersFolder + "ray_gen.bin").c_str());
	rayChitShader.LoadFromFile((sShadersFolder + "ray_chit.bin").c_str());
	rayMissShader.LoadFromFile((sShadersFolder + "ray_miss.bin").c_str());
	shadowChit.LoadFromFile((sShadersFolder + "shadow_ray_chit.bin").c_str());
	shadowMiss.LoadFromFile((sShadersFolder + "shadow_ray_miss.bin").c_str());

	rtxHelper.Initialize(2, 2, _RTXProps.shaderGroupHandleSize);

	rtxHelper.SetRaygenStage(rayGenShader.GetShaderStage(VK_SHADER_STAGE_RAYGEN_BIT_NV));

	rtxHelper.AddStageToHitGroup({ rayChitShader.GetShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV) }, SWS_PRIMARY_HIT_SHADERS_IDX);
	rtxHelper.AddStageToHitGroup({ shadowChit.GetShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV) }, SWS_SHADOW_HIT_SHADERS_IDX);

	rtxHelper.AddStageToMissGroup(rayMissShader.GetShaderStage(VK_SHADER_STAGE_MISS_BIT_NV), SWS_PRIMARY_MISS_SHADERS_IDX);
	rtxHelper.AddStageToMissGroup(shadowMiss.GetShaderStage(VK_SHADER_STAGE_MISS_BIT_NV), SWS_SHADOW_MISS_SHADERS_IDX);


	VkRayTracingPipelineCreateInfoNV rayPipelineInfo;
	rayPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
	rayPipelineInfo.pNext = nullptr;
	rayPipelineInfo.flags = 0;
	rayPipelineInfo.groupCount = rtxHelper.GetNu_Groups();
	rayPipelineInfo.stageCount = rtxHelper.GetNu_Stages();
	rayPipelineInfo.pStages = rtxHelper.GetStages();
	rayPipelineInfo.pGroups = rtxHelper.GetGroups();
	rayPipelineInfo.maxRecursionDepth = 1;
	rayPipelineInfo.layout = _RTXPipelineLayout;
	rayPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	rayPipelineInfo.basePipelineIndex = 0;

	error = vkCreateRayTracingPipelinesNV(_Device, VK_NULL_HANDLE, 1, &rayPipelineInfo, VK_NULL_HANDLE, &_RTXPipeline);
	CHECK_VK_ERROR(error, "vkCreateRaytracingPipelinesNVX");

	rtxHelper.CreateSBT(_Device, _RTXPipeline);
}

void RtxApp::UpdateDescriptorSets() {
	const uint32_t numMeshes = static_cast<uint32_t>(_Scene.meshes.size());
	const uint32_t numMaterials = static_cast<uint32_t>(_Scene.materials.size());

	std::vector<VkDescriptorPoolSize> poolSizes({
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, numMeshes * 3 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, numMaterials }
		});

	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo;
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.pNext = nullptr;
	descriptorPoolCreateInfo.flags = 0;
	descriptorPoolCreateInfo.maxSets = SWS_NUM_SETS;
	descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();

	VkResult error = vkCreateDescriptorPool(_Device, &descriptorPoolCreateInfo, nullptr, &_RTXDescriptorPool);
	CHECK_VK_ERROR(error, "vkCreateDescriptorPool");

	_RTXDescriptorSets.resize(SWS_NUM_SETS);

	Array<uint32_t> variableDescriptorCounts({ 1, numMeshes, numMeshes, numMeshes, numMaterials,
		});

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountInfo;
	variableDescriptorCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	variableDescriptorCountInfo.pNext = nullptr;
	variableDescriptorCountInfo.descriptorSetCount = SWS_NUM_SETS;
	variableDescriptorCountInfo.pDescriptorCounts = variableDescriptorCounts.data();

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = &variableDescriptorCountInfo;
	descriptorSetAllocateInfo.descriptorPool = _RTXDescriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = SWS_NUM_SETS;
	descriptorSetAllocateInfo.pSetLayouts = _RTXDescriptorSetsLayouts.data();

	error = vkAllocateDescriptorSets(_Device, &descriptorSetAllocateInfo, _RTXDescriptorSets.data());
	CHECK_VK_ERROR(error, "vkAllocateDescriptorSets");


	VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo;
	descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
	descriptorAccelerationStructureInfo.pNext = nullptr;
	descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
	descriptorAccelerationStructureInfo.pAccelerationStructures = &_Scene.topLevelAS.accelerationStructure;

	VkWriteDescriptorSet accelerationStructureWrite;
	accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
	accelerationStructureWrite.dstSet = _RTXDescriptorSets[SWS_SCENE_AS_SET];
	accelerationStructureWrite.dstBinding = SWS_SCENE_AS_BINDING;
	accelerationStructureWrite.dstArrayElement = 0;
	accelerationStructureWrite.descriptorCount = 1;
	accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
	accelerationStructureWrite.pImageInfo = nullptr;
	accelerationStructureWrite.pBufferInfo = nullptr;
	accelerationStructureWrite.pTexelBufferView = nullptr;

	VkDescriptorImageInfo descriptorOutputImageInfo;
	descriptorOutputImageInfo.sampler = VK_NULL_HANDLE;
	descriptorOutputImageInfo.imageView = _OffscreenImage.GetImageView();
	descriptorOutputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet resultImageWrite;
	resultImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	resultImageWrite.pNext = nullptr;
	resultImageWrite.dstSet = _RTXDescriptorSets[SWS_RESULT_IMAGE_SET];
	resultImageWrite.dstBinding = SWS_RESULT_IMAGE_BINDING;
	resultImageWrite.dstArrayElement = 0;
	resultImageWrite.descriptorCount = 1;
	resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	resultImageWrite.pImageInfo = &descriptorOutputImageInfo;
	resultImageWrite.pBufferInfo = nullptr;
	resultImageWrite.pTexelBufferView = nullptr;


	VkDescriptorBufferInfo camdataBufferInfo;
	camdataBufferInfo.buffer = _CameraBuffer.GetBuffer();
	camdataBufferInfo.offset = 0;
	camdataBufferInfo.range = _CameraBuffer.GetSize();

	VkWriteDescriptorSet camdataBufferWrite;
	camdataBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	camdataBufferWrite.pNext = nullptr;
	camdataBufferWrite.dstSet = _RTXDescriptorSets[SWS_CAMDATA_SET];
	camdataBufferWrite.dstBinding = SWS_CAMDATA_BINDING;
	camdataBufferWrite.dstArrayElement = 0;
	camdataBufferWrite.descriptorCount = 1;
	camdataBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	camdataBufferWrite.pImageInfo = nullptr;
	camdataBufferWrite.pBufferInfo = &camdataBufferInfo;
	camdataBufferWrite.pTexelBufferView = nullptr;


	VkWriteDescriptorSet matIDsBufferWrite;
	matIDsBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	matIDsBufferWrite.pNext = nullptr;
	matIDsBufferWrite.dstSet = _RTXDescriptorSets[SWS_MATIDS_SET];
	matIDsBufferWrite.dstBinding = 0;
	matIDsBufferWrite.dstArrayElement = 0;
	matIDsBufferWrite.descriptorCount = numMeshes;
	matIDsBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	matIDsBufferWrite.pImageInfo = nullptr;
	matIDsBufferWrite.pBufferInfo = _Scene.matIDsBufferInfos.data();
	matIDsBufferWrite.pTexelBufferView = nullptr;


	VkWriteDescriptorSet attribsBufferWrite;
	attribsBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	attribsBufferWrite.pNext = nullptr;
	attribsBufferWrite.dstSet = _RTXDescriptorSets[SWS_ATTRIBS_SET];
	attribsBufferWrite.dstBinding = 0;
	attribsBufferWrite.dstArrayElement = 0;
	attribsBufferWrite.descriptorCount = numMeshes;
	attribsBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	attribsBufferWrite.pImageInfo = nullptr;
	attribsBufferWrite.pBufferInfo = _Scene.attribsBufferInfos.data();
	attribsBufferWrite.pTexelBufferView = nullptr;

	VkWriteDescriptorSet facesBufferWrite;
	facesBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	facesBufferWrite.pNext = nullptr;
	facesBufferWrite.dstSet = _RTXDescriptorSets[SWS_FACES_SET];
	facesBufferWrite.dstBinding = 0;
	facesBufferWrite.dstArrayElement = 0;
	facesBufferWrite.descriptorCount = numMeshes;
	facesBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	facesBufferWrite.pImageInfo = nullptr;
	facesBufferWrite.pBufferInfo = _Scene.facesBufferInfos.data();
	facesBufferWrite.pTexelBufferView = nullptr;


	Array<VkWriteDescriptorSet> descriptorWrites({
		accelerationStructureWrite,
		resultImageWrite,
		camdataBufferWrite,
		matIDsBufferWrite,
		attribsBufferWrite,
		facesBufferWrite
		});

	vkUpdateDescriptorSets(_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, VK_NULL_HANDLE);
}


// SBT Helper class

RTXHelper::RTXHelper()
	: _ShaderHeaderSize(0u)
	, _NumHitGroups(0u)
	, _NumMissGroups(0u) {
}

void RTXHelper::Initialize(const uint32_t numHitGroups, const uint32_t numMissGroups, const uint32_t shaderHeaderSize) {
	_ShaderHeaderSize = shaderHeaderSize;
	_NumHitGroups = numHitGroups;
	_NumMissGroups = numMissGroups;

	_NumHitShaders.resize(numHitGroups, 0u);
	_NumMissShaders.resize(numMissGroups, 0u);

	_Stages.clear();
	_Groups.clear();
}

void RTXHelper::Destroy() {
	_NumHitShaders.clear();
	_NumMissShaders.clear();
	_Stages.clear();
	_Groups.clear();

	rtxHelper.Destroy();
}

void RTXHelper::SetRaygenStage(const VkPipelineShaderStageCreateInfo& stage) {
	assert(_Stages.empty());
	_Stages.push_back(stage);

	VkRayTracingShaderGroupCreateInfoNV groupInfo;
	groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	groupInfo.pNext = nullptr;
	groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	groupInfo.generalShader = 0;
	groupInfo.closestHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.anyHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.intersectionShader = VK_SHADER_UNUSED_NV;
	_Groups.push_back(groupInfo); // group 0 is always for raygen
}

void RTXHelper::AddStageToHitGroup(const Array<VkPipelineShaderStageCreateInfo>& stages, const uint32_t groupIndex) {
	assert(!_Stages.empty());
	assert(groupIndex < _NumHitShaders.size());
	assert(!stages.empty() && stages.size() <= 3);
	assert(_NumHitShaders[groupIndex] == 0);
	uint32_t offset = 1; 

	for (uint32_t i = 0; i <= groupIndex; ++i) {
		offset += _NumHitShaders[i];
	}

	auto itStage = _Stages.begin() + offset;
	_Stages.insert(itStage, stages.begin(), stages.end());

	VkRayTracingShaderGroupCreateInfoNV groupInfo;
	groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	groupInfo.pNext = nullptr;
	groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
	groupInfo.generalShader = VK_SHADER_UNUSED_NV;
	groupInfo.closestHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.anyHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.intersectionShader = VK_SHADER_UNUSED_NV;

	for (size_t i = 0; i < stages.size(); i++) {
		const VkPipelineShaderStageCreateInfo& stageInfo = stages[i];
		const uint32_t shaderIdx = static_cast<uint32_t>(offset + i);

		if (stageInfo.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV) {
			groupInfo.closestHitShader = shaderIdx;
		}
		else if (stageInfo.stage == VK_SHADER_STAGE_ANY_HIT_BIT_NV) {
			groupInfo.anyHitShader = shaderIdx;
		}
	};

	_Groups.insert((_Groups.begin() + 1 + groupIndex), groupInfo);

	_NumHitShaders[groupIndex] += static_cast<uint32_t>(stages.size());
}

void RTXHelper::AddStageToMissGroup(const VkPipelineShaderStageCreateInfo& stage, const uint32_t groupIndex) {
	assert(!_Stages.empty());
	assert(groupIndex < _NumMissShaders.size());
	assert(_NumMissShaders[groupIndex] == 0);
	uint32_t offset = 1;
	for (const uint32_t numHitShader : _NumHitShaders) {
		offset += numHitShader;
	}
	for (uint32_t i = 0; i <= groupIndex; ++i) {
		offset += _NumMissShaders[i];
	}

	_Stages.insert(_Stages.begin() + offset, stage);

	VkRayTracingShaderGroupCreateInfoNV groupInfo = {};
	groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	groupInfo.pNext = nullptr;
	groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	groupInfo.generalShader = offset;
	groupInfo.closestHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.anyHitShader = VK_SHADER_UNUSED_NV;
	groupInfo.intersectionShader = VK_SHADER_UNUSED_NV;

	_Groups.insert((_Groups.begin() + (groupIndex + 1 + _NumHitGroups)), groupInfo);

	_NumMissShaders[groupIndex]++;
}

uint32_t RTXHelper::GetGroupsStride() const {
	return _ShaderHeaderSize;
}

uint32_t RTXHelper::GetNu_Groups() const {
	return 1 + _NumHitGroups + _NumMissGroups;
}

uint32_t RTXHelper::GetRaygenOffset() const {
	return 0;
}

uint32_t RTXHelper::GetHitGroupsOffset() const {
	return 1 * _ShaderHeaderSize;
}

uint32_t RTXHelper::GetMissGroupsOffset() const {
	return (1 + _NumHitGroups) * _ShaderHeaderSize;
}

uint32_t RTXHelper::GetNu_Stages() const {
	return static_cast<uint32_t>(_Stages.size());
}

const VkPipelineShaderStageCreateInfo* RTXHelper::GetStages() const {
	return _Stages.data();
}

const VkRayTracingShaderGroupCreateInfoNV* RTXHelper::GetGroups() const {
	return _Groups.data();
}

uint32_t RTXHelper::GetSBTSize() const {
	return GetNu_Groups() * _ShaderHeaderSize;
}

bool RTXHelper::CreateSBT(VkDevice device, VkPipeline rtPipeline) {
	const size_t sbtSize = GetSBTSize();

	VkResult error = rtxHelper.Create(sbtSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	CHECK_VK_ERROR(error, "rtxHelper.Create");

	if (VK_SUCCESS != error) {
		return false;
	}

	void* mem = rtxHelper.Map();
	error = vkGetRayTracingShaderGroupHandlesNV(device, rtPipeline, 0, GetNu_Groups(), sbtSize, mem);
	CHECK_VK_ERROR(error, L"vkGetRaytracingShaderHandleNV");
	rtxHelper.Unmap();

	return (VK_SUCCESS == error);
}

VkBuffer RTXHelper::GetSBTBuffer() const {
	return rtxHelper.GetBuffer();
}

