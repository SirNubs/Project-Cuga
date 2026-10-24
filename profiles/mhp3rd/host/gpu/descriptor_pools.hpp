#pragma once

// Descriptor sets of one layout, taken from as many descriptor pools as they
// need.
//
// A pool holds a fixed number of sets and descriptors. Desktop drivers often
// hand out more than a pool was made for; phone drivers (Mali, Adreno) keep
// to the numbers and answer VK_ERROR_OUT_OF_POOL_MEMORY. So every pool here
// is counted exactly as it was declared: a set is never asked of a pool that
// has no room for it by the declared numbers, whatever the driver would
// allow, and a full pool makes the next one instead of failing. A driver
// that refuses a set although the pool has room by the numbers marks that
// pool full, and the pools made after it get more room.
//
// Environment, for testing the growth and the fallbacks on any GPU:
//   MHP3RD_DESCRIPTOR_POOL_SETS=N   every pool holds N sets
//   MHP3RD_DESCRIPTOR_POOL_LIMIT=N  at most N pools of each kind

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mhp3rd::gpu {

class DescriptorPools {
public:
    // The Vulkan calls, replaceable so the tests can play a strict driver.
    struct Driver {
        std::function<VkResult(const VkDescriptorPoolCreateInfo &, VkDescriptorPool &)> create_pool;
        std::function<void(VkDescriptorPool)> destroy_pool;
        std::function<VkResult(VkDescriptorPool, VkDescriptorSet &)> allocate;
        std::function<void(VkDescriptorPool, VkDescriptorSet)> free;
    };
    [[nodiscard]] static Driver vulkan(VkDevice device, VkDescriptorSetLayout layout);

    struct Options {
        std::string name;                          // in the log and in errors: "texture sets"
        std::vector<VkDescriptorPoolSize> per_set; // what one set of the layout holds
        std::uint32_t sets_per_pool{};
        std::uint32_t max_pools{};
    };
    // The options with MHP3RD_DESCRIPTOR_POOL_SETS and _LIMIT applied.
    [[nodiscard]] static Options from_environment(Options options);

    // A plain value: destroy() must run while the device is alive.
    // Makes the first pool.
    bool create(Driver driver, Options options, std::string &error);
    // A set, or VK_NULL_HANDLE with `error` naming the pools and what the
    // driver answered.
    [[nodiscard]] VkDescriptorSet allocate(std::string &error);
    // Gives a set back to its pool and clears it; a null set is ignored.
    void free(VkDescriptorSet &set);
    // Destroys every pool, and with them every set.
    void destroy();

    [[nodiscard]] bool ready() const noexcept { return !pools_.empty(); }
    [[nodiscard]] std::size_t live() const noexcept { return owner_.size(); }
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }
    [[nodiscard]] std::size_t pool_count() const noexcept { return pools_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept;
    // "texture sets: 530 in use (peak 612) in 2 pools of 512".
    [[nodiscard]] std::string summary() const;

private:
    struct Pool {
        VkDescriptorPool handle{};
        std::uint32_t sets{}; // as declared
        std::uint32_t used{};
        bool refused{}; // the driver said it is full
    };
    bool add_pool(std::uint32_t sets, std::string &error);

    Driver driver_;
    Options options_;
    std::vector<Pool> pools_;
    std::unordered_map<VkDescriptorSet, std::size_t> owner_; // set -> index into pools_
    std::size_t current_{};                                  // where the last set came from
    std::size_t peak_{};
    std::uint32_t headroom_{1u}; // grows when the driver refuses a pool with room left
};

// "VK_ERROR_OUT_OF_POOL_MEMORY (-1000069000)" and the like.
[[nodiscard]] std::string describe_result(VkResult result);

} // namespace mhp3rd::gpu
