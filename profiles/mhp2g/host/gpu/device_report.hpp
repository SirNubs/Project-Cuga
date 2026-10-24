#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

// What a player's log needs to tell one phone's GPU from another, and what
// the renderer checks before it relies on a capability.
//
// The renderer asks for little: Vulkan 1.1, RGBA8 images it can render to,
// sample and blit, a depth format, 128 bytes of push constants, three dynamic
// uniform buffers and a host-visible vertex buffer. Every Vulkan device is
// meant to offer all of that, so a missing piece is named in the error rather
// than assumed. Compressed texture formats (BC/DXT, which Mali and PowerVR
// lack) are never used: the GE's DXT textures are decoded on the CPU.
namespace mhp2g::gpu {

inline constexpr std::uint32_t kVendorArm = 0x13B5u;
inline constexpr std::uint32_t kVendorImagination = 0x1010u;
inline constexpr std::uint32_t kVendorQualcomm = 0x5143u;

struct DeviceFacts {
    std::string name;
    std::string driver_name; // VK_KHR_driver_properties, when the device has it
    std::string driver_info;
    std::uint32_t vendor_id{};
    std::uint32_t device_id{};
    std::uint32_t api_version{};
    std::uint32_t driver_version{};
    VkDeviceSize max_allocation{}; // maxMemoryAllocationSize; 0 when unknown

    [[nodiscard]] bool arm() const noexcept { return vendor_id == kVendorArm; }
    [[nodiscard]] bool imagination() const noexcept { return vendor_id == kVendorImagination; }
    // "r32p1" for ARM's Mali drivers, which number themselves that way in
    // driverVersion; the plain version otherwise.
    [[nodiscard]] std::string driver_version_text() const;
};

[[nodiscard]] DeviceFacts read_device_facts(VkPhysicalDevice device);

// One line to the log (stdout, which the Android app writes to yakumo.log)
// and, on Android, to logcat as well.
void log_line(const std::string &line);

// Logs the device's limits, features, formats and memory as far as the
// renderer cares, plus the surface's formats, present modes and
// capabilities. Once per start.
void log_device(VkPhysicalDevice device, VkSurfaceKHR surface, const DeviceFacts &facts);

// What the renderer needs and the device does not offer, one entry each,
// in words a player can pass on. Empty when everything is there.
[[nodiscard]] std::vector<std::string> missing_requirements(
    VkPhysicalDevice device, const DeviceFacts &facts, VkDeviceSize vertex_buffer_bytes);

// The drivers v0.6.5 and v0.6.6 started in GPU compatibility mode (ARM Mali
// older than r38, PowerVR: what budget MediaTek phones ship, issues #159,
// #169), named for the log, or empty. Auto no longer acts on it: it cost
// those phones speed (#210, #212), and the black screen of #169 very likely
// came from a descriptor pool running out (#209). The self-test decides.
[[nodiscard]] std::string compat_reason(const DeviceFacts &facts);

[[nodiscard]] const char *vk_result_name(VkResult result);
[[nodiscard]] const char *format_name(VkFormat format);

} // namespace mhp2g::gpu
