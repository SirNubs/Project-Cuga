#include "gpu/game_hud.hpp"

#include "settings/settings.hpp"

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

// The generated units the wrapped functions were compiled into, found by
// CMake in the corpus (MHP2G_HUD_FUNCTIONS in CMakeLists.txt).
#if defined(MHP2G_HUD_FUNCTIONS_INC)
namespace psprecomp {
#define MHP2G_HUD_FUNCTION(address, unit) void unit(Runtime &, AllegrexContext &);
#include MHP2G_HUD_FUNCTIONS_INC
#undef MHP2G_HUD_FUNCTION
} // namespace psprecomp
#endif

namespace mhp2g::gpu::hud {
namespace {

using Clock = std::chrono::steady_clock;

struct Compiled {
    std::uint32_t address;
    psprecomp::Runtime::RecompiledFunction function;
};
constexpr Compiled kCompiled[] = {
#if defined(MHP2G_HUD_FUNCTIONS_INC)
#define MHP2G_HUD_FUNCTION(address, unit) {address, &psprecomp::unit},
#include MHP2G_HUD_FUNCTIONS_INC
#undef MHP2G_HUD_FUNCTION
#endif
    {0u, nullptr},
};

psprecomp::Runtime::RecompiledFunction compiled(std::uint32_t address) {
    for (const Compiled &entry : kCompiled)
        if (entry.address == address) return entry.function;
    return nullptr;
}

// Files a packet into the display list's ordering table: a0 the list object,
// a1 the packet, a2 its length in words, a3 the bucket.
constexpr std::uint32_t kFilePacket = 0x08876874u;

// The text functions the rest of the game calls: every function that can
// reach the four that add to a text queue (0x088E8C4C, 0x088EBF0C,
// 0x088EC3E0, 0x088EC51C) and is called from outside the text code.
constexpr std::array<std::uint32_t, 13> kTextFunctions = {
    0x088EB07Cu,
    0x088EBF0Cu,
    0x088EC060u,
    0x088EC0F8u,
    0x088EC1F0u,
    0x088EC2ECu,
    0x088EC3E0u,
    0x088EC63Cu,
    0x088ECB74u,
    0x088ECC38u,
    0x088ECCE4u,
    0x088ECD9Cu,
    0x088ECE58u,
};
// Called by the queue's drawing (0x088EE17C) after each character, with the
// entry still in s3: the moment button icons the character added to the
// layer's icon list can be told to belong to that entry.
constexpr std::uint32_t kCharacterDrawn = 0x088EA9B0u;
constexpr std::uint32_t kCharacterDrawnFromQueue = 0x088EE2CCu; // its ra there
constexpr std::size_t kWrapped = 2u + kTextFunctions.size();

constexpr std::uint32_t wrapped_address(std::size_t index) {
    return index == 0u ? kFilePacket : index == 1u ? kCharacterDrawn : kTextFunctions[index - 2u];
}

// The functions everything of the HUD is drawn from. The quest's is overlay
// code (game_task.ovl in the slot at 0x0A05E600), so its first words are
// checked too before a match counts.
constexpr std::uint32_t kHubHud = 0x08945D78u;
constexpr std::uint32_t kQuestHud = 0x0A17370Cu;
constexpr std::array<std::uint32_t, 2> kQuestHudWords = {0x27BDFFD0u, 0xAFBF0020u};

// The text context, and in it each queue's length (u8 at +308 + queue) and
// entries (a pointer at +324 + 4 * queue, entries of 12 bytes).
constexpr std::uint32_t kTextContextPointer = 0x09FF0C28u;
constexpr std::uint32_t kQueueLengths = 308u;
constexpr std::uint32_t kQueueEntries = 324u;
constexpr std::uint32_t kQueues = 7u;
constexpr std::uint32_t kEntryBytes = 12u;
// Button icons in queued text are listed per queue while it is drawn (count
// u8 at +315 + queue, entries of 8 bytes from +0x263F8 + 80 * queue) and drawn
// after its glyphs by 0x088ED378, through 0x08900970, which files each icon's
// packet with 0x08900658.
constexpr std::uint32_t kIconCounts = 315u;
constexpr std::uint32_t kIconLists = 0x263F8u;
constexpr std::uint32_t kIconListStride = 80u;
constexpr std::uint32_t kIconBytes = 8u;
constexpr std::uint32_t kIconFiled = 0x08900784u;  // ra of 0x08900658's filing call
constexpr std::uint32_t kIconEmitterSavedRa = 36u; // 0x08900658: sw ra, 36(sp); 48-byte frame
constexpr std::uint32_t kIconEmitterFrame = 48u;
constexpr std::uint32_t kIconEmitterFromIcon = 0x08900A24u;
constexpr std::uint32_t kIconDrawSavedRa = 40u; // 0x08900970: sw ra, 40(sp); sw s0, 32(sp)
constexpr std::uint32_t kIconDrawSavedEntry = 32u;
constexpr std::uint32_t kIconDrawFromList = 0x088ED43Cu;

// Drawing a queued glyph: 0x088EE17C calls 0x088ED984 for it at 0x088EE2AC,
// with the entry in s3, which 0x088ED984 saves at sp+28 (and ra at sp+52);
// 0x088ED984 files the glyph's packet from 0x088EDB10.
constexpr std::uint32_t kGlyphFiled = 0x088EDB18u;     // ra of the filing call
constexpr std::uint32_t kGlyphFromQueue = 0x088EE2B4u; // 0x088ED984's ra when drawing a queue
constexpr std::uint32_t kGlyphSavedRa = 52u;
constexpr std::uint32_t kGlyphSavedEntry = 28u;

struct CodeWord {
    std::uint32_t address;
    std::uint32_t word;
};
// What the addresses above were read off; the switch stays off if any differs.
constexpr CodeWord kSignature[] = {
    {0x08876874u, 0x00073880u}, // sll a3, a3, 2: the ordering table insert
    {0x088768B8u, 0xACE20144u}, // sw v0, 0x144(a3)
    {0x08945D78u, 0x27BDFFE0u}, // addiu sp, sp, -32: the hub HUD
    {0x08945D9Cu, 0xAFBF0010u}, // sw ra, 16(sp)
    {0x088ED984u, 0x27BDFFC0u}, // addiu sp, sp, -64: drawing a glyph
    {0x088ED9ACu, 0xAFBF0034u}, // sw ra, 52(sp)
    {0x088ED9C4u, 0xAFB3001Cu}, // sw s3, 28(sp)
    {0x088EDB10u, 0x0E21DA1Du}, // jal 0x08876874
    {0x088EE2ACu, 0x0E23B661u}, // jal 0x088ED984, drawing a queue
    {0x088E8818u, 0x8E440C28u}, // lw a0, 0xC28(s2): the text context
    {0x088EE2C4u, 0x0E23AA6Cu}, // jal 0x088EA9B0 after each character
    {0x088EE370u, 0x344263F0u}, // ori v0, v0, 0x63F0: the icon lists
    {0x088EE32Cu, 0x92B1013Bu}, // lbu s1, 315(s5): the icon count
    {0x088ED434u, 0x0E24025Cu}, // jal 0x08900970, drawing an icon
    {0x0890097Cu, 0x27BDFFD0u}, // addiu sp, sp, -48
    {0x08900988u, 0xAFBF0028u}, // sw ra, 40(sp)
    {0x089009A0u, 0xAFB00020u}, // sw s0, 32(sp)
    {0x08900A1Cu, 0x0E240196u}, // jal 0x08900658
    {0x08900658u, 0x27BDFFD0u}, // addiu sp, sp, -48
    {0x08900674u, 0xAFBF0024u}, // sw ra, 36(sp)
    {0x0890077Cu, 0x0E21DA1Du}, // jal 0x08876874
};

// Where a function starts, found from a return address into it.
struct Frame {
    std::uint32_t start;
    std::uint32_t size;
    std::uint32_t ra_offset; // ~0u: the function does not save ra
    std::uint32_t start_word;
};

struct State {
    bool hidden{};
    bool free_camera{};
    bool shown_in_flight{}; // the player showed the HUD while the free camera hid it
    bool installed{};
    bool checked{};
    bool available{};
    std::string note;
    Clock::time_point note_until{};
    std::array<psprecomp::Runtime::RecompiledFunction, kWrapped> originals{};
    // HUD packets by start address, to their end. The GE draws a list while
    // the game fills the next one elsewhere, and the photo mode draws the last
    // shown one again, so a packet's range is kept until another packet is
    // filed over it.
    std::map<std::uint32_t, std::uint32_t> packets;
    // Text queue entries the HUD asked for, and the queues' lengths at the
    // last watched call.
    std::vector<std::uint32_t> text_entries;
    std::array<std::uint8_t, kQueues> lengths{};
    // Icons in HUD text, and each queue's icon count when last looked at.
    std::vector<std::uint32_t> icons;
    std::array<std::uint8_t, kQueues> icon_counts{};
    std::uint64_t icon_frame{~0ull};
    bool last_call_hud{};
    std::uint32_t quest_hud_mismatches{};
    std::unordered_map<std::uint32_t, Frame> frames;
    // MHP2G_TRACE_HUD
    std::uint64_t frame{};
    std::uint64_t trace_first{~0ull};
    std::uint64_t trace_every{};
};

State &state() {
    static State s;
    return s;
}

bool tracing(const State &s) {
    if (s.frame == s.trace_first) return true;
    return s.trace_every != 0u && s.frame > s.trace_first && (s.frame - s.trace_first) % s.trace_every == 0u;
}

// The frame of the function a return address points into: the nearest
// addiu sp, sp, -N before it, and where after that it saves ra.
const Frame *frame_of(State &s, const psprecomp::GuestMemory &memory, std::uint32_t ra) {
    if (auto found = s.frames.find(ra); found != s.frames.end()) {
        // Overlays replace code: the cached start must still hold what it did.
        if (memory.load32(found->second.start) == found->second.start_word) return &found->second;
        s.frames.erase(found);
    }
    Frame frame{};
    std::uint32_t at = (ra - 8u) & ~3u;
    for (std::uint32_t steps = 0u;; at -= 4u, ++steps) {
        if (steps == 4096u || !memory.contains(at, 4u)) return nullptr;
        const std::uint32_t word = memory.load32(at);
        if ((word >> 16u) == 0x27BDu && (word & 0x8000u) != 0u) {
            frame.start = at;
            frame.start_word = word;
            frame.size = 0x10000u - (word & 0xFFFFu);
            break;
        }
    }
    frame.ra_offset = ~0u;
    for (std::uint32_t pc = frame.start + 4u, steps = 0u; steps < 64u && memory.contains(pc, 4u); pc += 4u, ++steps) {
        const std::uint32_t word = memory.load32(pc);
        if ((word >> 16u) == 0xAFBFu) {
            frame.ra_offset = word & 0xFFFFu;
            break;
        }
        if (word == 0x03E00008u) break; // jr ra: returns without saving it
    }
    if (s.frames.size() > 4096u) s.frames.clear();
    return &s.frames.emplace(ra, frame).first->second;
}

bool quest_hud_here(State &s, const psprecomp::GuestMemory &memory) {
    for (std::uint32_t i = 0; i < kQuestHudWords.size(); ++i) {
        const std::uint32_t word = memory.load32(kQuestHud + i * 4u);
        if (word == kQuestHudWords[i]) continue;
        if (s.quest_hud_mismatches++ == 0u)
            std::cout << "[hud] unexpected code at 0x" << std::hex << kQuestHud + i * 4u << ": 0x" << word << std::dec
                      << "; not counted as the quest HUD\n";
        return false;
    }
    return true;
}

// Whether a HUD function is on the guest's call stack; the chain of return
// addresses and function starts goes to `line` when one is given.
bool from_hud(
    State &s, const psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx, std::string *line) {
    std::uint32_t ra = ctx.gpr[31];
    std::uint32_t sp = ctx.gpr[29];
    bool hud = false;
    for (int depth = 0; depth < 12; ++depth) {
        const Frame *frame = frame_of(s, memory, ra);
        if (frame == nullptr) break;
        if (line != nullptr) {
            char text[24];
            std::snprintf(text, sizeof(text), " %08X@%08X", ra, frame->start);
            *line += text;
        }
        if (frame->start == kHubHud || (frame->start == kQuestHud && quest_hud_here(s, memory))) {
            hud = true;
            if (line == nullptr) break;
        }
        if (frame->ra_offset == ~0u || !memory.contains(sp + frame->ra_offset, 4u)) break;
        ra = memory.load32(sp + frame->ra_offset);
        sp += frame->size;
    }
    return hud;
}

// The queue entries added since the last watched call belong to the code
// that made it: to the HUD when a HUD function was on the stack then.
void settle_text(State &s, const psprecomp::GuestMemory &memory) {
    const std::uint32_t context = memory.load32(kTextContextPointer);
    if (!memory.contains(context + kQueueLengths, kQueues) || !memory.contains(context + kQueueEntries, kQueues * 4u))
        return;
    for (std::uint32_t queue = 0; queue < kQueues; ++queue) {
        const std::uint8_t length = memory.load8(context + kQueueLengths + queue);
        const std::uint32_t entries = memory.load32(context + kQueueEntries + queue * 4u);
        std::uint8_t &known = s.lengths[queue];
        if (length < known) {
            // Drawn and emptied: its entries are someone else's from now on.
            const std::uint32_t end = entries + 256u * kEntryBytes;
            std::erase_if(s.text_entries, [&](std::uint32_t e) { return e >= entries && e < end; });
            known = 0u;
        }
        if (s.last_call_hud)
            for (std::uint32_t i = known; i < length; ++i) s.text_entries.push_back(entries + i * kEntryBytes);
        known = length;
    }
}

void note_packet(State &s, std::uint32_t packet, std::uint32_t words, bool hud) {
    const std::uint32_t end = packet + words * 4u;
    // Whatever was filed where this packet now lies is gone.
    auto it = s.packets.lower_bound(packet);
    if (it != s.packets.begin() && std::prev(it)->second > packet) --it;
    while (it != s.packets.end() && it->first < end) it = s.packets.erase(it);
    if (hud) s.packets.emplace(packet, end);
    if (s.packets.size() > 65536u) s.packets.clear(); // never expected; bounds a runaway
}

void on_file_packet(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const psprecomp::GuestMemory &memory = runtime.memory();
    settle_text(s, memory);
    const bool trace = tracing(s);
    std::string chain;
    bool hud = from_hud(s, memory, ctx, trace ? &chain : nullptr);
    // Text a HUD function queues after drawing a packet of its own, as the
    // name plate does, is the HUD's too.
    s.last_call_hud = hud;
    const std::uint32_t sp = ctx.gpr[29];
    std::uint32_t entry = 0u;
    if (!hud && ctx.gpr[31] == kGlyphFiled && memory.contains(sp + kGlyphSavedRa, 4u) &&
        memory.load32(sp + kGlyphSavedRa) == kGlyphFromQueue) {
        entry = memory.load32(sp + kGlyphSavedEntry);
        hud = std::find(s.text_entries.begin(), s.text_entries.end(), entry) != s.text_entries.end();
    }
    if (!hud && ctx.gpr[31] == kIconFiled && memory.contains(sp + kIconEmitterFrame + kIconDrawSavedRa, 4u) &&
        memory.load32(sp + kIconEmitterSavedRa) == kIconEmitterFromIcon &&
        memory.load32(sp + kIconEmitterFrame + kIconDrawSavedRa) == kIconDrawFromList) {
        entry = memory.load32(sp + kIconEmitterFrame + kIconDrawSavedEntry);
        hud = std::find(s.icons.begin(), s.icons.end(), entry) != s.icons.end();
    }
    note_packet(s, ctx.gpr[5], ctx.gpr[6], hud);
    if (trace) {
        char head[160];
        std::snprintf(head, sizeof(head), "[hud] f=%llu packet=%08X words=%u bucket=%u list=%08X %s |",
            static_cast<unsigned long long>(s.frame), ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[4],
            hud ? "HUD" : "-");
        std::cout << head << chain;
        if (entry != 0u) {
            char text[40];
            std::snprintf(text, sizeof(text), " | queued text %08X", entry);
            std::cout << text;
        }
        std::cout << "\n";
    }
}

// After a character of queued text is drawn: icons it added to the queue's
// icon list are the HUD's when its entry is.
void on_character_drawn(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &ctx) {
    State &s = state();
    if (ctx.gpr[31] != kCharacterDrawnFromQueue) return;
    const psprecomp::GuestMemory &memory = runtime.memory();
    const std::uint32_t context = memory.load32(kTextContextPointer);
    const std::uint32_t entry = ctx.gpr[19];
    // The lists are filled anew every frame.
    if (s.icon_frame != s.frame) {
        s.icon_frame = s.frame;
        s.icon_counts.fill(0u);
        s.icons.clear();
    }
    if (!memory.contains(context + kQueueEntries, kQueues * 4u) || !memory.contains(context + kIconCounts, kQueues))
        return;
    for (std::uint32_t queue = 0; queue < kQueues; ++queue) {
        const std::uint32_t entries = memory.load32(context + kQueueEntries + queue * 4u);
        if (entry < entries || entry >= entries + 256u * kEntryBytes) continue;
        const std::uint8_t count = memory.load8(context + kIconCounts + queue);
        const std::uint32_t list = context + kIconLists + queue * kIconListStride;
        std::uint8_t &known = s.icon_counts[queue];
        if (count < known) {
            std::erase_if(s.icons, [&](std::uint32_t e) { return e >= list && e < list + kIconListStride; });
            known = 0u;
        }
        if (std::find(s.text_entries.begin(), s.text_entries.end(), entry) != s.text_entries.end())
            for (std::uint32_t i = known; i < count; ++i) s.icons.push_back(list + i * kIconBytes);
        known = count;
        return;
    }
}

void on_text_call(std::size_t which, psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &ctx) {
    State &s = state();
    const psprecomp::GuestMemory &memory = runtime.memory();
    settle_text(s, memory);
    const bool trace = tracing(s);
    std::string chain;
    s.last_call_hud = from_hud(s, memory, ctx, trace ? &chain : nullptr);
    if (trace)
        std::cout << "[hud] f=" << s.frame << " text 0x" << std::hex << wrapped_address(which) << std::dec
                  << (s.last_call_hud ? " HUD" : " -") << " |" << chain << "\n";
}

template <std::size_t I> void wrapper(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if constexpr (I == 0u)
        on_file_packet(runtime, ctx);
    else if constexpr (I == 1u)
        on_character_drawn(runtime, ctx);
    else
        on_text_call(I, runtime, ctx);
    // The original, with the same context and return address.
    state().originals[I](runtime, ctx);
}

template <std::size_t... I> bool install_all(psprecomp::Runtime &runtime, std::index_sequence<I...>) {
    State &s = state();
    ((s.originals[I] = compiled(wrapped_address(I))), ...);
    for (const auto original : s.originals)
        if (original == nullptr) return false;
    (runtime.register_function(wrapped_address(I), &wrapper<I>, "mhp2g_hud"), ...);
    return true;
}

bool check_code(const psprecomp::Runtime &runtime) {
    for (const CodeWord &expected : kSignature) {
        const std::uint32_t found = runtime.memory().load32(expected.address);
        if (found == expected.word) continue;
        std::cout << "[hud] game code differs at 0x" << std::hex << expected.address << ": 0x" << found
                  << ", expected 0x" << expected.word << std::dec << "; hiding the HUD is unavailable\n";
        return false;
    }
    return true;
}

bool free_camera_hides() {
    return settings::current().free_camera_hide_hud;
}

} // namespace

void frame(psprecomp::Runtime &runtime, std::uint64_t presented_frames) {
    State &s = state();
    s.frame = presented_frames;
    if (!s.checked) {
        s.checked = true;
        if (const char *text = std::getenv("MHP2G_TRACE_HUD"); text != nullptr && *text != '\0') {
            char *end = nullptr;
            s.trace_first = std::strtoull(text, &end, 10);
            s.trace_every = end != nullptr && *end == '/' ? std::strtoull(end + 1, nullptr, 10) : 0ull;
        }
        if (const char *text = std::getenv("MHP2G_HIDE_HUD"); text != nullptr && *text == '1') s.hidden = true;
        s.available = check_code(runtime);
    }
    // Only at the flip, from an import: no generated frame is live on the
    // host stack, so the dispatch tables may change. Put in place once first
    // needed, then kept, so that hiding and showing the HUD again works at
    // once, in the photo mode too, whose frame was sorted when it was drawn.
    const bool wanted = s.hidden || (s.free_camera && free_camera_hides()) || s.trace_first != ~0ull;
    if (!s.installed && wanted && s.available) {
        s.installed = true;
        if (!install_all(runtime, std::make_index_sequence<kWrapped>{})) {
            s.available = false;
            std::cout << "[hud] the game's 2D functions are not in the generated code; hiding the HUD is "
                         "unavailable\n";
            return;
        }
        std::cout << "[hud] watching the game's 2D draws" << std::endl;
    }
}

namespace {

bool hidden_now(const State &s) {
    return s.hidden || (s.free_camera && free_camera_hides() && !s.shown_in_flight);
}

void note_change(State &s, bool before) {
    const bool after = hidden_now(s);
    if (after == before) return;
    s.note = after ? "HUD hidden" : "HUD shown";
    s.note_until = Clock::now() + std::chrono::milliseconds(1500);
    std::cout << "[hud] " << (after ? "hidden" : "shown") << std::endl;
}

} // namespace

void set_hidden(bool hidden) {
    State &s = state();
    const bool before = hidden_now(s);
    s.hidden = hidden;
    if (!hidden && s.free_camera && free_camera_hides()) s.shown_in_flight = true;
    note_change(s, before);
}

// Turns what is on screen around: while the free camera hides the HUD,
// the key shows it for the rest of the flight.
void toggle() {
    set_hidden(!hidden_now(state()));
}

bool hidden() {
    return hidden_now(state());
}

void set_free_camera(bool active) {
    State &s = state();
    // No note: the free camera's own line says it flies.
    s.free_camera = active;
    if (!active) s.shown_in_flight = false;
}

bool active() {
    const State &s = state();
    return s.installed && s.available && hidden_now(s);
}

bool available() {
    const State &s = state();
    return !s.checked || s.available;
}

double note_seconds_left() {
    const State &s = state();
    return std::chrono::duration<double>(s.note_until - Clock::now()).count();
}

std::string note_text() {
    return state().note;
}

bool hides(std::uint32_t command_address, std::uint32_t call_return) {
    if (!active()) return false;
    const State &s = state();
    const auto inside = [&](std::uint32_t address) {
        if (address == 0u) return false;
        auto it = s.packets.upper_bound(address);
        return it != s.packets.begin() && address < std::prev(it)->second;
    };
    return inside(command_address) || inside(call_return);
}

} // namespace mhp2g::gpu::hud
