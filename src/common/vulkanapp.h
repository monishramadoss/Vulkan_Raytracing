#include "vk_helpers.h"
#include "GLFW/glfw3.h"
#include "utils.h"


struct settings {
	std::string name;
	uint32_t    resolutionX;
	uint32_t    resolutionY;
	VkFormat    surfaceFormat;
	bool        enableValidation;
	bool        enableVSync;
	bool        supportRaytracing;
	bool        supportDescriptorIndexing;
};

struct FPSMeter {
	static const size_t kFPSHistorySize = 128;

	float   fpsHistory[kFPSHistorySize] = { 0.0f };
	size_t  historyPointer = 0;
	float   fpsAccumulator = 0.0f;
	float   fps = 0.0f;

	void    Update(const float dt);
	float   GetFPS() const;
	float   GetFrameTime() const;
};

class vulkanapp {
public:
	vulkanapp();
	virtual ~vulkanapp();

	void    Run();

protected:
	bool    Initialize();
	void    Loop();
	void    Shutdown();

	void    InitializeSettings();
	bool    InitializeVulkan();
	bool    InitializeDevicesAndQueues();
	bool    InitializeSurface();
	bool    InitializeSwapchain();
	bool    InitializeFencesAndCommandPool();
	bool    InitializeOffscreenImage();
	bool    InitializeCommandBuffers();
	bool    InitializeSynchronization();
	void    FillCommandBuffers();

	//
	void    ProcessFrame(const float dt);
	void    FreeVulkan();

	// to be overriden by subclasses
	virtual void InitSettings();
	virtual void InitApp();
	virtual void FreeResources();
	virtual void FillCommandBuffer(VkCommandBuffer commandBuffer, const size_t imageIndex);

	virtual void OnMouseMove(const float x, const float y);
	virtual void OnMouseButton(const int button, const int action, const int mods);
	virtual void OnKey(const int key, const int scancode, const int action, const int mods);
	virtual void Update(const size_t imageIndex, const float dt);

protected:
	settings             _Settings;
	GLFWwindow* _Window;

	VkInstance              _Instance;
	VkPhysicalDevice        _PhysicalDevice;
	VkDevice                _Device;
	VkSurfaceFormatKHR      _SurfaceFormat;
	VkSurfaceKHR            _Surface;
	VkSwapchainKHR          _Swapchain;
	Array<VkImage>          _SwapchainImages;
	Array<VkImageView>      _SwapchainImageViews;
	Array<VkFence>          _WaitForFrameFences;
	VkCommandPool           _CommandPool;
	helpers::Image    _OffscreenImage;
	Array<VkCommandBuffer>  _CommandBuffers;
	VkSemaphore             _SemaphoreImageAcquired;
	VkSemaphore             _SemaphoreRenderFinished;

	uint32_t                _GraphicsQueueFamilyIndex;
	uint32_t                _ComputeQueueFamilyIndex;
	uint32_t                _TransferQueueFamilyIndex;
	VkQueue                 _GraphicsQueue;
	VkQueue                 _ComputeQueue;
	VkQueue                 _TransferQueue;

	// RTX stuff
	VkPhysicalDeviceRayTracingPropertiesNV _RTXProps;

	// FPS meter
	FPSMeter                fpsMeter;
};
