#pragma once

#include "psprecomp/interpreter.hpp"
#include "psprecomp/runtime.hpp"

namespace mhp2g {

// Register the pristine overlay's AOT corpus before FUC modifies its code.
void prepare_fuc_overlay(psprecomp::Runtime &rt, const psprecomp::AllegrexContext &ctx);

// FUComplete installs these hooks after static recompilation. Run the guest's
// own hook, including its delay slot, instead of the stale compiled instruction.
// Called at gathering and prerequisite file-loader sites; ordinary code stays AOT.
inline bool follow_fuc_gather_hook(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx,
    psprecomp::GuestMemory::AotFastView &memory, std::uint32_t site) {
    const std::uint32_t instruction = memory.aot_load32(site);
    // Farm gathering also removes the store in the following call's delay
    // slot. Its JAL opcode is unchanged, so honor that actual slot explicitly.
    if (site == 0x09AAA0F8u && (instruction >> 26u) == 3u && memory.aot_load32(site + 4u) == 0u) {
        ctx.pc = site;
        psprecomp::interpret_allegrex(rt, ctx, 1u);
        return true;
    }
    if ((instruction >> 26u) != 2u) return false; // J, never JAL or an ordinary store
    const std::uint32_t target = ((site + 4u) & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2u);
    // FUC's MODLOAD.BIN allocation. Unrelated overlay jumps are not hooks.
    if (target < 0x08A35910u || target >= 0x08A40000u) return false;
    if (site == 0x0884E730u) prepare_fuc_overlay(rt, ctx);
    ctx.pc = site;
    psprecomp::interpret_allegrex(rt, ctx, 1u);
    return true;
}

} // namespace mhp2g
