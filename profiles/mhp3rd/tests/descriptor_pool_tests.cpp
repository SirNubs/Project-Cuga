// Descriptor pools the way a phone's driver counts them: a pool gives out
// exactly the sets it was made for, and VK_ERROR_OUT_OF_POOL_MEMORY after.
// The pools must grow instead of failing, give sets back, keep to their
// limit and make room for a driver that refuses before the numbers say.

#include "gpu/descriptor_pools.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using mhp3rd::gpu::DescriptorPools;

int failures{};

void expect(bool condition, const char *what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL: " << what << "\n";
}

template <typename Handle> Handle make_handle(std::uint64_t value) {
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(static_cast<std::uintptr_t>(value));
    else
        return static_cast<Handle>(value);
}

// A strict driver: each pool holds maxSets sets and its declared descriptors,
// less `short_by` sets when it plays a driver that refuses early.
struct StrictDriver {
    struct Pool {
        std::uint32_t max_sets{};
        std::map<VkDescriptorType, std::uint32_t> left;
        std::uint32_t used{};
    };
    std::map<VkDescriptorPool, Pool> pools;
    std::map<VkDescriptorPool, std::uint32_t> destroyed;
    std::vector<VkDescriptorPoolSize> per_set;
    std::uint32_t short_by{};
    std::uint64_t next{1u};
    std::uint32_t created{};

    DescriptorPools::Driver driver() {
        DescriptorPools::Driver d;
        d.create_pool = [this](const VkDescriptorPoolCreateInfo &info, VkDescriptorPool &handle) {
            handle = make_handle<VkDescriptorPool>(next++);
            Pool pool;
            pool.max_sets = info.maxSets;
            for (std::uint32_t i = 0; i < info.poolSizeCount; ++i)
                pool.left[info.pPoolSizes[i].type] += info.pPoolSizes[i].descriptorCount;
            pools[handle] = pool;
            ++created;
            return VK_SUCCESS;
        };
        d.destroy_pool = [this](VkDescriptorPool handle) {
            ++destroyed[handle];
            pools.erase(handle);
        };
        d.allocate = [this](VkDescriptorPool handle, VkDescriptorSet &set) {
            Pool &pool = pools.at(handle);
            if (pool.used + short_by >= pool.max_sets) return VK_ERROR_OUT_OF_POOL_MEMORY;
            for (const VkDescriptorPoolSize &size : per_set)
                if (pool.left[size.type] < size.descriptorCount) return VK_ERROR_OUT_OF_POOL_MEMORY;
            for (const VkDescriptorPoolSize &size : per_set) pool.left[size.type] -= size.descriptorCount;
            ++pool.used;
            set = make_handle<VkDescriptorSet>(next++);
            return VK_SUCCESS;
        };
        d.free = [this](VkDescriptorPool handle, VkDescriptorSet) {
            Pool &pool = pools.at(handle);
            for (const VkDescriptorPoolSize &size : per_set) pool.left[size.type] += size.descriptorCount;
            --pool.used;
        };
        return d;
    }
};

const std::vector<VkDescriptorPoolSize> kTexture{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u}};
const std::vector<VkDescriptorPoolSize> kLighting{
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3u}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2u}};

void test_grows_instead_of_failing() {
    StrictDriver strict;
    strict.per_set = kTexture;
    DescriptorPools pools;
    std::string error;
    expect(pools.create(strict.driver(), {"texture sets", kTexture, 4u, 8u}, error), "the first pool is made");
    std::vector<VkDescriptorSet> sets;
    for (int i = 0; i < 10; ++i) sets.push_back(pools.allocate(error));
    bool all = true;
    for (VkDescriptorSet set : sets) all = all && set != VK_NULL_HANDLE;
    expect(all, "ten sets from pools of four");
    expect(pools.pool_count() == 3u, "three pools for ten sets");
    expect(pools.live() == 10u && pools.peak() == 10u, "ten in use");
    for (VkDescriptorSet &set : sets) pools.free(set);
    expect(pools.live() == 0u, "all given back");
    expect(sets.front() == VK_NULL_HANDLE, "a freed set is cleared");
    for (int i = 0; i < 12; ++i) sets[static_cast<std::size_t>(i % 10)] = pools.allocate(error);
    expect(pools.pool_count() == 3u, "freed room is used again before a new pool is made");
    pools.destroy();
    expect(strict.pools.empty(), "destroy() destroys every pool");
}

void test_limit() {
    StrictDriver strict;
    strict.per_set = kTexture;
    DescriptorPools pools;
    std::string error;
    pools.create(strict.driver(), {"texture sets", kTexture, 2u, 2u}, error);
    VkDescriptorSet a = pools.allocate(error), b = pools.allocate(error), c = pools.allocate(error),
                    d = pools.allocate(error);
    expect(a && b && c && d, "two pools of two give four sets");
    error.clear();
    const VkDescriptorSet e = pools.allocate(error);
    expect(e == VK_NULL_HANDLE, "the fifth set fails at the limit");
    expect(error.find("texture sets") != std::string::npos, "the error names the pools");
    expect(error.find("VK_ERROR_OUT_OF_POOL_MEMORY (-1000069000)") != std::string::npos,
        "the error names the result and its number");
    pools.free(b);
    expect(pools.allocate(error) != VK_NULL_HANDLE, "a freed set makes room again");
    pools.destroy();
}

void test_never_over_asks() {
    // A pool made for exactly one lighting set: the second must come from a
    // new pool, never from the full one.
    StrictDriver strict;
    strict.per_set = kLighting;
    DescriptorPools pools;
    std::string error;
    pools.create(strict.driver(), {"lighting set", kLighting, 1u, 4u}, error);
    const VkDescriptorSet first = pools.allocate(error);
    const VkDescriptorSet second = pools.allocate(error);
    expect(first != VK_NULL_HANDLE && second != VK_NULL_HANDLE, "two lighting sets");
    expect(pools.pool_count() == 2u, "one pool each");
    pools.destroy();
}

void test_driver_refusing_early() {
    // A driver that holds one set fewer than declared: a pool made for one
    // set holds none. The next pools get more room until one works.
    StrictDriver strict;
    strict.per_set = kLighting;
    strict.short_by = 1u;
    DescriptorPools pools;
    std::string error;
    pools.create(strict.driver(), {"lighting set", kLighting, 1u, 4u}, error);
    const VkDescriptorSet set = pools.allocate(error);
    expect(set != VK_NULL_HANDLE, "a driver that refuses early still gives a set from a larger pool");
    expect(pools.pool_count() == 2u, "one more pool, with twice the room");
    pools.destroy();

    // One that refuses everything fails with the pools named, within the limit.
    StrictDriver never;
    never.per_set = kLighting;
    never.short_by = 1000u;
    DescriptorPools none;
    none.create(never.driver(), {"lighting set", kLighting, 1u, 3u}, error);
    error.clear();
    expect(none.allocate(error) == VK_NULL_HANDLE, "a driver that never gives a set fails");
    expect(none.pool_count() == 3u, "no more pools than the limit");
    expect(error.find("refused by the driver") != std::string::npos, "the error says the driver refused");
    none.destroy();
}

void test_free_of_unknown_and_after_destroy() {
    StrictDriver strict;
    strict.per_set = kTexture;
    DescriptorPools pools;
    std::string error;
    pools.create(strict.driver(), {"texture sets", kTexture, 2u, 2u}, error);
    VkDescriptorSet set = pools.allocate(error);
    pools.destroy();
    pools.free(set); // the pool is gone: nothing to give back to
    expect(set == VK_NULL_HANDLE, "a set freed after destroy() is only cleared");
    VkDescriptorSet null_set = VK_NULL_HANDLE;
    pools.free(null_set);
    expect(pools.allocate(error) == VK_NULL_HANDLE, "no set without a pool");
}

} // namespace

int main() {
    test_grows_instead_of_failing();
    test_limit();
    test_never_over_asks();
    test_driver_refusing_early();
    test_free_of_unknown_and_after_destroy();
    if (failures != 0) {
        std::cout << failures << " descriptor pool checks failed\n";
        return 1;
    }
    std::cout << "descriptor pool checks passed\n";
    return 0;
}
