#pragma once

#include "common/vulkanapp.h"
#include "common/camera.h"

struct RTAccelerationStructure {
	VkDeviceMemory                memory;
	VkAccelerationStructureInfoNV accelerationStructureInfo;
	VkAccelerationStructureNV     accelerationStructure;
	uint64_t                      handle;
};

struct RTMesh {
	uint32_t                    numVertices;
	uint32_t                    numFaces;

	helpers::Buffer       positions;
	helpers::Buffer       attribs;
	helpers::Buffer       indices;
	helpers::Buffer       faces;
	helpers::Buffer       matIDs;

	RTAccelerationStructure     blas;
};

struct RTMaterial {
	helpers::Image        texture;
};

struct RTScene {
	Array<RTMesh>               meshes;
	Array<RTMaterial>           materials;
	RTAccelerationStructure     topLevelAS;

	// shader resources stuff
	Array<VkDescriptorBufferInfo>   matIDsBufferInfos;
	Array<VkDescriptorBufferInfo>   attribsBufferInfos;
	Array<VkDescriptorBufferInfo>   facesBufferInfos;
	Array<VkDescriptorImageInfo>    texturesInfos;
};


class RTXHelper {
public:
	RTXHelper();
	~RTXHelper() = default;

	void        Initialize(const uint32_t numHitGroups, const uint32_t numMissGroups, const uint32_t shaderHeaderSize);
	void        Destroy();
	void        SetRaygenStage(const VkPipelineShaderStageCreateInfo& stage);
	void        AddStageToHitGroup(const Array<VkPipelineShaderStageCreateInfo>& stages, const uint32_t groupIndex);
	void        AddStageToMissGroup(const VkPipelineShaderStageCreateInfo& stage, const uint32_t groupIndex);

	uint32_t    GetGroupsStride() const;
	uint32_t    GetNu_Groups() const;
	uint32_t    GetRaygenOffset() const;
	uint32_t    GetHitGroupsOffset() const;
	uint32_t    GetMissGroupsOffset() const;

	uint32_t                                   GetNu_Stages() const;
	const VkPipelineShaderStageCreateInfo* GetStages() const;
	const VkRayTracingShaderGroupCreateInfoNV* GetGroups() const;

	uint32_t    GetSBTSize() const;
	bool        CreateSBT(VkDevice device, VkPipeline rtPipeline);
	VkBuffer    GetSBTBuffer() const;

private:
	uint32_t                                   _ShaderHeaderSize;
	uint32_t                                   _NumHitGroups;
	uint32_t                                   _NumMissGroups;
	Array<uint32_t>                            _NumHitShaders;
	Array<uint32_t>                            _NumMissShaders;
	Array<VkPipelineShaderStageCreateInfo>     _Stages;
	Array<VkRayTracingShaderGroupCreateInfoNV> _Groups;
	helpers::Buffer                      rtxHelper;
};


class RtxApp : public vulkanapp {
public:
	RtxApp();
	~RtxApp();

protected:
	virtual void InitSettings() override;
	virtual void InitApp() override;
	virtual void FreeResources() override;
	virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, const size_t imageIndex) override;

	virtual void OnMouseMove(const float x, const float y) override;
	virtual void OnMouseButton(const int button, const int action, const int mods) override;
	virtual void OnKey(const int key, const int scancode, const int action, const int mods) override;
	virtual void Update(const size_t imageIndex, const float dt) override;

private:
	bool CreateAS(const VkAccelerationStructureTypeNV type,
		const uint32_t geometryCount,
		const VkGeometryNV* geometries,
		const uint32_t instanceCount,
		RTAccelerationStructure& _as);
	void LoadSceneGeometry();
	void CreateScene();
	void CreateCamera();
	void UpdateCameraParams(struct UniformParams* params, const float dt);
	void CreateDescriptorSetsLayouts();
	void CreateRaytracingPipelineAndSBT();
	void UpdateDescriptorSets();

private:
	Array<VkDescriptorSetLayout>    _RTXDescriptorSetsLayouts;
	VkPipelineLayout                _RTXPipelineLayout;
	VkPipeline                      _RTXPipeline;
	VkDescriptorPool                _RTXDescriptorPool;
	Array<VkDescriptorSet>          _RTXDescriptorSets;

	RTXHelper                       rtxHelper;

	RTScene                         _Scene;

	Camera                          _Camera;
	helpers::Buffer           _CameraBuffer;
	bool                            WKeyDown;
	bool                            AKeyDown;
	bool                            SKeyDown;
	bool                            DKeyDown;
	bool                            ShiftDown;
	bool                            LMBDown;
	vec2                            CursorPos;
};
