#include "mods/fuc_runtime_hooks.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <cstring>

using namespace psprecomp;
// Standalone instruction tests do not load shared overlay libraries.
namespace mhp2g { void prepare_fuc_overlay(Runtime &, const AllegrexContext &) {} }
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void actual_modloader(const char *path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
    check(bytes.size() > 4u && bytes.size() < 0xA6F0u, "Invalid private MODLOAD.BIN");
    Runtime rt;
    constexpr auto code = 0x08A35910u, object = 0x08900000u;
    rt.memory().copy_in(code, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(bytes.data() + 4u), bytes.size() - 4u));
    auto memory = rt.memory().aot_fast_view();
    auto locate = [&](std::initializer_list<std::uint32_t> words) {
        std::uint32_t found = 0;
        for (std::uint32_t at = code; at + words.size() * 4u <= code + bytes.size() - 4u; at += 4u) {
            bool match = true;
            std::uint32_t offset = 0;
            for (auto word : words) { match &= rt.memory().load32(at + offset) == word; offset += 4u; }
            if (match) { check(found == 0, "Ambiguous guest routine"); found = at; }
        }
        check(found != 0, "Guest routine not found");
        return found;
    };
    const auto counter = locate({0x2483FFFFu, 0x27BDFFF0u});
    const auto standing = locate({0x27BDFFE0u, 0xAFA7001Cu, 0xAFA60018u, 0xAFA50014u,
        0xAFA40010u, 0x96840298u});
    const auto farm = locate({0x27BDFFE0u, 0xAFA7001Cu, 0xAFA60018u, 0xAFA50014u,
        0xAFA40010u, 0x3405002Eu});
    const auto crouched = locate({0x34060020u, 0x90870481u});
    auto run = [&](std::uint32_t site, std::uint32_t target, std::uint32_t end) {
        auto &ctx = rt.cpu();
        ctx.gpr[29] = 0x08910000u;
        rt.memory().store32(site, 0x08000000u | (target >> 2u));
        rt.memory().store32(site + 4u, 0u);
        rt.register_function(end, [](Runtime &runtime, AllegrexContext &) { runtime.stop("Guest hook returned"); }, "test_return");
        check(mhp2g::follow_fuc_gather_hook(rt, ctx, memory, site), "Actual hook not followed");
        rt.run(ctx.pc, 100u);
        check(ctx.pc == end && rt.stop_reason() == "Guest hook returned", "Actual hook failed to return");
    };
    auto &ctx = rt.cpu();
    ctx.gpr[16] = object; ctx.gpr[4] = 3u;
    run(0x0886CFE8u, counter, 0x0886CFF0u);
    check(rt.memory().load8(0x08A3589Cu) == 2u && rt.memory().load16(object + 0x12u) == 2u,
        "Actual carve counter not updated");
    for (auto held : {false, true}) {
        ctx.gpr[20] = object;
        rt.memory().store16(object + 0x298u, 0x4700u);
        rt.memory().store8(object + 0x481u, held ? 0x20u : 0u);
        run(0x09A630C0u, standing, 0x09A630C8u);
        check(rt.memory().load16(object + 0x298u) == (held ? 0x4700u : 0u), "Standing plant hold/release failed");
        ctx.gpr[16] = object;
        rt.memory().store16(object + 0x298u, 0x108u);
        rt.memory().store8(object + 0x29Au, 0x2Eu);
        rt.memory().store8(object + 0x481u, held ? 0x80u : 0u);
        run(0x09AAA0F0u, farm, 0x09AAA0F8u);
        check(rt.memory().load16(object + 0x298u) == (held ? 0x108u : 0u), "Farm hold/release failed");
        ctx.gpr[4] = object;
        rt.memory().store8(object + 0x299u, 0x5Du);
        rt.memory().store8(object + 0x481u, held ? 0x20u : 0u);
        run(0x09A9CBA4u, crouched, 0x09A9CBACu);
        check(ctx.gpr[6] == (held ? 0x5Du : 0x1Du), "Crouched hold/release failed");
    }
    for (auto remaining : {0u, 2u}) {
        ctx.gpr[20] = object;
        rt.memory().store16(object + 0x298u, 0x4A00u);
        rt.memory().store8(object + 0x481u, 0x20u);
        rt.memory().store8(0x08A3589Cu, static_cast<std::uint8_t>(remaining));
        run(0x09A630C0u, standing, 0x09A630C8u);
        check(rt.memory().load16(object + 0x298u) == (remaining ? 0x4A00u : 0u), "Carving did not stop when exhausted");
        ctx.gpr[4] = object;
        rt.memory().store8(object + 0x299u, 0x60u);
        run(0x09A9CBA4u, crouched, 0x09A9CBACu);
        check(ctx.gpr[6] == (remaining ? 0x60u : 0x1Du), "Crouched carving exhaustion failed");
    }
    for (auto item : {0x90u, 0x91u, 0x92u, 0x94u, 0x95u, 0x96u, 0x4C9u, 0x4CAu, 1u}) {
        ctx.gpr[20] = object;
        rt.memory().store16(object + 0x298u, 0x5100u);
        rt.memory().store8(object + 0x481u, 0x80u);
        rt.memory().store8(object + 0x648u, 0u);
        rt.memory().store16(0x09A00846u, static_cast<std::uint16_t>(item));
        run(0x09A630C0u, standing, 0x09A630C8u);
        check(rt.memory().load16(object + 0x298u) == (item != 1u ? 0x5100u : 0u), "Mining/tool selection failed");
    }
    std::cout << "Actual private FUC modloader: counter, standing/crouched gathering, farm and carving checks passed\n";
}
int tests(int argc, char **argv) {
    Runtime rt;
    AllegrexContext ctx{};
    auto memory = rt.memory().aot_fast_view();
    constexpr std::uint32_t target = 0x08A35940u;
    for (const auto site : {0x0884E158u, 0x0884E730u, 0x0886CFE8u, 0x09A630C0u, 0x09A9CBA4u, 0x09AAA0F0u}) {
        ctx.pc = 0x08800000u;
        ctx.gpr[4] = 17u;
        rt.memory().store32(site, 0xA2800298u); // original store: keep compiled path
        check(!mhp2g::follow_fuc_gather_hook(rt, ctx, memory, site), "Unpatched path intercepted");
        check(ctx.pc == 0x08800000u && ctx.gpr[4] == 17u, "Unpatched state changed");
        rt.memory().store32(site, 0x08000000u | (target >> 2u));
        rt.memory().store32(site + 4u, 0x24840003u); // addiu a0,a0,3 delay slot
        check(mhp2g::follow_fuc_gather_hook(rt, ctx, memory, site), "FUC jump missed");
        check(ctx.pc == target && ctx.gpr[4] == 20u, "Jump/delay slot semantics incorrect");
        rt.memory().store32(site, 0x0C000000u | (target >> 2u));
        check(!mhp2g::follow_fuc_gather_hook(rt, ctx, memory, site), "Unrelated JAL intercepted");
        rt.memory().store32(site, 0x08000000u | (0x08804000u >> 2u));
        check(!mhp2g::follow_fuc_gather_hook(rt, ctx, memory, site), "Unrelated code jump intercepted");
    }
    std::cout << "36 FUC runtime jump/delay-slot/state checks passed\n";
    constexpr auto farm_call = 0x09AAA0F8u;
    rt.memory().store32(farm_call, 0x0C000000u | (0x09AAB770u >> 2u));
    rt.memory().store32(farm_call + 4u, 0u);
    ctx.gpr[20] = 0x08900000u;
    rt.memory().store16(ctx.gpr[20] + 0x298u, 0x108u);
    check(mhp2g::follow_fuc_gather_hook(rt, ctx, memory, farm_call), "Patched farm call missed");
    check(ctx.pc == 0x09AAB770u && ctx.gpr[31] == farm_call + 8u, "Farm call/link incorrect");
    check(rt.memory().load16(ctx.gpr[20] + 0x298u) == 0x108u, "Stale delay slot cleared farm action");
    rt.memory().store32(farm_call + 4u, 0xA2800299u);
    check(!mhp2g::follow_fuc_gather_hook(rt, ctx, memory, farm_call), "Original farm delay slot intercepted");
    std::cout << "4 farm call/delay-slot checks passed\n";
    if (argc == 2) actual_modloader(argv[1]);
    return 0;
}
int main(int argc, char **argv) {
    try { return tests(argc, argv); }
    catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
