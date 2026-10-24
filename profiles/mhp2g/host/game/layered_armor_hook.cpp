#include "game/layered_armor.hpp"

#include "game/guest_ram.hpp"
#include "settings/settings.hpp"

#include "psprecomp/runtime.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

// The generated unit the game's model lookup was compiled into, found by CMake
// in the corpus (MHP2G_LAYERED_FUNCTIONS in CMakeLists.txt).
#if defined(MHP2G_LAYERED_FUNCTIONS_INC)
namespace psprecomp {
#define MHP2G_LAYERED_FUNCTION(address, unit) void unit(Runtime &, AllegrexContext &);
#include MHP2G_LAYERED_FUNCTIONS_INC
#undef MHP2G_LAYERED_FUNCTION
} // namespace psprecomp
#endif

namespace mhp2g::game::layered {
namespace {

struct Compiled {
    std::uint32_t address;
    psprecomp::Runtime::RecompiledFunction function;
    const char *name;
};
constexpr Compiled kCompiled[] = {
#if defined(MHP2G_LAYERED_FUNCTIONS_INC)
#define MHP2G_LAYERED_FUNCTION(address, unit) {address, &psprecomp::unit, #unit},
#include MHP2G_LAYERED_FUNCTIONS_INC
#undef MHP2G_LAYERED_FUNCTION
#endif
    {0u, nullptr, nullptr},
};

const Compiled *compiled(std::uint32_t address) {
    for (const Compiled &entry : kCompiled)
        if (entry.address == address && entry.function != nullptr) return &entry;
    return nullptr;
}

// The first instructions of the lookup, as traced: the record's address from
// a1 (a1 * 252 + 0x30), the check that the part is below 7, and the jump
// table by part. A different executable does not get the replacement.
struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};
constexpr CodeWord kSignature[] = {
    {0x08869778u, 0x00051200u},  // sll v0,a1,8
    {0x0886977Cu, 0x00052880u},  // sll a1,a1,2
    {0x08869780u, 0x00451023u},  // subu v0,v0,a1
    {0x08869784u, 0x24420030u},  // addiu v0,v0,0x30
    {0x08869788u, 0x2CC30007u},  // sltiu v1,a2,7
    {0x088697C8u, 0x94A20030u},  // lhu v0,0x30(a1): the waist's id
    {0x08869800u, 0x94A2001Cu},  // lhu v0,0x1C(a1): the chest's id
    {0x08869858u, 0x94A20044u},  // lhu v0,0x44(a1): the head's id
    {0x08869890u, 0x94A2003Au},  // lhu v0,0x3A(a1): the legs' id
    {0x088698C8u, 0x94A20026u},  // lhu v0,0x26(a1): the arms' id
};

// What starting a hunter's load over relies on: the load step and the game's
// own request.
constexpr CodeWord kLoadSignature[] = {
    {0x088A5598u, 0x8E230B80u},  // lw v1,0xB80(s1): the step reads the state
    {0x088A55BCu, 0xAE320B80u},  // sw s2,0xB80(s1): 0 becomes 1, the first part
    {0x088A5610u, 0x8E250B84u},  // lw a1,0xB84(s1): the part
    {0x088A5618u, 0x8C6200A8u},  // lw v0,0xA8(v1): the object's file function for it
    {0x088A5638u, 0x8C430B8Cu},  // lw v1,0xB8C(v0): compared with the file loaded
    {0x088A53DCu, 0x28420003u},  // slti v0,v0,3: the request cancels a load below 3
    {0x088A5404u, 0xAE000B84u},  // sw zero,0xB84(s0): and sets the part
    {0x088A5408u, 0xAE000B80u},  // sw zero,0xB80(s0): and the state to 0
    {0x088BD258u, 0x8C620094u},  // lw v0,0x94(v1): the driver steps through the vtable
};

struct State {
    bool checked{};
    bool usable{};
    bool installed{};
    bool trace{};
    const Compiled *original{};
    const Compiled *step{};
    bool step_installed{};
    // The settings as of the last flip; the menu changes them between frames.
    bool on{};
    Pieces pieces{kReal, kReal, kReal, kReal, kReal};
    // The look the hunter played here was last loaded with: the one at the
    // first flip, then the one of each load the game ran on it.
    bool loaded_known{};
    Pieces loaded{kReal, kReal, kReal, kReal, kReal};
};
State &state() {
    static State s;
    return s;
}

void model_of_part(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (s.on) {
        const GuestRam ram(runtime.memory());
        const std::optional<std::uint16_t> model = replacement_model(ram, ctx.gpr[4], ctx.gpr[5], ctx.gpr[6], s.pieces);
        if (model) {
            if (s.trace)
                std::cout << "[layered] " << part_label(static_cast<std::uint8_t>(ctx.gpr[6])) << ": model " << *model
                          << " instead of the worn piece's (hunter " << ctx.gpr[5] << ", from 0x" << std::hex
                          << ctx.gpr[31] << std::dec << ")\n";
            // What the game's function leaves for its caller: the model in v0,
            // back to the return address. It only reads memory.
            ctx.gpr[2] = *model;
            ctx.pc = ctx.gpr[31];
            return;
        }
    }
    s.original->function(runtime, ctx);
}

// The look the hunter is to be drawn in; the real one while off.
Pieces wanted(const State &s) { return s.on ? s.pieces : Pieces{kReal, kReal, kReal, kReal, kReal}; }

void load_step(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    State &s = state();
    GuestRam ram(runtime.memory());
    const std::uint32_t object = ctx.gpr[4];
    if (own_hunter(ram, object)) {
        const Pieces look = wanted(s);
        if (!load_done(ram, object)) {
            // A load the game started: it asks for the files with the look as
            // it is now.
            s.loaded = look;
        } else if (s.loaded != look) {
            restart_load(ram, object);
            s.loaded = look;
            if (s.trace) std::cout << "[layered] the look changed: the hunter loads its models again\n";
        }
    }
    s.step->function(runtime, ctx);
}

bool check(psprecomp::Runtime &runtime) {
    State &s = state();
    s.original = compiled(kModelOfPart);
    if (s.original == nullptr) {
        std::cerr << "[layered] the game's armor model lookup is not in the generated code; layered armor "
                     "unavailable\n";
        return false;
    }
    for (const CodeWord &expected : kSignature) {
        const std::uint32_t found = runtime.memory().load32(expected.address);
        if (found == expected.word) continue;
        std::cerr << "[layered] game code differs at 0x" << std::hex << expected.address << ": 0x" << found
                  << ", expected 0x" << expected.word << std::dec << "; layered armor unavailable\n";
        return false;
    }
    s.step = compiled(kLoadStep);
    for (const CodeWord &expected : kLoadSignature)
        if (runtime.memory().load32(expected.address) != expected.word) s.step = nullptr;
    if (s.step == nullptr)
        std::cerr << "[layered] the hunter's load step is not as traced; a new look shows after a restart\n";
    return true;
}

} // namespace

void frame(psprecomp::Runtime &runtime) {
    State &s = state();
    const settings::Settings &settings = settings::current();
    s.on = settings.layered_armor;
    s.pieces = settings.layered_pieces;
    // The look the game starts with: the hunter's first load asks for it.
    if (!s.loaded_known) {
        s.loaded_known = true;
        s.loaded = wanted(s);
    }
    // Off from the start: nothing is registered, and the game runs exactly as
    // it was built.
    if (!s.on && !s.installed) return;
    if (!s.checked) {
        s.checked = true;
        const char *text = std::getenv("MHP2G_TRACE_LAYERED_ARMOR");
        s.trace = text != nullptr && *text != '\0' && std::string(text) != "0";
        s.usable = check(runtime);
    }
    if (!s.usable) return;
    // Only ever at the flip, from an import: no generated code is running, so
    // the dispatch tables can change here.
    if (s.on && !s.installed) {
        runtime.register_function(kModelOfPart, &model_of_part, "mhp2g_layered_armor");
        s.installed = true;
        // Stays once in place, to show the real look again after turning off.
        if (s.step != nullptr && !s.step_installed) {
            runtime.register_function(kLoadStep, &load_step, "mhp2g_layered_armor_load");
            s.step_installed = true;
        }
        std::cout << "[layered] on\n";
    } else if (!s.on && s.installed) {
        // The game's own lookup again, as the generated code registered it.
        runtime.register_function(kModelOfPart, s.original->function, s.original->name);
        s.installed = false;
        std::cout << "[layered] off\n";
    }
}

bool installed() { return state().installed; }

} // namespace mhp2g::game::layered
