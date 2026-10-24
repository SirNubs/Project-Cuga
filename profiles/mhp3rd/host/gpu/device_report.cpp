#include "gpu/device_report.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace mhp3rd::gpu {
namespace {

std::string version_text(std::uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
        std::to_string(VK_API_VERSION_PATCH(version));
}

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

bool has_extension(VkPhysicalDevice device, const char *name) {
    std::uint32_t count = 0u;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
        [&](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, name) == 0; });
}

// The format features the renderer reads, by name.
std::string feature_text(VkFormatFeatureFlags flags) {
    static const std::pair<VkFormatFeatureFlags, const char *> kNames[] = {
        {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "sampled"},
        {VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "linear"},
        {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "color"},
        {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT, "blend"},
        {VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth"},
        {VK_FORMAT_FEATURE_BLIT_SRC_BIT, "blit-src"},
        {VK_FORMAT_FEATURE_BLIT_DST_BIT, "blit-dst"},
        {VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, "copy-src"},
        {VK_FORMAT_FEATURE_TRANSFER_DST_BIT, "copy-dst"},
        {VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "vertex"},
    };
    std::string text;
    for (const auto &[bit, name] : kNames) {
        if ((flags & bit) == 0u) continue;
        if (!text.empty()) text += ' ';
        text += name;
    }
    return text.empty() ? std::string("none") : text;
}

// A format by name where the log names it, by number otherwise.
std::string format_label(VkFormat format) {
    const char *name = format_name(format);
    return std::strcmp(name, "another format") == 0 ? "format " + std::to_string(static_cast<int>(format)) : name;
}

std::string memory_flags_text(VkMemoryPropertyFlags flags) {
    std::string text;
    const auto add = [&](VkMemoryPropertyFlags bit, const char *name) {
        if ((flags & bit) == 0u) return;
        if (!text.empty()) text += '|';
        text += name;
    };
    add(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "device-local");
    add(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "host-visible");
    add(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "coherent");
    add(VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "cached");
    add(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "lazy");
    return text.empty() ? std::string("none") : text;
}

} // namespace

std::string DeviceFacts::driver_version_text() const {
    if (arm())
        return "r" + std::to_string(VK_API_VERSION_MAJOR(driver_version)) + "p" +
            std::to_string(VK_API_VERSION_MINOR(driver_version));
    if (vendor_id == kVendorQualcomm) return version_text(driver_version) + " (" + hex(driver_version) + ")";
    return hex(driver_version);
}

DeviceFacts read_device_facts(VkPhysicalDevice device) {
    DeviceFacts facts;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    facts.name = properties.deviceName;
    facts.vendor_id = properties.vendorID;
    facts.device_id = properties.deviceID;
    facts.api_version = properties.apiVersion;
    facts.driver_version = properties.driverVersion;
    if (VK_API_VERSION_MINOR(properties.apiVersion) < 1u && VK_API_VERSION_MAJOR(properties.apiVersion) == 1u)
        return facts;
    // Vulkan 1.1: properties2 and maintenance3 are core; the driver's own
    // name and build string need VK_KHR_driver_properties (core in 1.2).
    VkPhysicalDeviceMaintenance3Properties maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &maintenance;
    const bool driver_properties = VK_API_VERSION_MINOR(properties.apiVersion) >= 2u ||
        has_extension(device, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    if (driver_properties) maintenance.pNext = &driver;
    vkGetPhysicalDeviceProperties2(device, &properties2);
    facts.max_allocation = maintenance.maxMemoryAllocationSize;
    if (driver_properties) {
        facts.driver_name = driver.driverName;
        facts.driver_info = driver.driverInfo;
    }
    return facts;
}

void log_line(const std::string &line) {
    std::cout << line << "\n";
#if defined(__ANDROID__)
    SDL_Log("Yakumo: %s", line.c_str());
#endif
}

void log_device(VkPhysicalDevice device, VkSurfaceKHR surface, const DeviceFacts &facts) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    const VkPhysicalDeviceLimits &l = properties.limits;
    log_line("[gpu] " + facts.name + ", Vulkan " + version_text(facts.api_version) + ", driver " +
        facts.driver_version_text() + (facts.driver_name.empty() ? "" : " " + facts.driver_name) +
        (facts.driver_info.empty() ? "" : " (" + facts.driver_info + ")") + ", vendor " + hex(facts.vendor_id) +
        ", device " + hex(facts.device_id));
    log_line("[gpu] limits: image2D " + std::to_string(l.maxImageDimension2D) + ", push constants " +
        std::to_string(l.maxPushConstantsSize) + ", bound sets " + std::to_string(l.maxBoundDescriptorSets) +
        ", dynamic uniform buffers " + std::to_string(l.maxDescriptorSetUniformBuffersDynamic) + ", uniform range " +
        std::to_string(l.maxUniformBufferRange) + ", storage range " + std::to_string(l.maxStorageBufferRange) +
        ", samplers per stage " + std::to_string(l.maxPerStageDescriptorSamplers) + ", vertex attributes " +
        std::to_string(l.maxVertexInputAttributes) + ", allocations " + std::to_string(l.maxMemoryAllocationCount) +
        ", largest allocation " +
        (facts.max_allocation != 0u ? std::to_string(facts.max_allocation >> 20u) + " MiB" : "unknown") +
        ", uniform alignment " + std::to_string(l.minUniformBufferOffsetAlignment) + ", timestamps " +
        (l.timestampComputeAndGraphics ? "yes" : "no"));
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);
    log_line(std::string("[gpu] features: robustBufferAccess ") + (features.robustBufferAccess ? "yes" : "no") +
        ", vertexPipelineStoresAndAtomics " + (features.vertexPipelineStoresAndAtomics ? "yes" : "no") +
        ", shaderInt16 " + (features.shaderInt16 ? "yes" : "no") + ", compressed textures BC " +
        (features.textureCompressionBC ? "yes" : "no") + " ETC2 " + (features.textureCompressionETC2 ? "yes" : "no") +
        " ASTC " + (features.textureCompressionASTC_LDR ? "yes" : "no") + " (none needed: DXT is decoded on the CPU)");
    const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT_S8_UINT};
    for (const VkFormat format : formats) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &format_properties);
        log_line(std::string("[gpu] format ") + format_name(format) + ": " +
            feature_text(format_properties.optimalTilingFeatures));
    }
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        log_line("[gpu] memory heap " + std::to_string(i) + ": " + std::to_string(memory.memoryHeaps[i].size >> 20u) +
            " MiB" + ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u ? ", device local" : ""));
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        log_line("[gpu] memory type " + std::to_string(i) + ": heap " +
            std::to_string(memory.memoryTypes[i].heapIndex) + ", " +
            memory_flags_text(memory.memoryTypes[i].propertyFlags));

    std::uint32_t count = 0u;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    std::string names;
    for (const VkExtensionProperties &extension : extensions) {
        if (!names.empty()) names += ' ';
        names += extension.extensionName;
    }
    log_line("[gpu] " + std::to_string(count) + " device extensions: " + names);

    if (surface == VK_NULL_HANDLE) return;
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
    log_line("[gpu] surface: images " + std::to_string(capabilities.minImageCount) + "-" +
        std::to_string(capabilities.maxImageCount) + ", usage " + hex(capabilities.supportedUsageFlags) +
        ", composite alpha " + hex(capabilities.supportedCompositeAlpha) + ", transforms " +
        hex(capabilities.supportedTransforms) + ", current transform " + hex(capabilities.currentTransform) +
        ", extent " + std::to_string(capabilities.currentExtent.width) + "x" +
        std::to_string(capabilities.currentExtent.height));
    std::uint32_t format_count = 0u;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, surface_formats.data());
    // The sRGB colour space's formats; the others (HDR, wide gamut) are
    // only counted.
    std::string format_list;
    std::size_t other_spaces = 0u;
    for (const VkSurfaceFormatKHR &format : surface_formats) {
        if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            ++other_spaces;
            continue;
        }
        if (!format_list.empty()) format_list += ", ";
        format_list += format_label(format.format);
    }
    log_line("[gpu] surface formats: " + format_list +
        (other_spaces != 0u ? " (and " + std::to_string(other_spaces) + " in other colour spaces)" : ""));
    std::uint32_t mode_count = 0u;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, modes.data());
    std::string mode_list;
    for (const VkPresentModeKHR mode : modes) {
        if (!mode_list.empty()) mode_list += ' ';
        mode_list += std::to_string(static_cast<int>(mode));
    }
    log_line("[gpu] present modes: " + mode_list + " (0 immediate, 1 mailbox, 2 fifo, 3 fifo relaxed)");
}

std::vector<std::string> missing_requirements(
    VkPhysicalDevice device, const DeviceFacts &facts, VkDeviceSize vertex_buffer_bytes) {
    std::vector<std::string> missing;
    if (VK_API_VERSION_MAJOR(facts.api_version) == 1u && VK_API_VERSION_MINOR(facts.api_version) < 1u)
        missing.push_back("Vulkan 1.1 (the driver offers " + version_text(facts.api_version) + ")");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    const VkPhysicalDeviceLimits &l = properties.limits;
    const auto need = [&](bool ok, const std::string &what) {
        if (!ok) missing.push_back(what);
    };
    need(
        l.maxPushConstantsSize >= 128u, "128 bytes of push constants (" + std::to_string(l.maxPushConstantsSize) + ")");
    need(l.maxBoundDescriptorSets >= 2u, "two descriptor sets (" + std::to_string(l.maxBoundDescriptorSets) + ")");
    need(l.maxDescriptorSetUniformBuffersDynamic >= 3u,
        "three dynamic uniform buffers (" + std::to_string(l.maxDescriptorSetUniformBuffersDynamic) + ")");
    need(l.maxUniformBufferRange >= 544u,
        "a 544-byte uniform buffer range (" + std::to_string(l.maxUniformBufferRange) + ")");
    need(l.maxVertexInputAttributes >= 4u,
        "four vertex attributes (" + std::to_string(l.maxVertexInputAttributes) + ")");
    need(l.maxImageDimension2D >= 4096u, "4096-pixel images (" + std::to_string(l.maxImageDimension2D) + ")");
    if (facts.max_allocation != 0u)
        need(facts.max_allocation >= vertex_buffer_bytes,
            "a " + std::to_string(vertex_buffer_bytes >> 20u) + " MiB allocation (the largest is " +
                std::to_string(facts.max_allocation >> 20u) + " MiB)");

    const auto optimal = [&](VkFormat format) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &format_properties);
        return format_properties.optimalTilingFeatures;
    };
    const VkFormatFeatureFlags rgba = optimal(VK_FORMAT_R8G8B8A8_UNORM);
    const VkFormatFeatureFlags rgba_needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    need((rgba & rgba_needed) == rgba_needed,
        "RGBA8 images to draw into, sample and scale (has: " + feature_text(rgba) + ")");
    const VkFormat depths[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32,
        VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT_S8_UINT};
    need(std::any_of(std::begin(depths), std::end(depths),
             [&](VkFormat f) { return (optimal(f) & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u; }),
        "a depth buffer format");
    const VkFormat vertex_formats[] = {
        VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32_SFLOAT};
    for (const VkFormat format : vertex_formats) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &format_properties);
        need((format_properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0u,
            std::string("vertex format ") + format_name(format));
    }
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    bool host_visible = false;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((memory.memoryTypes[i].propertyFlags & wanted) == wanted) host_visible = true;
    }
    need(host_visible, "host-visible, coherent memory");
    return missing;
}

std::string compat_reason(const DeviceFacts &facts) {
    if (facts.arm() && VK_API_VERSION_MAJOR(facts.driver_version) < 38u)
        return "Mali driver " + facts.driver_version_text() + " is older than r38";
    if (facts.imagination()) return "PowerVR driver";
    return {};
}

const char *vk_result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INVALID_SHADER_NV:
        return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    default:
        return "an unlisted VkResult";
    }
}

const char *format_name(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
        return "R5G6B5_UNORM";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "A2B10G10R10_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_D32_SFLOAT:
        return "D32_SFLOAT";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return "X8_D24_UNORM";
    case VK_FORMAT_D16_UNORM:
        return "D16_UNORM";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return "R32G32B32A32_SFLOAT";
    case VK_FORMAT_R32G32B32_SFLOAT:
        return "R32G32B32_SFLOAT";
    case VK_FORMAT_R32G32_SFLOAT:
        return "R32G32_SFLOAT";
    default:
        return "another format";
    }
}

} // namespace mhp3rd::gpu
