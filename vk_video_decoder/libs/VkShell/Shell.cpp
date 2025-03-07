/*
 * Copyright (C) 2016 Google, Inc.
 * Copyright 2020 NVIDIA Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <array>
#include <iostream>
#include <string>
#include <sstream>
#include <set>
#include "VkCodecUtils/Helpers.h"
#include "Shell.h"

#include "FrameProcessor.h"

Shell::Shell(FrameProcessor &frameProcessor)
    : frameProcessor_(frameProcessor),
      settings_(frameProcessor.settings()),
      ctx_(), frameProcessor_tick_(1.0f / settings_.ticks_per_second),
      frameProcessor_time_(frameProcessor_tick_) {
    // require generic WSI extensions
    instance_extensions_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#if defined(__linux) || defined(__linux__) || defined(linux)
    device_extensions_.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    device_extensions_.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
#endif
#if !defined(VK_USE_PLATFORM_WIN32_KHR)
    device_extensions_.push_back(VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME);
#endif
    if (frameProcessor.requires_vulkan_video()) {
        device_extensions_.push_back(VK_EXT_YCBCR_2PLANE_444_FORMATS_EXTENSION_NAME);
        device_extensions_.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        device_extensions_.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        device_extensions_.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
    }

    if (settings_.validate) {
        instance_extensions_.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
}

Shell::AcquireBuffer::AcquireBuffer()
    : semaphore_(VkSemaphore(0)),
      fence_(VkFence(0)),
      dev_(VkDevice(0))
{
}

Shell::AcquireBuffer::~AcquireBuffer()
{
    if (semaphore_) {
        vk::DestroySemaphore(dev_, semaphore_, nullptr);
        semaphore_ = VkSemaphore(0);
    }

    if (fence_) {
        vk::DestroyFence(dev_, fence_, nullptr);
        fence_ = VkFence(0);
    }
}

VkResult Shell::AcquireBuffer::Create(VkDevice dev)
{
    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    // Fence for vkAcquireNextImageKHR must be unsignaled

    dev_ = dev;
    vk::assert_success(vk::CreateSemaphore(dev_, &sem_info, nullptr, &semaphore_));
    vk::assert_success(vk::CreateFence(dev_, &fence_info, nullptr, &fence_));

    return VK_SUCCESS;
}

Shell::BackBuffer::BackBuffer()
    : imageIndex_(0),
      acquireBuffer_(nullptr),
      renderSemaphore_(VkSemaphore (0)),
      state_(BACK_BUFFER_INIT),
      dev_(VkDevice(0))
{
}

VkResult Shell::BackBuffer::Create(VkDevice dev)
{
    VkSemaphoreCreateInfo sem_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    dev_ = dev;
    vk::assert_success(vk::CreateSemaphore(dev_, &sem_info, nullptr, &renderSemaphore_));
    return VK_SUCCESS;
}

Shell::BackBuffer::~BackBuffer()
{
    if (renderSemaphore_) {
        vk::DestroySemaphore(dev_, renderSemaphore_, nullptr);
        renderSemaphore_ = VkSemaphore(0);
    }

    if (acquireBuffer_) {
        delete acquireBuffer_;
        acquireBuffer_ = nullptr;
    }
}

void Shell::log(LogPriority priority, const char *msg) {
    std::ostream &st = (priority >= LOG_ERR) ? std::cerr : std::cout;
    st << msg << "\n";
}

void Shell::init_vk(uint32_t deviceID) {
    vk::init_dispatch_table_top(load_vk());

    init_instance();
    vk::init_dispatch_table_middle(ctx_.instance, false);

    init_debug_report();
    init_physical_dev(deviceID);
}

void Shell::cleanup_vk() {
    if (settings_.validate) vk::DestroyDebugReportCallbackEXT(ctx_.instance, ctx_.debug_report, nullptr);

    vk::DestroyInstance(ctx_.instance, nullptr);
}

bool Shell::debug_report_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT obj_type, uint64_t object,
                                  size_t location, int32_t msg_code, const char *layer_prefix, const char *msg) {
    LogPriority prio = LOG_WARN;
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        prio = LOG_ERR;
    else if (flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT))
        prio = LOG_WARN;
    else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
        prio = LOG_INFO;
    else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
        prio = LOG_DEBUG;

    std::stringstream ss;
    ss << layer_prefix << ": " << msg;

    log(prio, ss.str().c_str());

    return false;
}

void Shell::assert_all_instance_layers() const {
    // enumerate instance layer
    std::vector<VkLayerProperties> layers;
    vk::enumerate(layers);

    std::cout << "Enumerating instance layers:" << std::endl;
    std::set<std::string> layer_names;
    for (const auto &layer : layers) {
        layer_names.insert(layer.layerName);
        std::cout << '\t' << layer.layerName << std::endl;
    }

    // all listed instance layers are required
    std::cout << "Looking for instance layers:" << std::endl;
    for (const auto &name : instance_layers_) {
        std::cout << '\t' << name << std::endl;
        if (layer_names.find(name) == layer_names.end()) {
            std::stringstream ss;
            ss << "instance layer " << name << " is missing";
            throw std::runtime_error(ss.str());
        }
    }
}

void Shell::assert_all_instance_extensions() const {
    // enumerate instance extensions
    std::vector<VkExtensionProperties> exts;
    vk::enumerate(nullptr, exts);

    std::cout << "Enumerating instance extensions:" << std::endl;
    std::set<std::string> ext_names;
    for (const auto &ext : exts) {
        ext_names.insert(ext.extensionName);
        std::cout << '\t' <<  ext.extensionName << std::endl;
    }

    // all listed instance extensions are required
    std::cout << "Looking for instance extensions:" << std::endl;
    for (const auto &name : instance_extensions_) {
        std::cout << '\t' <<  name << std::endl;
        if (ext_names.find(name) == ext_names.end()) {
            std::stringstream ss;
            ss << "instance extension " << name << " is missing";
            throw std::runtime_error(ss.str());
        }
    }
}

bool Shell::has_all_device_extensions(VkPhysicalDevice phy) const {
    // enumerate device extensions
    std::vector<VkExtensionProperties> exts;
    vk::enumerate(phy, nullptr, exts);

    std::set<std::string> ext_names;
    for (const auto &ext : exts) ext_names.insert(ext.extensionName);

    // all listed device extensions are required
    for (const auto &name : device_extensions_) {
        if (ext_names.find(name) == ext_names.end()) return false;
    }

    return true;
}

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
#include <link.h>
int DumpSoLibs()
{
    using UnknownStruct = struct unknown_struct {
       void*  pointers[3];
       struct unknown_struct* ptr;
    };
    using LinkMap = struct link_map;

    auto* handle = dlopen(NULL, RTLD_NOW);
    auto* p = reinterpret_cast<UnknownStruct*>(handle)->ptr;
    auto* map = reinterpret_cast<LinkMap*>(p->ptr);

    while (map) {
      std::cout << map->l_name << std::endl;
      // do something with |map| like with handle, returned by |dlopen()|.
      map = map->l_next;
    }

    return 0;
}
#endif

void Shell::init_instance() {
    assert_all_instance_layers();
    assert_all_instance_extensions();

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = settings_.name.c_str();
    app_info.applicationVersion = 0;
    app_info.apiVersion = VK_HEADER_VERSION_COMPLETE;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledLayerCount = static_cast<uint32_t>(instance_layers_.size());
    instance_info.ppEnabledLayerNames = instance_layers_.data();
    instance_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions_.size());
    instance_info.ppEnabledExtensionNames = instance_extensions_.data();

    vk::assert_success(vk::CreateInstance(&instance_info, nullptr, &ctx_.instance));

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
    DumpSoLibs();
#endif
}

void Shell::init_debug_report() {
    if (!settings_.validate) return;

    VkDebugReportCallbackCreateInfoEXT debug_report_info = {};
    debug_report_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;

    debug_report_info.flags =
        VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
    if (settings_.validate_verbose) {
        debug_report_info.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    }

    debug_report_info.pfnCallback = debug_report_callback;
    debug_report_info.pUserData = reinterpret_cast<void *>(this);

    vk::assert_success(vk::CreateDebugReportCallbackEXT(ctx_.instance, &debug_report_info, nullptr, &ctx_.debug_report));
}

void Shell::init_physical_dev(uint32_t deviceID) {
    // enumerate physical devices
    std::vector<VkPhysicalDevice> phys;
    vk::assert_success(vk::enumerate(ctx_.instance, phys));

    ctx_.physical_dev = VK_NULL_HANDLE;
    for (auto phy : phys) {

        VkPhysicalDeviceProperties props;
        vk::GetPhysicalDeviceProperties(phy, &props);
        if (deviceID && (props.deviceID != deviceID)) {
            continue;
        }

        if (!has_all_device_extensions(phy)) {
            continue;
        }

        // get queue properties
        std::vector<VkQueueFamilyProperties2> queues;
        std::vector<VkVideoQueueFamilyProperties2KHR> videoQueues;
        vk::get(phy, queues, videoQueues);

        int frameProcessor_queue_family = -1, present_queue_family = -1, video_decode_queue_family = -1;
        for (uint32_t i = 0; i < queues.size(); i++) {
            const VkQueueFamilyProperties2 &q = queues[i];
            const VkVideoQueueFamilyProperties2KHR &videoQueue = videoQueues[i];

            // requires only GRAPHICS for frameProcessor queues
            const VkFlags frameProcessor_queue_flags = VK_QUEUE_GRAPHICS_BIT;
            if ((frameProcessor_queue_family < 0) &&
                    (q.queueFamilyProperties.queueFlags & frameProcessor_queue_flags)) {
                frameProcessor_queue_family = i;
            }

            if (frameProcessor_.requires_vulkan_video()) {
                const VkFlags video_decode_queue_flags = VK_QUEUE_VIDEO_DECODE_BIT_KHR;
                const VkVideoCodecOperationFlagsKHR suported_video_decode_queue_operations =
                        VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_EXT |
                        VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_EXT;
                if ((video_decode_queue_family < 0) &&
                        (q.queueFamilyProperties.queueFlags & video_decode_queue_flags) &&
                        (videoQueue.videoCodecOperations & suported_video_decode_queue_operations)) {
                    video_decode_queue_family = i;
                }
            }

            // present queue must support the surface
            if ((present_queue_family < 0) && can_present(phy, i)) {
                present_queue_family = i;
            }

            if ((frameProcessor_queue_family >= 0) && (present_queue_family >= 0) && (video_decode_queue_family >= 0)) {
                break;
            }
        }

        if ((frameProcessor_queue_family >= 0) && (present_queue_family >= 0) && (video_decode_queue_family >= 0)) {
            ctx_.physical_dev = phy;
            ctx_.frameProcessor_queue_family = frameProcessor_queue_family;
            ctx_.present_queue_family = present_queue_family;
            ctx_.video_decode_queue_family = video_decode_queue_family;
            break;
        }
    }

    if (ctx_.physical_dev == VK_NULL_HANDLE) throw std::runtime_error("failed to find any capable Vulkan physical device");
}

void Shell::create_context() {
    create_dev();
    vk::init_dispatch_table_bottom(ctx_.instance, ctx_.dev);

    ctx_.currentBackBuffer_ = 0;
    ctx_.acquiredFrameId = 0;

    vk::GetDeviceQueue(ctx_.dev, ctx_.frameProcessor_queue_family, 0, &ctx_.frameProcessor_queue);
    vk::GetDeviceQueue(ctx_.dev, ctx_.present_queue_family, 0, &ctx_.present_queue);
    if (ctx_.video_decode_queue_family != (uint32_t)-1) {
        vk::GetDeviceQueue(ctx_.dev, ctx_.video_decode_queue_family, 0, &ctx_.video_queue);
    }
    create_back_buffers();

    // initialize ctx_.{surface,format} before attach_shell
    create_swapchain();

    frameProcessor_.attach_shell(*this);
}

void Shell::destroy_context() {
    if (ctx_.dev == VK_NULL_HANDLE) return;

    vk::DeviceWaitIdle(ctx_.dev);

    destroy_swapchain();

    frameProcessor_.detach_shell();

    destroy_back_buffers();

    ctx_.frameProcessor_queue = VK_NULL_HANDLE;
    ctx_.present_queue = VK_NULL_HANDLE;
    ctx_.video_queue  = VK_NULL_HANDLE;

    vk::DestroyDevice(ctx_.dev, nullptr);
    ctx_.dev = VK_NULL_HANDLE;
}

void Shell::create_dev() {
    VkDeviceCreateInfo dev_info = {};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 0;

    const std::vector<float> queue_priorities(settings_.queue_count, 0.0f);
    std::array<VkDeviceQueueCreateInfo, 3> queue_info = {};
    queue_info[dev_info.queueCreateInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[dev_info.queueCreateInfoCount].queueFamilyIndex = ctx_.frameProcessor_queue_family;
    queue_info[dev_info.queueCreateInfoCount].queueCount = settings_.queue_count;
    queue_info[dev_info.queueCreateInfoCount].pQueuePriorities = queue_priorities.data();
    dev_info.queueCreateInfoCount++;
    if (ctx_.frameProcessor_queue_family != ctx_.present_queue_family) {

        queue_info[dev_info.queueCreateInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[dev_info.queueCreateInfoCount].queueFamilyIndex = ctx_.present_queue_family;
        queue_info[dev_info.queueCreateInfoCount].queueCount = 1;
        queue_info[dev_info.queueCreateInfoCount].pQueuePriorities = queue_priorities.data();
        dev_info.queueCreateInfoCount++;
    }

    if (ctx_.video_decode_queue_family != (uint32_t)-1) {
        queue_info[dev_info.queueCreateInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[dev_info.queueCreateInfoCount].queueFamilyIndex = ctx_.video_decode_queue_family;
        queue_info[dev_info.queueCreateInfoCount].queueCount = 1;
        queue_info[dev_info.queueCreateInfoCount].pQueuePriorities = queue_priorities.data();
        dev_info.queueCreateInfoCount++;
    }

    dev_info.pQueueCreateInfos = queue_info.data();

    dev_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions_.size());
    dev_info.ppEnabledExtensionNames = device_extensions_.data();

    // disable all features
    VkPhysicalDeviceFeatures features = {};
    dev_info.pEnabledFeatures = &features;


    vk::assert_success(vk::CreateDevice(ctx_.physical_dev, &dev_info, nullptr, &ctx_.dev));
}

void Shell::create_back_buffers() {

    // BackBuffer is used to track which swapchain image and its associated
    // sync primitives are busy.  Having more BackBuffer's than swapchain
    // images may allows us to replace CPU wait on present_fence by GPU wait
    // on acquire_semaphore.
    const int count = settings_.back_buffer_count + 1;
    ctx_.backBuffers_.resize(count);
    for (auto &backBuffers : ctx_.backBuffers_) {
        vk::assert_success(backBuffers.Create(ctx_.dev));
    }


    for (int i = 0; i < (count + 1); i++) {
        AcquireBuffer* pAcquireBuffer = new AcquireBuffer();
        vk::assert_success(pAcquireBuffer->Create(ctx_.dev));
        ctx_.acquireBuffers_.push(pAcquireBuffer);
    }

    ctx_.currentBackBuffer_ = 0;
}

void Shell::destroy_back_buffers() {

    ctx_.backBuffers_.clear();

    while (!ctx_.acquireBuffers_.empty()) {
        AcquireBuffer* pAcquireBuffer = ctx_.acquireBuffers_.front();
        ctx_.acquireBuffers_.pop();
        delete pAcquireBuffer;
    }

    ctx_.currentBackBuffer_ = 0;
}

void Shell::create_swapchain() {
    ctx_.surface = create_surface(ctx_.instance);
    assert(ctx_.surface);

    VkBool32 supported;
    vk::assert_success(
        vk::GetPhysicalDeviceSurfaceSupportKHR(ctx_.physical_dev, ctx_.present_queue_family, ctx_.surface, &supported));
    // this should be guaranteed by the platform-specific can_present call
    assert(supported);

    std::vector<VkSurfaceFormatKHR> formats;
    vk::get(ctx_.physical_dev, ctx_.surface, formats);
    ctx_.format = formats[0];

    // Tegra hack __VkModesetApiNvdc::vkFormatToNvColorFormat() does not mapp the correct formats.
#ifdef NV_RMAPI_TEGRA
    ctx_.format.format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
#endif // NV_RMAPI_TEGRA

    // defer to resize_swapchain()
    ctx_.swapchain = VK_NULL_HANDLE;
    ctx_.extent.width = (uint32_t)-1;
    ctx_.extent.height = (uint32_t)-1;
}

void Shell::destroy_swapchain() {
    if (ctx_.swapchain != VK_NULL_HANDLE) {
        frameProcessor_.detach_swapchain();

        vk::DestroySwapchainKHR(ctx_.dev, ctx_.swapchain, nullptr);
        ctx_.swapchain = VK_NULL_HANDLE;
    }

    vk::DestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr);
    ctx_.surface = VK_NULL_HANDLE;
}

void Shell::resize_swapchain(uint32_t width_hint, uint32_t height_hint) {
    VkSurfaceCapabilitiesKHR caps;
    vk::assert_success(vk::GetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_.physical_dev, ctx_.surface, &caps));

    VkExtent2D extent = caps.currentExtent;
    // use the hints
    if (extent.width == (uint32_t)-1) {
        extent.width = width_hint;
        extent.height = height_hint;
    }
    // clamp width; to protect us from broken hints?
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    else if (extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    // clamp height
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    else if (extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;

    if (ctx_.extent.width == extent.width && ctx_.extent.height == extent.height) return;

    uint32_t image_count = settings_.back_buffer_count;
    if (image_count < caps.minImageCount)
        image_count = caps.minImageCount;
    else if (image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    assert(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    assert(caps.supportedTransforms & caps.currentTransform);
    assert(caps.supportedCompositeAlpha & (VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR));
    VkCompositeAlphaFlagBitsKHR composite_alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
                                                      ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                                                      : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    std::vector<VkPresentModeKHR> modes;
    vk::get(ctx_.physical_dev, ctx_.surface, modes);

    // FIFO is the only mode universally supported
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
        if ((settings_.vsync && m == VK_PRESENT_MODE_MAILBOX_KHR) || (!settings_.vsync && m == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            mode = m;
            break;
        }
    }

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = ctx_.surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = ctx_.format.format;
    swapchain_info.imageColorSpace = ctx_.format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    std::vector<uint32_t> queue_families(1, ctx_.frameProcessor_queue_family);
    if (ctx_.frameProcessor_queue_family != ctx_.present_queue_family) {
        queue_families.push_back(ctx_.present_queue_family);

        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_info.queueFamilyIndexCount = (uint32_t)queue_families.size();
        swapchain_info.pQueueFamilyIndices = queue_families.data();
    } else {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchain_info.preTransform = caps.currentTransform;
    swapchain_info.compositeAlpha = composite_alpha;
    swapchain_info.presentMode = mode;
    swapchain_info.clipped = true;
    swapchain_info.oldSwapchain = ctx_.swapchain;

    vk::assert_success(vk::CreateSwapchainKHR(ctx_.dev, &swapchain_info, nullptr, &ctx_.swapchain));
    ctx_.extent = extent;

    // destroy the old swapchain
    if (swapchain_info.oldSwapchain != VK_NULL_HANDLE) {
        frameProcessor_.detach_swapchain();

        vk::DeviceWaitIdle(ctx_.dev);
        vk::DestroySwapchainKHR(ctx_.dev, swapchain_info.oldSwapchain, nullptr);
    }

    frameProcessor_.attach_swapchain();
}

void Shell::add_frameProcessor_time(float time) {
    int max_ticks = 3;

    if (!settings_.no_tick) frameProcessor_time_ += time;

    while (frameProcessor_time_ >= frameProcessor_tick_ && max_ticks--) {
        frameProcessor_.on_tick();
        frameProcessor_time_ -= frameProcessor_tick_;
    }
}

void Shell::acquire_back_buffer(bool trainFrame) {
    // acquire just once when not presenting
    if (settings_.no_present && GetCurrentBackBuffer().GetAcquireSemaphore() != VK_NULL_HANDLE) return;

    AcquireBuffer* acquireBuf = ctx_.acquireBuffers_.front();

    assert(acquireBuf != nullptr);

    uint32_t imageIndex = 0;
    vk::assert_success(
        vk::AcquireNextImageKHR(ctx_.dev, ctx_.swapchain, UINT64_MAX, acquireBuf->semaphore_, acquireBuf->fence_, &imageIndex));

    assert(imageIndex < ctx_.backBuffers_.size());
    BackBuffer& back = ctx_.backBuffers_[imageIndex];

    // wait until acquire and render semaphores are waited/unsignaled
    vk::assert_success(vk::WaitForFences(ctx_.dev, 1, &acquireBuf->fence_, true, UINT64_MAX));
    // reset the fence
    vk::assert_success(vk::ResetFences(ctx_.dev, 1, &acquireBuf->fence_));

    ctx_.currentBackBuffer_ = imageIndex;
    AcquireBuffer* oldAcquireBuffer = back.SetAcquireBuffer(imageIndex, acquireBuf);
    ctx_.acquireBuffers_.pop();
    if (oldAcquireBuffer) {
        ctx_.acquireBuffers_.push(oldAcquireBuffer);
    }
    ctx_.acquiredFrameId++;
}

void Shell::present_back_buffer(bool trainFrame) {
    BackBuffer& back = GetCurrentBackBuffer();
    assert(back.isInPrepareState());

    // float timeTick = frameProcessor_time_ / frameProcessor_tick_;
    frameProcessor_.on_frame(trainFrame);

    if (settings_.no_present) {
        fake_present();
        return;
    }

    uint32_t imageIndex = back.GetImageIndex();
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &back.GetRenderSemaphore();
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &ctx_.swapchain;
    present_info.pImageIndices = &imageIndex;

    VkResult res = vk::QueuePresentKHR(ctx_.present_queue, &present_info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        std::cout << "Out of date Present Surface" << res << std::endl;
        back.setBufferCanceled();
        return;
    }

    back.setBufferInSwapchain();
}

void Shell::fake_present() {
    const BackBuffer& back = GetCurrentBackBuffer();
    assert(back.isInPrepareState());

    assert(settings_.no_present);

    // wait render semaphore and signal acquire semaphore
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &back.GetRenderSemaphore();
    submit_info.pWaitDstStageMask = &stage;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &back.GetAcquireSemaphore();
    vk::assert_success(vk::QueueSubmit(ctx_.frameProcessor_queue, 1, &submit_info, VK_NULL_HANDLE));
}
