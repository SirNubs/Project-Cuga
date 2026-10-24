#include "game/call_trace.hpp"

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

namespace mhp2g::game {
namespace {

constexpr std::uint32_t kHunter = 0x090AF180u;
constexpr std::size_t kSlots = 4u;

struct Slot {
    std::uint32_t address{};
    psprecomp::Runtime::RecompiledFunction original{};
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>> seen;
};
std::array<Slot, kSlots> slots;

template <std::size_t N>
void traced(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    Slot &slot = slots[N];
    if (ctx.pc == slot.address) {
        const auto &memory = runtime.memory();
        const std::uint32_t motion = memory.raw_pointer(kHunter, 0x400u) != nullptr ? memory.load16(kHunter + 0x324u) : 0u;
        if (motion >= 1000u) {
            const auto key = std::make_tuple(motion, ctx.gpr[31], ctx.gpr[5], ctx.gpr[6], ctx.fpr_bits(12));
            if (slot.seen.insert(key).second)
                std::cout << "[call] " << std::hex << std::uppercase << slot.address << " ra " << ctx.gpr[31] << " a0 "
                          << ctx.gpr[4] << " a1 " << ctx.gpr[5] << " a2 " << ctx.gpr[6] << " a3 " << ctx.gpr[7] << std::dec
                          << " f12 " << ctx.fpr[12] << " motion " << motion << std::endl;
        }
    }
    slot.original(runtime, ctx);
}

// Lab experiment, MHP2G_FORCE_SKILL=<file>: the game's "has the hunter this skill" (0x088D9364: hunter, skill
// number) answers yes for the number, or the range of two numbers (hex), in the file, which is read again now and then.
constexpr std::uint32_t kHasSkill = 0x088D9364u;
psprecomp::Runtime::RecompiledFunction has_skill_original{};
std::string forced_file;
std::uint32_t forced_skill = 0xFFFFFFFFu; // the first of the range
std::uint32_t forced_last = 0u;
unsigned forced_calls = 0u;

void has_skill(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if (ctx.pc == kHasSkill) {
        if (forced_calls++ % 256u == 0u) {
            std::ifstream in(forced_file);
            std::string word;
            forced_skill = in >> word ? static_cast<std::uint32_t>(std::strtoul(word.c_str(), nullptr, 16)) : 0xFFFFFFFFu;
            forced_last = in >> word ? static_cast<std::uint32_t>(std::strtoul(word.c_str(), nullptr, 16)) : forced_skill;
        }
        if (ctx.gpr[4] == kHunter && ctx.gpr[5] >= forced_skill && ctx.gpr[5] <= forced_last) {
            ctx.gpr[2] = 1u;
            ctx.pc = ctx.gpr[31];
            return;
        }
    }
    has_skill_original(runtime, ctx);
}

} // namespace

void prepare_call_trace(psprecomp::Runtime &runtime) {
    if (const char *file = std::getenv("MHP2G_FORCE_SKILL"); file != nullptr && *file != 0) {
        has_skill_original = runtime.function_at(kHasSkill);
        if (has_skill_original != nullptr) {
            forced_file = file;
            runtime.register_function(kHasSkill, &has_skill, "mhp2g_force_skill");
            std::cout << "[call] forcing the skill named in " << file << std::endl;
        }
    }
    const char *list = std::getenv("MHP2G_TRACE_CALLS");
    if (list == nullptr || *list == 0) return;
    constexpr psprecomp::Runtime::RecompiledFunction wrappers[kSlots] = {&traced<0>, &traced<1>, &traced<2>, &traced<3>};
    std::stringstream in(list);
    std::string item;
    std::size_t n = 0u;
    while (n < kSlots && std::getline(in, item, ',')) {
        const std::uint32_t address = static_cast<std::uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
        const auto original = runtime.function_at(address);
        if (original == nullptr) {
            std::cout << "[call] no function at " << item << std::endl;
            continue;
        }
        slots[n].address = address;
        slots[n].original = original;
        runtime.register_function(address, wrappers[n], "mhp2g_call_trace");
        std::cout << "[call] tracing " << item << std::endl;
        ++n;
    }
}

} // namespace mhp2g::game
