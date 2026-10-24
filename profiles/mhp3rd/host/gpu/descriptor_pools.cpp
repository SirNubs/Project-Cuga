#include "gpu/descriptor_pools.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace mhp3rd::gpu {
namespace {

std::uint32_t environment_number(const char *name) {
    const char *text = std::getenv(name);
    if (text == nullptr) return 0u;
    const unsigned long value = std::strtoul(text, nullptr, 10);
    return static_cast<std::uint32_t>(std::min<unsigned long>(value, 1u << 20u));
}

// The most a pool is made for, however often the driver refuses: past this
// the refusal is not about room.
constexpr std::uint32_t kMaxHeadroom = 16u;

} // namespace

std::string describe_result(VkResult result) {
    const char *name = nullptr;
    switch (result) {
    case VK_SUCCESS:
        name = "VK_SUCCESS";
        break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        name = "VK_ERROR_OUT_OF_HOST_MEMORY";
        break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        name = "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        break;
    case VK_ERROR_FRAGMENTED_POOL:
        name = "VK_ERROR_FRAGMENTED_POOL";
        break;
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        name = "VK_ERROR_OUT_OF_POOL_MEMORY";
        break;
    case VK_ERROR_FRAGMENTATION:
        name = "VK_ERROR_FRAGMENTATION";
        break;
    case VK_ERROR_DEVICE_LOST:
        name = "VK_ERROR_DEVICE_LOST";
        break;
    default:
        break;
    }
    const std::string number = std::to_string(static_cast<int>(result));
    return name != nullptr ? std::string(name) + " (" + number + ")" : "VkResult " + number;
}

DescriptorPools::Driver DescriptorPools::vulkan(VkDevice device, VkDescriptorSetLayout layout) {
    Driver driver;
    driver.create_pool = [device](const VkDescriptorPoolCreateInfo &info, VkDescriptorPool &pool) {
        return vkCreateDescriptorPool(device, &info, nullptr, &pool);
    };
    driver.destroy_pool = [device](VkDescriptorPool pool) { vkDestroyDescriptorPool(device, pool, nullptr); };
    driver.allocate = [device, layout](VkDescriptorPool pool, VkDescriptorSet &set) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool;
        info.descriptorSetCount = 1u;
        info.pSetLayouts = &layout;
        return vkAllocateDescriptorSets(device, &info, &set);
    };
    driver.free = [device](
                      VkDescriptorPool pool, VkDescriptorSet set) { vkFreeDescriptorSets(device, pool, 1u, &set); };
    return driver;
}

DescriptorPools::Options DescriptorPools::from_environment(Options options) {
    if (const std::uint32_t sets = environment_number("MHP3RD_DESCRIPTOR_POOL_SETS"); sets != 0u)
        options.sets_per_pool = sets;
    if (const std::uint32_t limit = environment_number("MHP3RD_DESCRIPTOR_POOL_LIMIT"); limit != 0u)
        options.max_pools = limit;
    return options;
}

bool DescriptorPools::create(Driver driver, Options options, std::string &error) {
    destroy();
    driver_ = std::move(driver);
    options_ = std::move(options);
    options_.sets_per_pool = std::max(options_.sets_per_pool, 1u);
    options_.max_pools = std::max(options_.max_pools, 1u);
    headroom_ = 1u;
    return add_pool(options_.sets_per_pool, error);
}

bool DescriptorPools::add_pool(std::uint32_t sets, std::string &error) {
    std::vector<VkDescriptorPoolSize> sizes = options_.per_set;
    for (VkDescriptorPoolSize &size : sizes) size.descriptorCount *= sets;
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = sets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool handle{};
    const VkResult result = driver_.create_pool(info, handle);
    if (result != VK_SUCCESS) {
        error = "vkCreateDescriptorPool for the " + options_.name + " (" + std::to_string(sets) +
            " sets) failed with " + describe_result(result);
        return false;
    }
    pools_.push_back({handle, sets, 0u, false});
    if (pools_.size() > 1u)
        std::cout << "[render] " << options_.name << ": pool " << pools_.size() << " of at most " << options_.max_pools
                  << " made, for " << sets << " sets (" << owner_.size() << " in use)\n";
    return true;
}

VkDescriptorSet DescriptorPools::allocate(std::string &error) {
    if (pools_.empty()) {
        error = "the " + options_.name + " have no pool";
        return VK_NULL_HANDLE;
    }
    VkResult last = VK_SUCCESS;
    for (;;) {
        // The pool the last set came from first, then the others.
        for (std::size_t step = 0; step < pools_.size(); ++step) {
            const std::size_t index = (current_ + step) % pools_.size();
            Pool &pool = pools_[index];
            if (pool.refused || pool.used >= pool.sets) continue;
            VkDescriptorSet set{};
            const VkResult result = driver_.allocate(pool.handle, set);
            if (result == VK_SUCCESS && set != VK_NULL_HANDLE) {
                ++pool.used;
                owner_.emplace(set, index);
                current_ = index;
                if (owner_.size() > peak_) {
                    peak_ = owner_.size();
                    if (peak_ % 256u == 0u) std::cout << "[render] " << summary() << "\n";
                }
                return set;
            }
            last = result;
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
                error = "vkAllocateDescriptorSets for the " + options_.name + " failed with " + describe_result(result);
                return VK_NULL_HANDLE;
            }
            // Full for this driver before it is full by the numbers: the
            // pools made from now on get more room.
            pool.refused = true;
            if (pool.used < pool.sets) {
                headroom_ = std::min(headroom_ * 2u, kMaxHeadroom);
                std::cout << "[render] " << options_.name << ": the driver refused a set from pool " << index + 1u
                          << " with " << pool.used << " of " << pool.sets << " sets in use (" << describe_result(result)
                          << "); new pools get " << headroom_ << "x the room\n";
            }
        }
        if (pools_.size() >= options_.max_pools) break;
        std::string pool_error;
        if (!add_pool(options_.sets_per_pool * headroom_, pool_error)) {
            error = pool_error;
            return VK_NULL_HANDLE;
        }
        current_ = pools_.size() - 1u;
    }
    error = "vkAllocateDescriptorSets for the " + options_.name + " failed with " +
        describe_result(last != VK_SUCCESS ? last : VK_ERROR_OUT_OF_POOL_MEMORY) + ": " + summary();
    return VK_NULL_HANDLE;
}

void DescriptorPools::free(VkDescriptorSet &set) {
    if (set == VK_NULL_HANDLE) return;
    const auto found = owner_.find(set);
    if (found != owner_.end()) {
        Pool &pool = pools_[found->second];
        driver_.free(pool.handle, set);
        if (pool.used > 0u) --pool.used;
        pool.refused = false;
        owner_.erase(found);
    }
    set = VK_NULL_HANDLE;
}

void DescriptorPools::destroy() {
    for (const Pool &pool : pools_)
        if (driver_.destroy_pool) driver_.destroy_pool(pool.handle);
    pools_.clear();
    owner_.clear();
    current_ = 0u;
}

std::size_t DescriptorPools::capacity() const noexcept {
    std::size_t total = 0u;
    for (const Pool &pool : pools_) total += pool.sets;
    return total;
}

std::string DescriptorPools::summary() const {
    std::string text = options_.name + ": " + std::to_string(owner_.size()) + " in use (peak " + std::to_string(peak_) +
        ") in " + std::to_string(pools_.size()) + " pool" + (pools_.size() == 1u ? "" : "s") + " of at most " +
        std::to_string(options_.max_pools) + ", " + std::to_string(capacity()) + " sets in all";
    std::size_t refused = 0u;
    for (const Pool &pool : pools_) refused += pool.refused ? 1u : 0u;
    if (refused != 0u) text += ", " + std::to_string(refused) + " refused by the driver";
    return text;
}

} // namespace mhp3rd::gpu
