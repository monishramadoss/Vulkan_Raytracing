#include "vulkanapp.h"

// include volk.c for implementation
#include "volk.c"


void FPSMeter::Update(const float dt) {
	fpsAccumulator += dt - fpsHistory[historyPointer];
	fpsHistory[historyPointer] = dt;
	historyPointer = (historyPointer + 1) % FPSMeter::kFPSHistorySize;
	fps = (fpsAccumulator > 0.0f) ? (1.0f / (fpsAccumulator / static_cast<float>(FPSMeter::kFPSHistorySize))) : FLT_MAX;
}

float FPSMeter::GetFPS() const {
	return fps;
}

float FPSMeter::GetFrameTime() const {
	return 1000.0f / fps;
}



vulkanapp::vulkanapp()
	: _Settings({})
	, _Window(nullptr)
	, _Instance(VK_NULL_HANDLE)
	, _PhysicalDevice(VK_NULL_HANDLE)
	, _Device(VK_NULL_HANDLE)
	, _SurfaceFormat({})
	, _Surface(VK_NULL_HANDLE)
	, _Swapchain(VK_NULL_HANDLE)
	, _CommandPool(VK_NULL_HANDLE)
	, _SemaphoreImageAcquired(VK_NULL_HANDLE)
	, _SemaphoreRenderFinished(VK_NULL_HANDLE)
	, _GraphicsQueueFamilyIndex(0u)
	, _ComputeQueueFamilyIndex(0u)
	, _TransferQueueFamilyIndex(0u)
	, _GraphicsQueue(VK_NULL_HANDLE)
	, _ComputeQueue(VK_NULL_HANDLE)
	, _TransferQueue(VK_NULL_HANDLE)
{

}
vulkanapp::~vulkanapp() {
	FreeVulkan();
}

void vulkanapp::Run() {
	if (Initialize()) {
		Loop();
		Shutdown();
		FreeResources();
	}
}

bool vulkanapp::Initialize() {
	if (!glfwInit()) {
		return false;
	}

	if (!glfwVulkanSupported()) {
		return false;
	}

	if (VK_SUCCESS != volkInitialize()) {
		return false;
	}

	InitializeSettings();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(static_cast<int>(_Settings.resolutionX),
		static_cast<int>(_Settings.resolutionY),
		_Settings.name.c_str(),
		nullptr, nullptr);
	if (!window) {
		return false;
	}

	glfwSetWindowUserPointer(window, this);

	glfwSetKeyCallback(window, [](GLFWwindow* wnd, int key, int scancode, int action, int mods) {
		vulkanapp* app = reinterpret_cast<vulkanapp*>(glfwGetWindowUserPointer(wnd));
		app->OnKey(key, scancode, action, mods);
		});
	glfwSetMouseButtonCallback(window, [](GLFWwindow* wnd, int button, int action, int mods) {
		vulkanapp* app = reinterpret_cast<vulkanapp*>(glfwGetWindowUserPointer(wnd));
		app->OnMouseButton(button, action, mods);
		});
	glfwSetCursorPosCallback(window, [](GLFWwindow* wnd, double x, double y) {
		vulkanapp* app = reinterpret_cast<vulkanapp*>(glfwGetWindowUserPointer(wnd));
		app->OnMouseMove(static_cast<float>(x), static_cast<float>(y));
		});

	_Window = window;

	if (!InitializeVulkan()) {
		return false;
	}

	volkLoadInstance(_Instance);

	if (!InitializeDevicesAndQueues()) {
		return false;
	}
	if (!InitializeSurface()) {
		return false;
	}
	if (!InitializeSwapchain()) {
		return false;
	}
	if (!InitializeFencesAndCommandPool()) {
		return false;
	}

	helpers::Initialize(_PhysicalDevice, _Device, _CommandPool, _GraphicsQueue);

	if (!InitializeOffscreenImage()) {
		return false;
	}
	if (!InitializeCommandBuffers()) {
		return false;
	}
	if (!InitializeSynchronization()) {
		return false;
	}

	InitApp();
	FillCommandBuffers();

	return true;
}

void vulkanapp::Loop() {
	glfwSetTime(0.0);
	double curTime, prevTime = 0.0, deltaTime = 0.0;
	while (!glfwWindowShouldClose(_Window)) {
		curTime = glfwGetTime();
		deltaTime = curTime - prevTime;
		prevTime = curTime;

		ProcessFrame(static_cast<float>(deltaTime));

		glfwPollEvents();
	}
}

void vulkanapp::Shutdown() {
	vkDeviceWaitIdle(_Device);

	glfwTerminate();
}

void vulkanapp::InitializeSettings() {
	_Settings.name = "vulkanapp";
	_Settings.resolutionX = 1280;
	_Settings.resolutionY = 720;
	_Settings.surfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;
	_Settings.enableValidation = false;
	_Settings.enableVSync = true;
	_Settings.supportRaytracing = true;
	_Settings.supportDescriptorIndexing = false;

	InitSettings();
}

bool vulkanapp::InitializeVulkan() {
	VkApplicationInfo appInfo;
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = nullptr;
	appInfo.pApplicationName = _Settings.name.c_str();
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "vulkanapp";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	uint32_t requiredExtensionsCount = 0;
	const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionsCount);

	Array<const char*> extensions;
	Array<const char*> layers;

	extensions.insert(extensions.begin(), requiredExtensions, requiredExtensions + requiredExtensionsCount);

	if (_Settings.enableValidation) {
		extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		layers.push_back("VK_LAYER_LUNARG_standard_validation");
	}

	VkInstanceCreateInfo instInfo;
	instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instInfo.pNext = nullptr;
	instInfo.flags = 0;
	instInfo.pApplicationInfo = &appInfo;
	instInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	instInfo.ppEnabledExtensionNames = extensions.data();
	instInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
	instInfo.ppEnabledLayerNames = layers.data();

	VkResult error = vkCreateInstance(&instInfo, nullptr, &_Instance);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkCreateInstance");
		return false;
	}

	return true;
}

bool vulkanapp::InitializeDevicesAndQueues() {
	uint32_t numPhyDevices = 0;
	VkResult error = vkEnumeratePhysicalDevices(_Instance, &numPhyDevices, nullptr);
	if (VK_SUCCESS != error || !numPhyDevices) {
		CHECK_VK_ERROR(error, "vkEnumeratePhysicalDevices");
		return false;
	}

	Array<VkPhysicalDevice> phyDevices(numPhyDevices);
	vkEnumeratePhysicalDevices(_Instance, &numPhyDevices, phyDevices.data());
	_PhysicalDevice = phyDevices[0];

	// find our queues
	const VkQueueFlagBits askingFlags[3] = { VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT };
	uint32_t queuesIndices[3] = { ~0u, ~0u, ~0u };

	uint32_t queueFamilyPropertyCount;
	vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyPropertyCount, nullptr);
	Array<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());

	for (size_t i = 0; i < 3; ++i) {
		const VkQueueFlagBits flag = askingFlags[i];
		uint32_t& queueIdx = queuesIndices[i];

		if (flag == VK_QUEUE_COMPUTE_BIT) {
			for (uint32_t j = 0; j < queueFamilyPropertyCount; ++j) {
				if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
					!(queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
					queueIdx = j;
					break;
				}
			}
		}
		else if (flag == VK_QUEUE_TRANSFER_BIT) {
			for (uint32_t j = 0; j < queueFamilyPropertyCount; ++j) {
				if ((queueFamilyProperties[j].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
					!(queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
					!(queueFamilyProperties[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
					queueIdx = j;
					break;
				}
			}
		}

		if (queueIdx == ~0u) {
			for (uint32_t j = 0; j < queueFamilyPropertyCount; ++j) {
				if (queueFamilyProperties[j].queueFlags & flag) {
					queueIdx = j;
					break;
				}
			}
		}
	}

	_GraphicsQueueFamilyIndex = queuesIndices[0];
	_ComputeQueueFamilyIndex = queuesIndices[1];
	_TransferQueueFamilyIndex = queuesIndices[2];

	// create device
	Array<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;
	const float priority = 0.0f;

	VkDeviceQueueCreateInfo deviceQueueCreateInfo;
	deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	deviceQueueCreateInfo.pNext = nullptr;
	deviceQueueCreateInfo.flags = 0;
	deviceQueueCreateInfo.queueFamilyIndex = _GraphicsQueueFamilyIndex;
	deviceQueueCreateInfo.queueCount = 1;
	deviceQueueCreateInfo.pQueuePriorities = &priority;
	deviceQueueCreateInfos.push_back(deviceQueueCreateInfo);

	if (_ComputeQueueFamilyIndex != _GraphicsQueueFamilyIndex) {
		deviceQueueCreateInfo.queueFamilyIndex = _ComputeQueueFamilyIndex;
		deviceQueueCreateInfos.push_back(deviceQueueCreateInfo);
	}
	if (_TransferQueueFamilyIndex != _GraphicsQueueFamilyIndex && _TransferQueueFamilyIndex != _ComputeQueueFamilyIndex) {
		deviceQueueCreateInfo.queueFamilyIndex = _TransferQueueFamilyIndex;
		deviceQueueCreateInfos.push_back(deviceQueueCreateInfo);
	}

	Array<const char*> deviceExtensions({ VK_KHR_SWAPCHAIN_EXTENSION_NAME });
	if (_Settings.supportRaytracing) {
		deviceExtensions.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
	}

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexing = { };
	descriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;

	VkPhysicalDeviceFeatures2 features2 = { };
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

	if (_Settings.supportDescriptorIndexing) {
		deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		features2.pNext = &descriptorIndexing;
	}

	vkGetPhysicalDeviceFeatures2(_PhysicalDevice, &features2); // enable all the features our GPU has

	VkDeviceCreateInfo deviceCreateInfo;
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = &features2;
	deviceCreateInfo.flags = 0;
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfos.size());
	deviceCreateInfo.pQueueCreateInfos = deviceQueueCreateInfos.data();
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = nullptr;
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	deviceCreateInfo.pEnabledFeatures = nullptr;

	error = vkCreateDevice(_PhysicalDevice, &deviceCreateInfo, nullptr, &_Device);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "vkCreateDevice");
		return false;
	}

	// get our queues handles
	vkGetDeviceQueue(_Device, _GraphicsQueueFamilyIndex, 0, &_GraphicsQueue);
	vkGetDeviceQueue(_Device, _ComputeQueueFamilyIndex, 0, &_ComputeQueue);
	vkGetDeviceQueue(_Device, _TransferQueueFamilyIndex, 0, &_TransferQueue);

	// if raytracing support requested - let's get raytracing properties to know shader header size and max recursion
	if (_Settings.supportRaytracing) {
		_RTXProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
		_RTXProps.pNext = nullptr;
		_RTXProps.maxRecursionDepth = 0;
		_RTXProps.shaderGroupHandleSize = 0;

		VkPhysicalDeviceProperties2 devProps;
		devProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		devProps.pNext = &_RTXProps;
		devProps.properties = { };

		vkGetPhysicalDeviceProperties2(_PhysicalDevice, &devProps);
	}

	return true;
}

bool vulkanapp::InitializeSurface() {
	VkResult error = glfwCreateWindowSurface(_Instance, _Window, nullptr, &_Surface);
	if (VK_SUCCESS != error) {
		CHECK_VK_ERROR(error, "glfwCreateWindowSurface");
		return false;
	}

	VkBool32 supportPresent = VK_FALSE;
	error = vkGetPhysicalDeviceSurfaceSupportKHR(_PhysicalDevice, _GraphicsQueueFamilyIndex, _Surface, &supportPresent);
	if (VK_SUCCESS != error || !supportPresent) {
		CHECK_VK_ERROR(error, "vkGetPhysicalDeviceSurfaceSupportKHR");
		return false;
	}

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(_PhysicalDevice, _Surface, &formatCount, nullptr);
	Array<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(_PhysicalDevice, _Surface, &formatCount, surfaceFormats.data());

	if (formatCount == 1 && surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
		_SurfaceFormat.format = _Settings.surfaceFormat;
		_SurfaceFormat.colorSpace = surfaceFormats[0].colorSpace;
	}
	else {
		bool found = false;
		for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats) {
			if (surfaceFormat.format == _Settings.surfaceFormat) {
				_SurfaceFormat = surfaceFormat;
				found = true;
				break;
			}
		}
		if (!found) {
			_SurfaceFormat = surfaceFormats[0];
		}
	}

	return true;
}

bool vulkanapp::InitializeSwapchain() {
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VkResult error = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_PhysicalDevice, _Surface, &surfaceCapabilities);
	if (VK_SUCCESS != error) {
		return false;
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(_PhysicalDevice, _Surface, &presentModeCount, nullptr);
	Array<VkPresentModeKHR> presentModes(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(_PhysicalDevice, _Surface, &presentModeCount, presentModes.data());

	// trying to find best present mode for us
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!_Settings.enableVSync) {
		// if we don't want vsync - let's find best one
		for (const VkPresentModeKHR mode : presentModes) {
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				// this is the best one, so if we found it - just quit
				presentMode = mode;
				break;
			}
			else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				// we'll use this one if no mailbox supported
				presentMode = mode;
			}
		}
	}

	VkSwapchainKHR prevSwapchain = _Swapchain;

	VkSwapchainCreateInfoKHR swapchainCreateInfo;
	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.pNext = nullptr;
	swapchainCreateInfo.flags = 0;
	swapchainCreateInfo.surface = _Surface;
	swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
	swapchainCreateInfo.imageFormat = _SurfaceFormat.format;
	swapchainCreateInfo.imageColorSpace = _SurfaceFormat.colorSpace;
	swapchainCreateInfo.imageExtent = {_Settings.resolutionX, _Settings.resolutionY };
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.pQueueFamilyIndices = nullptr;
	swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = presentMode;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = prevSwapchain;

	error = vkCreateSwapchainKHR(_Device, &swapchainCreateInfo, nullptr, &_Swapchain);
	if (VK_SUCCESS != error) {
		return false;
	}

	if (prevSwapchain) {
		for (VkImageView& imageView : _SwapchainImageViews) {
			vkDestroyImageView(_Device, imageView, nullptr);
			imageView = VK_NULL_HANDLE;
		}
		vkDestroySwapchainKHR(_Device, prevSwapchain, nullptr);
	}

	uint32_t imageCount;
	vkGetSwapchainImagesKHR(_Device, _Swapchain, &imageCount, nullptr);
	_SwapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(_Device, _Swapchain, &imageCount, _SwapchainImages.data());

	_SwapchainImageViews.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i) {
		VkImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.pNext = nullptr;
		imageViewCreateInfo.format = _SurfaceFormat.format;
		imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.image = _SwapchainImages[i];
		imageViewCreateInfo.flags = 0;
		imageViewCreateInfo.components = { };

		error = vkCreateImageView(_Device, &imageViewCreateInfo, nullptr, &_SwapchainImageViews[i]);
		if (VK_SUCCESS != error) {
			return false;
		}
	}

	return true;
}

bool vulkanapp::InitializeFencesAndCommandPool() {
	VkFenceCreateInfo fenceCreateInfo;
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	_WaitForFrameFences.resize(_SwapchainImages.size());
	for (VkFence& fence : _WaitForFrameFences) {
		vkCreateFence(_Device, &fenceCreateInfo, nullptr, &fence);
	}

	VkCommandPoolCreateInfo commandPoolCreateInfo;
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = nullptr;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = _GraphicsQueueFamilyIndex;

	const VkResult error = vkCreateCommandPool(_Device, &commandPoolCreateInfo, nullptr, &_CommandPool);
	return (VK_SUCCESS == error);
}

bool vulkanapp::InitializeOffscreenImage() {
	const VkExtent3D extent = { _Settings.resolutionX, _Settings.resolutionY, 1 };
	VkResult error = _OffscreenImage.Create(VK_IMAGE_TYPE_2D,
		_SurfaceFormat.format,
		extent,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (VK_SUCCESS != error) {
		return false;
	}

	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	error = _OffscreenImage.CreateImageView(VK_IMAGE_VIEW_TYPE_2D, _SurfaceFormat.format, range);
	return (VK_SUCCESS == error);
}

bool vulkanapp::InitializeCommandBuffers() {
	_CommandBuffers.resize(_SwapchainImages.size());

	VkCommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = nullptr;
	commandBufferAllocateInfo.commandPool = _CommandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = static_cast<uint32_t>(_CommandBuffers.size());

	const VkResult error = vkAllocateCommandBuffers(_Device, &commandBufferAllocateInfo, _CommandBuffers.data());
	return (VK_SUCCESS == error);
}

bool vulkanapp::InitializeSynchronization() {
	VkSemaphoreCreateInfo semaphoreCreatInfo;
	semaphoreCreatInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreatInfo.pNext = nullptr;
	semaphoreCreatInfo.flags = 0;

	VkResult error = vkCreateSemaphore(_Device, &semaphoreCreatInfo, nullptr, &_SemaphoreImageAcquired);
	if (VK_SUCCESS != error) {
		return false;
	}

	error = vkCreateSemaphore(_Device, &semaphoreCreatInfo, nullptr, &_SemaphoreRenderFinished);
	return (VK_SUCCESS == error);
}

void vulkanapp::FillCommandBuffers() {
	VkCommandBufferBeginInfo commandBufferBeginInfo;
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBufferBeginInfo.pNext = nullptr;
	commandBufferBeginInfo.flags = 0;
	commandBufferBeginInfo.pInheritanceInfo = nullptr;

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	for (size_t i = 0; i < _CommandBuffers.size(); i++) {
		const VkCommandBuffer commandBuffer = _CommandBuffers[i];

		VkResult error = vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
		CHECK_VK_ERROR(error, "vkBeginCommandBuffer");

		helpers::ImageBarrier(commandBuffer,
			_OffscreenImage.GetImage(),
			subresourceRange,
			0,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);

		FillCommandBuffer(commandBuffer, i); // user draw code

		helpers::ImageBarrier(commandBuffer,
			_SwapchainImages[i],
			subresourceRange,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		helpers::ImageBarrier(commandBuffer,
			_OffscreenImage.GetImage(),
			subresourceRange,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageCopy copyRegion;
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.srcOffset = { 0, 0, 0 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstOffset = { 0, 0, 0 };
		copyRegion.extent = { _Settings.resolutionX, _Settings.resolutionY, 1 };
		vkCmdCopyImage(commandBuffer,
			_OffscreenImage.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			_SwapchainImages[i],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion);

		helpers::ImageBarrier(commandBuffer,
			_SwapchainImages[i], subresourceRange,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			0,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		error = vkEndCommandBuffer(commandBuffer);
		CHECK_VK_ERROR(error, "vkEndCommandBuffer");
	}
}


//
void vulkanapp::ProcessFrame(const float dt) {
	fpsMeter.Update(dt);

	uint32_t imageIndex;
	VkResult error = vkAcquireNextImageKHR(_Device, _Swapchain, UINT64_MAX, _SemaphoreImageAcquired, VK_NULL_HANDLE, &imageIndex);
	if (VK_SUCCESS != error) {
		return;
	}

	const VkFence fence = _WaitForFrameFences[imageIndex];
	error = vkWaitForFences(_Device, 1, &fence, VK_TRUE, UINT64_MAX);
	if (VK_SUCCESS != error) {
		return;
	}
	vkResetFences(_Device, 1, &fence);

	Update(imageIndex, dt);

	const VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submitInfo;
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &_SemaphoreImageAcquired;
	submitInfo.pWaitDstStageMask = &waitStageMask;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &_CommandBuffers[imageIndex];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &_SemaphoreRenderFinished;

	error = vkQueueSubmit(_GraphicsQueue, 1, &submitInfo, fence);
	if (VK_SUCCESS != error) {
		return;
	}

	VkPresentInfoKHR presentInfo;
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &_SemaphoreRenderFinished;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &_Swapchain;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr;

	error = vkQueuePresentKHR(_GraphicsQueue, &presentInfo);
	if (VK_SUCCESS != error) {
		return;
	}
}

void vulkanapp::FreeVulkan() {
	if (_SemaphoreRenderFinished) {
		vkDestroySemaphore(_Device, _SemaphoreRenderFinished, nullptr);
		_SemaphoreRenderFinished = VK_NULL_HANDLE;
	}

	if (_SemaphoreImageAcquired) {
		vkDestroySemaphore(_Device, _SemaphoreImageAcquired, nullptr);
		_SemaphoreImageAcquired = VK_NULL_HANDLE;
	}

	if (!_CommandBuffers.empty()) {
		vkFreeCommandBuffers(_Device, _CommandPool, static_cast<uint32_t>(_CommandBuffers.size()), _CommandBuffers.data());
		_CommandBuffers.clear();
	}

	if (_CommandPool) {
		vkDestroyCommandPool(_Device, _CommandPool, nullptr);
		_CommandPool = VK_NULL_HANDLE;
	}

	for (VkFence& fence : _WaitForFrameFences) {
		vkDestroyFence(_Device, fence, nullptr);
	}
	_WaitForFrameFences.clear();

	_OffscreenImage.Destroy();

	for (VkImageView& view : _SwapchainImageViews) {
		vkDestroyImageView(_Device, view, nullptr);
	}
	_SwapchainImageViews.clear();
	_SwapchainImages.clear();

	if (_Swapchain) {
		vkDestroySwapchainKHR(_Device, _Swapchain, nullptr);
		_Swapchain = VK_NULL_HANDLE;
	}

	if (_Surface) {
		vkDestroySurfaceKHR(_Instance, _Surface, nullptr);
		_Surface = VK_NULL_HANDLE;
	}

	if (_Device) {
		vkDestroyDevice(_Device, nullptr);
		_Device = VK_NULL_HANDLE;
	}

	if (_Instance) {
		vkDestroyInstance(_Instance, nullptr);
		_Instance = VK_NULL_HANDLE;
	}
}

void vulkanapp::InitSettings() {}
void vulkanapp::InitApp() {}
void vulkanapp::FreeResources() {}
void vulkanapp::FillCommandBuffer(VkCommandBuffer, const size_t) {}
void vulkanapp::OnMouseMove(const float, const float) {}
void vulkanapp::OnMouseButton(const int, const int, const int) {}
void vulkanapp::OnKey(const int, const int, const int, const int) {}
void vulkanapp::Update(const size_t, const float) {}
