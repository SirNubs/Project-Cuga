#include "hle/hle_common.hpp"
#include "adhoc/client.hpp"
#include "adhoc/server.hpp"
#include "adhoc/session.hpp"
#include "adhoc/sockets.hpp"
#include "settings/settings.hpp"
#include "fonts/game_font.hpp"
#include "install/user_data.hpp"
#include <cstdlib>
#include <cstring>
#include "hle/utility_dialog.hpp"
#include <thread>
#include "kernel/kernel.hpp"
#include "psprecomp/common.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char *contract) {
    if (!condition) throw std::runtime_error(contract);
}
void memory_contracts() {
    psprecomp::Runtime runtime(64u * 1024u * 1024u);
    mhp3rd::Kernel kernel;
    kernel.install(runtime, 0u, 0x08804123u);
    const auto total = kernel.free_memory();
    check(total == mhp3rd::kUserMemoryEnd - 0x08805000u, "image end rounds up to page");
    check(static_cast<unsigned>(kernel.allocate_block("zero", 0, 0, 0)) == mhp3rd::error::kIllegalArgument,
        "zero allocation rejected");
    check(static_cast<unsigned>(kernel.allocate_block("bad", 5, 256, 0)) == mhp3rd::error::kIllegalMemblockType,
        "unknown partition type rejected");
    check(static_cast<unsigned>(kernel.allocate_block("bad", 3, 256, 384)) == mhp3rd::error::kIllegalArgument,
        "alignment must be a power of two");
    const auto low = kernel.allocate_block("low", 0, 1, 0);
    const auto high = kernel.allocate_block("high", 1, 257, 0);
    check(low > 0 && high > 0, "low/high allocations succeed");
    check(kernel.find_block(low)->address == 0x08805000u && kernel.find_block(low)->size == 256u,
        "low allocation rounds size");
    check(
        kernel.find_block(high)->address == mhp3rd::kUserMemoryEnd - 512u, "high allocation starts at upper boundary");
    const auto middle = kernel.allocate_block("middle", 2, 512, 0x0900017fu);
    check(middle > 0 && kernel.find_block(middle)->address == 0x09000100u, "fixed allocation rounds address down");
    check(static_cast<unsigned>(kernel.allocate_block("overlap", 2, 256, 0x09000100u)) == mhp3rd::error::kNoMemory,
        "overlapping fixed allocations fail");
    const auto aligned = kernel.allocate_block("aligned", 3, 256, 4096);
    const auto aligned_high = kernel.allocate_block("aligned-high", 4, 256, 4096);
    check(aligned > 0 && aligned_high > 0, "aligned allocations succeed");
    check((kernel.find_block(aligned)->address & 4095u) == 0 && (kernel.find_block(aligned_high)->address & 4095u) == 0,
        "both allocation directions honor alignment");
    for (auto uid : {middle, low, aligned_high, high, aligned})
        check(kernel.free_block(uid) == 0, "free succeeds in shuffled order");
    check(kernel.free_memory() == total, "all free fragments coalesce without losing bytes");
    check(kernel.find_block(low) == nullptr &&
            static_cast<unsigned>(kernel.free_block(low)) == mhp3rd::error::kIllegalMemblock,
        "double free rejected");
    const auto whole = kernel.allocate_block("whole", 0, total, 0);
    check(whole > 0 && kernel.free_memory() == 0, "coalesced range accepts entire region");
    check(static_cast<unsigned>(kernel.allocate_block("exhausted", 0, 256, 0)) == mhp3rd::error::kNoMemory,
        "exhaustion rejected");
    check(kernel.free_block(whole) == 0, "entire region can be returned");
}
class Fixture {
public:
    static constexpr std::uint32_t text = 0x08810000u;
    static constexpr std::uint32_t output = 0x08811000u;
    psprecomp::Runtime runtime{64u * 1024u * 1024u};
    mhp3rd::Kernel &kernel = mhp3rd::kernel();
    psprecomp::AllegrexContext &ctx = runtime.cpu();
    std::map<std::pair<std::string, std::string>, std::uint32_t> nids;
    unsigned return_address = 0x08822000u;
    Fixture() {
        kernel = mhp3rd::Kernel{};
        kernel.install(runtime, 0x08801000u, 0x08820000u);
        const char *csv_path = std::getenv("PSPRECOMP_TEST_NIDS_CSV");
        runtime.nids().load_csv(csv_path ? csv_path : PSPRECOMP_TEST_NIDS_CSV);
        for (const auto &symbol : runtime.nids().all()) nids[{symbol.library, symbol.name}] = symbol.nid;
        mhp3rd::HleRegistrar hle(runtime);
        mhp3rd::register_threadman(hle);
        mhp3rd::register_sysmem(hle);
        mhp3rd::register_system(hle);
        kernel.start_loader_thread(ctx, 0x08820000u, 0);
        string(text, "contract");
    }
    ~Fixture() {
        kernel = mhp3rd::Kernel{};
        psprecomp::set_runtime_starvation_hook(nullptr, 0);
    }
    void string(std::uint32_t address, std::string_view value) {
        mhp3rd::write_cstring(runtime.memory(), address, value, 512);
    }
    std::uint32_t call(std::string library, std::string name, std::initializer_list<std::uint32_t> args = {}) {
        for (unsigned i = 0; i < 8; ++i) ctx.set_gpr(i + 4, 0);
        unsigned i = 0;
        for (const auto value : args) ctx.set_gpr(i++ + 4, value);
        ctx.set_gpr(31, return_address);
        runtime.invoke_import(library, nids.at({library, name}), ctx);
        return ctx.gpr[2];
    }
    std::uint32_t thread(std::string name, std::initializer_list<std::uint32_t> args = {}) {
        return call("ThreadManForUser", std::move(name), args);
    }
    std::uint32_t helper(std::uint32_t priority = 0x30u) {
        const auto uid = kernel.create_thread("helper", 0x08824000u, priority, 0x1000u, 0, 0x08801000u);
        check(uid > 0 && kernel.start_thread(ctx, uid, 0, 0) == 0, "helper thread starts");
        return static_cast<std::uint32_t>(uid);
    }
};

void common_hle_contracts() {
    Fixture f;
    auto &memory = f.runtime.memory();
    f.string(Fixture::text, "prefix-suffix");
    check(mhp3rd::read_cstring(memory, 0).empty(), "null guest string is empty");
    check(mhp3rd::read_cstring(memory, Fixture::text, 6) == "prefix", "guest string obeys maximum length");
    memory.store8(Fixture::output, 0x5a);
    mhp3rd::write_cstring(memory, Fixture::output, "ignored", 0);
    mhp3rd::write_cstring(memory, 0, "ignored", 8);
    check(memory.load8(Fixture::output) == 0x5a, "zero capacity leaves guest memory untouched");
    mhp3rd::write_cstring(memory, Fixture::output, "longer", 4);
    check(mhp3rd::read_cstring(memory, Fixture::output) == "lon" && memory.load8(Fixture::output + 3) == 0,
        "guest string truncation always includes terminator");
    mhp3rd::write_cstring(memory, Fixture::output, "longer", 1);
    check(memory.load8(Fixture::output) == 0, "one-byte capacity writes an empty terminated string");
    mhp3rd::store64(memory, Fixture::output, 0xfedcba9876543210ull);
    check(memory.load32(Fixture::output) == 0x76543210 && memory.load32(Fixture::output + 4) == 0xfedcba98,
        "64-bit store uses little-endian word ordering");
    f.ctx.set_gpr(6, 0x76543210);
    f.ctx.set_gpr(7, 0xfedcba98);
    check(mhp3rd::arg64(f.ctx, 2) == 0xfedcba9876543210ull, "EABI paired arguments preserve all 64 bits");
    mhp3rd::HleRegistrar hle(f.runtime);
    check(!hle.try_add("missing-public-library", "missing-public-name", [](auto &, auto &) {}) && hle.count() == 0,
        "unknown optional binding leaves registrar empty");
    bool rejected = false;
    try {
        hle.add("missing-public-library", "missing-public-name", [](auto &, auto &) {});
    } catch (const psprecomp::Error &error) {
        rejected = std::string(error.what()).find("missing from configs/nids.csv") != std::string::npos;
    }
    check(rejected, "required unknown binding reports its registry error");
    const auto nid = f.nids.at({"ThreadManForUser", "sceKernelGetThreadId"});
    hle.add("ThreadManForUser", "sceKernelGetThreadId", [](auto &, auto &ctx) { ctx.set_gpr(2, 0x12345678); });
    check(hle.count() == 1 && hle.bound("ThreadManForUser", nid) && !hle.bound("missing-public-library", nid),
        "registrar tracks exact library and NID identity");
    check(f.thread("sceKernelGetThreadId") == 0x12345678, "registered handler runs through actual import dispatch");
}
void scheduler_contracts() {
    Fixture f;
    auto &k = f.kernel;
    auto &ctx = f.ctx;
    const auto loader = k.current_uid();
    check(ctx.gpr[28] == 0x08801000u && ctx.gpr[31] == mhp3rd::kThreadExitStub,
        "loader starts with GP and exit trampoline");
    check(f.runtime.memory().load32(ctx.gpr[26] + 0xC0) == static_cast<unsigned>(loader),
        "thread control block identifies current thread");
    for (auto entry : {0u, 0x08824001u})
        check(
            static_cast<unsigned>(k.create_thread("invalid", entry, 0x30, 4096, 0, 0)) == mhp3rd::error::kIllegalEntry,
            "bad entry rejected");
    for (auto priority : {0u, 128u})
        check(static_cast<unsigned>(k.create_thread("invalid", 0x08824000, priority, 4096, 0, 0)) ==
                mhp3rd::error::kIllegalPriority,
            "bad priority rejected");
    check(static_cast<unsigned>(k.create_thread("invalid", 0x08824000, 0x30, 511, 0, 0)) ==
            mhp3rd::error::kIllegalStackSize,
        "undersized stack rejected");
    check(
        static_cast<unsigned>(k.start_thread(ctx, -1, 0, 0)) == mhp3rd::error::kUnknownThid, "unknown start rejected");
    const auto high = k.create_thread("high", 0x08824000, 0x10, 4096, 0, 0x1234);
    f.string(Fixture::output, "arguments");
    check(k.start_thread(ctx, high, 10, Fixture::output) == 0, "start copies arguments");
    const auto *t = k.find_thread(high);
    check(t->context.gpr[4] == 10 && mhp3rd::read_cstring(f.runtime.memory(), t->context.gpr[5]) == "arguments",
        "thread argument bytes and size retained");
    check(static_cast<unsigned>(k.start_thread(ctx, high, 0, 0)) == mhp3rd::error::kNotDormant,
        "running thread cannot start twice");
    check(
        static_cast<unsigned>(k.delete_thread(high)) == mhp3rd::error::kNotDormant, "active thread cannot be deleted");
    ctx.set_gpr(31, 0x08823000);
    k.finish(ctx, 42);
    check(k.current_uid() == high && ctx.pc == 0x08824000, "higher priority thread preempts on import completion");
    check(k.find_thread(loader)->context.pc == 0x08823000 && k.find_thread(loader)->context.gpr[2] == 42,
        "preemption saves return address and result");
    check(static_cast<unsigned>(k.terminate_thread(ctx, high, false)) == mhp3rd::error::kIllegalThid,
        "cannot terminate current thread");
    k.delay_current(ctx, 2500, 77);
    check(k.current_uid() == loader && k.find_thread(high)->status == mhp3rd::ThreadStatus::Waiting,
        "delay yields to ready loader");
    k.delay_current(ctx, 3000);
    check(k.now_us() == 2500 && k.current_uid() == high && ctx.gpr[2] == 77,
        "earliest deadline advances virtual clock and resumes result");
    k.exit_current_thread(ctx, 123, false);
    check(k.find_thread(high)->exit_status == 123 && k.current_uid() == loader && k.now_us() == 3000,
        "exit status retained and next timed thread resumes");
    check(k.delete_thread(high) == 0 && k.find_thread(high) == nullptr,
        "dormant thread deletion removes stack and identity");
    check(static_cast<unsigned>(k.delete_thread(high)) == mhp3rd::error::kUnknownThid, "repeated thread delete fails");
    check(static_cast<unsigned>(k.change_priority(ctx, -1, 1)) == mhp3rd::error::kUnknownThid,
        "unknown priority target rejected");
    check(static_cast<unsigned>(k.change_priority(ctx, 0, 128)) == mhp3rd::error::kIllegalPriority,
        "out of range changed priority rejected");
    check(k.change_priority(ctx, 0, 0) == 0, "zero priority inherits current priority");
    check(f.thread("sceKernelGetThreadId") == static_cast<unsigned>(loader), "thread HLE returns live identity");
    check(f.thread("sceKernelGetThreadCurrentPriority") == 0x20, "thread HLE returns loader priority");
    check(f.thread("sceKernelSuspendDispatchThread") == 1 && !k.dispatch_enabled(),
        "dispatch suspension returns previous flag");
    check(f.thread("sceKernelSuspendDispatchThread") == 0, "nested suspension reports disabled state");
    check(f.thread("sceKernelResumeDispatchThread", {1}) == 0 && k.dispatch_enabled(),
        "dispatch resume restores scheduler");
    check(f.thread("sceKernelWakeupThread", {static_cast<unsigned>(loader)}) == 0, "self wake records a wake token");
    check(f.thread("sceKernelSleepThread") == 0 && k.current_uid() == loader,
        "sleep consumes wake token without blocking");
    check(f.thread("sceKernelWakeupThread", {static_cast<unsigned>(loader)}) == 0 &&
            f.thread("sceKernelSleepThreadCB") == 0,
        "callback-aware sleep consumes wake token");
    check(f.thread("sceKernelWakeupThread", {0xFFFFFFFFu}) == mhp3rd::error::kUnknownThid,
        "wake unknown thread rejected");
    check(f.thread("sceKernelDelayThread", {50}) == 0 && k.now_us() == 3050, "delay HLE charges exact virtual time");
    check(
        f.thread("sceKernelDelayThreadCB", {50}) == 0 && k.now_us() == 3100, "callback delay has same clock semantics");
    check(f.thread("sceKernelGetSystemTime", {Fixture::output}) == 0 &&
            f.runtime.memory().load32(Fixture::output) == 3100,
        "time output matches scheduler clock");
    check(f.thread("sceKernelGetSystemTimeWide") == 3100 && ctx.gpr[3] == 0, "wide system time returns register pair");
    check(f.thread("sceKernelGetSystemTimeLow") == 3100, "low system time matches wide time");
    check(f.thread("sceKernelSysClock2USecWide", {2000003, 0, Fixture::output, Fixture::output + 4}) == 0,
        "clock conversion succeeds");
    check(f.runtime.memory().load32(Fixture::output) == 2 && f.runtime.memory().load32(Fixture::output + 4) == 3,
        "clock conversion splits seconds and microseconds");
    check(k.describe_threads().find("module_start") != std::string::npos, "diagnostics identify live thread");
}

void semaphore_contracts() {
    Fixture f;
    for (auto args : {std::array<unsigned, 2>{0, 0}, {2, 1}, {0xFFFFFFFFu, 1}})
        check(f.thread("sceKernelCreateSema", {Fixture::text, 0, args[0], args[1]}) == mhp3rd::error::kIllegalCount,
            "invalid semaphore counts rejected");
    const auto uid = f.thread("sceKernelCreateSema", {Fixture::text, 0, 1, 3});
    check(static_cast<int>(uid) > 0, "semaphore creation succeeds");
    check(f.thread("sceKernelPollSema", {uid, 1}) == 0 && f.kernel.semaphores.at(uid).count == 0,
        "poll consumes available count");
    check(f.thread("sceKernelPollSema", {uid, 1}) == mhp3rd::error::kSemaZero, "empty poll does not wait");
    check(f.thread("sceKernelPollSema", {uid, 0}) == mhp3rd::error::kIllegalCount, "zero poll count rejected");
    check(f.thread("sceKernelSignalSema", {uid, 4}) == mhp3rd::error::kSemaOverflow, "overflow leaves count unchanged");
    check(f.kernel.semaphores.at(uid).count == 0, "overflow preserves state");
    check(f.thread("sceKernelSignalSema", {uid, 2}) == 0 && f.thread("sceKernelWaitSema", {uid, 1, 0}) == 0,
        "immediate wait consumes signal");
    check(f.thread("sceKernelWaitSema", {uid, 0, 0}) == mhp3rd::error::kIllegalCount &&
            f.thread("sceKernelWaitSema", {uid, 4, 0}) == mhp3rd::error::kIllegalCount,
        "wait validates requested count");
    f.runtime.memory().store32(Fixture::output, 250);
    const auto start = f.kernel.now_us();
    check(f.thread("sceKernelWaitSema", {uid, 2, Fixture::output}) == mhp3rd::error::kWaitTimeout,
        "semaphore wait times out deterministically");
    check(f.kernel.now_us() == start + 250 && f.runtime.memory().load32(Fixture::output) == 0 &&
            f.kernel.semaphores.at(uid).waiters.empty(),
        "timeout zeros remaining time and removes waiter");
    const auto loader = f.kernel.current_uid();
    const auto helper = f.helper();
    f.thread("sceKernelWaitSema", {uid, 2, 0});
    check(f.kernel.current_uid() == static_cast<int>(helper), "blocked semaphore switches to helper");
    check(f.thread("sceKernelSignalSema", {uid, 1}) == 0 && f.kernel.current_uid() == loader,
        "signal wakes and preempts to higher priority waiter");
    check(f.kernel.semaphores.at(uid).count == 0, "wake consumes full requested count");
    f.thread("sceKernelWaitSema", {uid, 1, 0});
    check(f.thread("sceKernelDeleteSema", {uid}) == mhp3rd::error::kWaitDelete && f.kernel.current_uid() == loader,
        "delete wakes waiter with deletion result");
    for (const auto *name : {"sceKernelDeleteSema", "sceKernelSignalSema", "sceKernelWaitSema", "sceKernelPollSema"})
        check(f.thread(name, {uid, 1, 0}) == mhp3rd::error::kUnknownSemid, "deleted semaphore returns unknown UID");
}

void event_flag_contracts() {
    Fixture f;
    const auto uid = f.thread("sceKernelCreateEventFlag", {Fixture::text, 0, 0x3});
    check(f.thread("sceKernelPollEventFlag", {uid, 3, 0, Fixture::output}) == 0 &&
            f.runtime.memory().load32(Fixture::output) == 3,
        "AND match returns full pattern");
    check(f.thread("sceKernelPollEventFlag", {uid, 6, 0, Fixture::output}) == mhp3rd::error::kEvfCond &&
            f.runtime.memory().load32(Fixture::output) == 3,
        "failed poll returns unchanged pattern");
    check(f.thread("sceKernelPollEventFlag", {uid, 6, 1, 0}) == 0, "OR mode succeeds on any requested bit");
    for (auto mode : {2u, 0x30u})
        check(f.thread("sceKernelPollEventFlag", {uid, 1, mode, 0}) == mhp3rd::error::kIllegalMode,
            "invalid clear/match mode rejected");
    check(f.thread("sceKernelPollEventFlag", {uid, 0, 0, 0}) == mhp3rd::error::kEvfIllegalPattern,
        "zero pattern rejected");
    check(f.thread("sceKernelPollEventFlag", {uid, 1, 0x20, 0}) == 0 && f.kernel.event_flags.at(uid).pattern == 2,
        "clear requested bits preserves unrelated bits");
    check(f.thread("sceKernelPollEventFlag", {uid, 2, 0x10, 0}) == 0 && f.kernel.event_flags.at(uid).pattern == 0,
        "clear all erases full matched pattern");
    check(f.thread("sceKernelSetEventFlag", {uid, 7}) == 0 && f.thread("sceKernelClearEventFlag", {uid, 6}) == 0 &&
            f.kernel.event_flags.at(uid).pattern == 6,
        "set ORs and clear ANDs pattern");
    check(f.thread("sceKernelWaitEventFlag", {uid, 2, 0, 0, 0}) == 0, "matching wait completes immediately");
    f.runtime.memory().store32(Fixture::output, 50);
    check(f.thread("sceKernelWaitEventFlag", {uid, 8, 0, 0, Fixture::output}) == mhp3rd::error::kWaitTimeout &&
            f.kernel.event_flags.at(uid).waiters.empty(),
        "event timeout removes queue entry");
    const auto loader = f.kernel.current_uid();
    f.helper();
    f.thread("sceKernelWaitEventFlag", {uid, 8, 0x20, Fixture::output, 0});
    check(f.thread("sceKernelWaitEventFlag", {uid, 8, 0, 0, 0}) == mhp3rd::error::kEvfMulti,
        "single-waiter flag rejects second blocking waiter");
    f.thread("sceKernelSetEventFlag", {uid, 8});
    check(f.kernel.current_uid() == loader && f.ctx.gpr[2] == 0 && f.runtime.memory().load32(Fixture::output) == 14 &&
            f.kernel.event_flags.at(uid).pattern == 6,
        "signal delivers output then clears matching bits");
    f.thread("sceKernelWaitEventFlag", {uid, 8, 0, 0, 0});
    f.thread("sceKernelDeleteEventFlag", {uid});
    check(f.kernel.current_uid() == loader && f.ctx.gpr[2] == mhp3rd::error::kWaitDelete,
        "event deletion cancels waiter");
    for (const auto *name :
        {"sceKernelDeleteEventFlag", "sceKernelSetEventFlag", "sceKernelClearEventFlag", "sceKernelPollEventFlag"})
        check(f.thread(name, {uid, 1}) == mhp3rd::error::kUnknownEvfid, "deleted event flag rejects operations");
}

void mutex_contracts() {
    Fixture f;
    const auto uid = f.thread("sceKernelCreateMutex", {Fixture::text, 0, 0});
    check(
        f.thread("sceKernelLockMutex", {uid, 0, 0}) == mhp3rd::error::kIllegalCount, "mutex zero lock count rejected");
    check(f.thread("sceKernelLockMutex", {uid, 1, 0}) == 0, "unowned mutex locks");
    check(f.thread("sceKernelLockMutex", {uid, 1, 0}) == mhp3rd::error::kMutexRecursiveNotAllowed,
        "nonrecursive re-lock rejected");
    check(f.thread("sceKernelUnlockMutex", {uid, 0}) == mhp3rd::error::kIllegalCount, "zero unlock count rejected");
    check(f.thread("sceKernelUnlockMutex", {uid, 2}) == mhp3rd::error::kMutexUnlockUnderflow,
        "unlock underflow preserves ownership");
    check(f.thread("sceKernelUnlockMutex", {uid, 1}) == 0 &&
            f.thread("sceKernelUnlockMutex", {uid, 1}) == mhp3rd::error::kMutexUnlocked,
        "balanced unlock clears ownership");
    const auto recursive = f.thread("sceKernelCreateMutex", {Fixture::text, 0x200, 2});
    check(f.thread("sceKernelLockMutex", {recursive, 3, 0}) == 0 && f.kernel.mutexes.at(recursive).lock_count == 5,
        "recursive mutex adds count");
    check(f.thread("sceKernelUnlockMutex", {recursive, 5}) == 0, "recursive mutex releases all references");
    const auto loader = f.kernel.current_uid();
    const auto helper = f.helper();
    f.thread("sceKernelDelayThread", {100});
    check(f.kernel.current_uid() == static_cast<int>(helper), "loader delay yields to helper");
    check(f.thread("sceKernelLockMutex", {uid, 1, 0}) == 0, "helper owns mutex");
    f.kernel.on_starvation(f.ctx);
    check(f.kernel.current_uid() == loader, "elapsed loader deadline preempts helper");
    f.runtime.memory().store32(Fixture::output, 100);
    f.thread("sceKernelLockMutex", {uid, 1, Fixture::output});
    check(f.kernel.current_uid() == static_cast<int>(helper), "contended lock yields to owner");
    f.thread("sceKernelUnlockMutex", {uid, 1});
    check(f.kernel.current_uid() == loader && f.kernel.mutexes.at(uid).owner == loader &&
            f.runtime.memory().load32(Fixture::output) == 100,
        "unlock transfers ownership and retains remaining timeout");
    f.thread("sceKernelUnlockMutex", {uid, 1});
    check(f.thread("sceKernelDeleteMutex", {uid}) == 0 && f.thread("sceKernelDeleteMutex", {recursive}) == 0,
        "mutex deletion succeeds");
    for (const auto *name : {"sceKernelLockMutex", "sceKernelUnlockMutex", "sceKernelDeleteMutex"})
        check(f.thread(name, {uid, 1, 0}) == mhp3rd::error::kMutexNotFound, "deleted mutex rejects operations");
}
void interrupt_and_host_wait_contracts() {
    Fixture f;
    auto &k = f.kernel;
    auto &ctx = f.ctx;
    check(f.call("Kernel_Library", "sceKernelCpuSuspendIntr") == 1 && !k.interrupts_enabled(),
        "mask interrupts returns prior state");
    check(f.call("Kernel_Library", "sceKernelCpuSuspendIntr") == 0, "nested mask reports disabled");
    check(f.call("Kernel_Library", "sceKernelCpuResumeIntr", {1}) == 0 && k.interrupts_enabled(),
        "resume restores interrupts");
    check(f.call("Kernel_Library", "sceKernelMemset", {Fixture::output, 0x1ab, 8}) == Fixture::output &&
            f.runtime.memory().load32(Fixture::output) == 0xababababu,
        "memset truncates value and returns destination");
    const auto cb = f.thread("sceKernelCreateCallback", {Fixture::text, 0x08826000, 0x7654});
    k.notify_callback(-1, 1);
    k.notify_callback(cb, 7);
    k.notify_callback(cb, 9);
    check(k.deliver_callbacks() && !k.deliver_callbacks(), "notifications queue once and clear pending state");
    ctx.set_gpr(16, 0x12345678);
    ctx.set_gpr(31, 0x08827000);
    k.finish(ctx, 77);
    check(k.in_interrupt() && ctx.pc == 0x08826000 && ctx.gpr[4] == 2 && ctx.gpr[5] == 9 && ctx.gpr[6] == 0x7654,
        "callback receives count, latest notification and common data");
    check(ctx.gpr[29] == mhp3rd::kInterruptStackTop - 0x40 && ctx.gpr[31] == mhp3rd::kInterruptReturnStub,
        "interrupt uses reserved stack and trampoline");
    check(!k.deliver_callbacks(), "nested callback delivery suppressed");
    k.delay_current(ctx, 1);
    check(ctx.gpr[2] == mhp3rd::error::kCanNotWait, "interrupt handler cannot block");
    k.interrupt_return_stub(ctx);
    check(!k.in_interrupt() && ctx.gpr[16] == 0x12345678 && ctx.gpr[2] == 77 && ctx.pc == 0x08827000,
        "interrupt restores saved registers and import result");
    check(f.thread("sceKernelDeleteCallback", {cb}) == 0 &&
            f.thread("sceKernelDeleteCallback", {cb}) == mhp3rd::error::kUnknownCbid,
        "callback deletion validates identity");
    check(f.call("InterruptManager", "sceKernelEnableSubIntr", {30, 4}) == mhp3rd::error::kIllegalArgument,
        "missing interrupt cannot enable");
    check(f.call("InterruptManager", "sceKernelRegisterSubIntrHandler", {30, 4, 0x08828000, 123}) == 0 &&
            f.call("InterruptManager", "sceKernelEnableSubIntr", {30, 4}) == 0,
        "registered subinterrupt enables");
    unsigned vblanks = 0;
    k.add_vblank_hook([&] { ++vblanks; });
    k.on_vblank();
    k.finish(ctx, 0);
    check(k.in_interrupt() && ctx.pc == 0x08828000 && ctx.gpr[4] == 4 && ctx.gpr[5] == 123 && vblanks == 1,
        "vblank delivers guest handler and host hook");
    k.interrupt_return_stub(ctx);
    check(f.call("InterruptManager", "sceKernelReleaseSubIntrHandler", {30, 4}) == 0 &&
            f.call("InterruptManager", "sceKernelReleaseSubIntrHandler", {30, 4}) == mhp3rd::error::kIllegalArgument,
        "interrupt release rejects repeated delete");
    unsigned calls = 0;
    k.wait_host(ctx, std::nullopt, [&](bool timeout) -> std::optional<unsigned> {
        check(!timeout, "immediate host completion is not timeout");
        ++calls;
        return 17;
    });
    check(ctx.gpr[2] == 17 && calls == 1, "satisfied host wait completes without switching");
    calls = 0;
    k.wait_host(ctx, 0, [&](bool timeout) -> std::optional<unsigned> {
        ++calls;
        return timeout ? std::optional<unsigned>(19) : std::nullopt;
    });
    check(ctx.gpr[2] == 19 && calls == 2, "zero timeout performs final poll");
    const auto start = k.now_us();
    k.wait_host(ctx, 2000,
        [](bool timeout) -> std::optional<unsigned> { return timeout ? std::optional<unsigned>(23) : std::nullopt; });
    check(ctx.gpr[2] == 23 && k.now_us() == start + 2000, "host deadline advances clock and returns final poll value");
    k.wait_host(ctx, 100, [](bool) -> std::optional<unsigned> { return std::nullopt; });
    check(ctx.gpr[2] == mhp3rd::error::kWaitTimeout, "empty final poll falls back to kernel timeout");
    bool returned = false;
    ctx.set_gpr(31, 0x08829000);
    k.call_guest(ctx, 0x0882a000, {1, 2, 3, 4}, [&](auto &context, unsigned value) {
        returned = true;
        check(value == 33 && context.pc == 0x08829000 && context.gpr[31] == 0x08829000,
            "guest completion restores import return address");
    });
    check(ctx.pc == 0x0882a000 && ctx.gpr[4] == 1 && ctx.gpr[7] == 4 && ctx.gpr[31] == mhp3rd::kGuestCallReturnStub,
        "guest call receives argument registers and trampoline");
    ctx.set_gpr(2, 33);
    k.guest_call_return_stub(ctx);
    check(returned, "guest return invokes completion");
    k.guest_call_return_stub(ctx);
    check(f.runtime.stopped() && f.runtime.stop_reason().find("made none") != std::string::npos,
        "unexpected guest return stops with diagnostic");
}
void vtimer_contracts() {
    Fixture f;
    const auto uid = f.thread("sceKernelCreateVTimer", {Fixture::text});
    check(f.thread("sceKernelStartVTimer", {uid}) == 0 && f.thread("sceKernelStartVTimer", {uid}) == 1,
        "vtimer starts once");
    check(f.thread("sceKernelStartVTimer", {0xffffffffu}) == mhp3rd::error::kUnknownVtid &&
            f.thread("sceKernelSetVTimerHandlerWide", {0xffffffffu}) == mhp3rd::error::kUnknownVtid,
        "timer operations validate UID");
    check(f.thread("sceKernelSetVTimerHandlerWide", {uid, 0, 500, 0, 0x0882b000, 0x5432}) == 0,
        "timer sets schedule and common pointer");
    f.kernel.on_starvation(f.ctx);
    check(f.kernel.in_interrupt() && f.ctx.pc == 0x0882b000 && f.ctx.gpr[4] == uid && f.ctx.gpr[7] == 0x5432,
        "elapsed timer dispatches handler");
    check(f.runtime.memory().load32(f.ctx.gpr[5]) == 500 && f.runtime.memory().load32(f.ctx.gpr[6]) == 1000,
        "timer passes scheduled and current clocks");
    f.ctx.set_gpr(2, 2000);
    f.kernel.interrupt_return_stub(f.ctx);
    check(f.kernel.vtimers.at(uid).schedule_us == 2500, "timer rearms relative to original schedule");
    f.kernel.on_starvation(f.ctx);
    check(!f.kernel.in_interrupt(), "timer does not fire early");
    f.kernel.on_starvation(f.ctx);
    check(f.kernel.in_interrupt(), "rearmed timer fires at next schedule");
    f.ctx.set_gpr(2, 0);
    f.kernel.interrupt_return_stub(f.ctx);
    check(f.kernel.vtimers.at(uid).handler == 0, "zero handler result disarms timer");
    mhp3rd::VTimer stopped{};
    stopped.accumulated_us = 17;
    check(f.kernel.vtimer_value(stopped) == 17, "stopped timer retains accumulated clock");
}
void sysmem_contracts() {
    Fixture f;
    auto call = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("SysMemUserForUser", name, args);
    };
    const auto uid = call("sceKernelAllocPartitionMemory", {2, Fixture::text, 0, 512, 0});
    check(static_cast<int>(uid) > 0, "partition allocation returns UID");
    const auto address = call("sceKernelGetBlockHeadAddr", {uid});
    check(
        address != 0 && call("sceKernelGetBlockHeadAddr", {0xffffffffu}) == 0, "head address resolves valid UID only");
    check(call("sceKernelGetMemoryBlockAddr", {uid, Fixture::output}) == 0 &&
            f.runtime.memory().load32(Fixture::output) == address,
        "block address writes output");
    check(call("sceKernelGetMemoryBlockAddr", {uid, 0}) == 0 &&
            call("sceKernelGetMemoryBlockAddr", {0xffffffffu, Fixture::output}) == mhp3rd::error::kIllegalMemblock,
        "block lookup handles optional pointer and bad UID");
    check(call("sceKernelFreePartitionMemory", {uid}) == 0, "partition free succeeds");
    check(call("sceKernelAllocMemoryBlock", {Fixture::text, 2, 256}) == mhp3rd::error::kIllegalMemblockType,
        "new allocation API rejects fixed type");
    const auto newer = call("sceKernelAllocMemoryBlock", {Fixture::text, 1, 256});
    check(static_cast<int>(newer) > 0 && call("sceKernelFreeMemoryBlock", {newer}) == 0,
        "new allocation API allocates and frees");
    for (const auto *name : {"sceKernelSetCompilerVersion", "sceKernelSetCompiledSdkVersion603_605"})
        check(call(name) == 0, "compiler metadata acknowledged");
    check(f.call("sceSuspendForUser", "sceKernelVolatileMemLock", {0, Fixture::output, Fixture::output + 4}) == 0,
        "volatile lock succeeds");
    check(f.runtime.memory().load32(Fixture::output) == mhp3rd::kVolatileMemoryBase &&
            f.runtime.memory().load32(Fixture::output + 4) == mhp3rd::kVolatileMemorySize,
        "volatile lock returns reserved region");
    check(f.call("sceSuspendForUser", "sceKernelVolatileMemLock") == 0 &&
            f.call("sceSuspendForUser", "sceKernelVolatileMemUnlock") == 0 &&
            f.call("sceSuspendForUser", "sceKernelPowerTick") == 0,
        "optional lock pointers and power operations succeed");
    f.string(Fixture::output, "abcdefghij");
    check(f.call("sceDmac", "sceDmacMemcpy", {Fixture::output + 2, Fixture::output, 8}) == 0 &&
            mhp3rd::read_cstring(f.runtime.memory(), Fixture::output, 10) == "ababcdefgh",
        "backward overlapping copy preserves bytes");
    check(f.call("sceDmac", "sceDmacMemcpy", {Fixture::output, Fixture::output + 2, 8}) == 0 &&
            mhp3rd::read_cstring(f.runtime.memory(), Fixture::output, 8) == "abcdefgh",
        "forward overlapping copy preserves bytes");
    check(f.call("sceDmac", "sceDmacMemcpy", {Fixture::output, Fixture::output, 0}) == 0, "empty DMA copy succeeds");
    f.string(Fixture::text, "%d %i %u %x %X %p %c %% %q %.");
    check(call("sceKernelPrintf", {Fixture::text, 0xffffffffu, 2, 3, 4, 5, 6, 'z'}) == 0,
        "guest printf safely consumes bounded register arguments");
    f.string(Fixture::text, "%s");
    check(call("sceKernelPrintf", {Fixture::text, Fixture::output}) == 0, "guest printf reads string argument");
}
class PublicFiles {
public:
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("yakumo-kernel-contract-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    PublicFiles() { std::filesystem::create_directories(root / "ms"); }
    ~PublicFiles() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    std::filesystem::path iso() {
        std::vector<std::uint8_t> bytes(24u * 2048u);
        auto le32 = [&](std::size_t offset, std::uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
        };
        bytes[16u * 2048u] = 1;
        const std::string signature = "CD001";
        std::copy(signature.begin(), signature.end(), bytes.begin() + 16u * 2048u + 1);
        le32(16u * 2048u + 158, 20);
        le32(16u * 2048u + 166, 2048);
        const std::string filename = "PUBLIC.TXT;1";
        const auto record = 20u * 2048u;
        bytes[record] = static_cast<std::uint8_t>(33 + filename.size());
        le32(record + 2, 21);
        le32(record + 10, 8);
        bytes[record + 32] = static_cast<std::uint8_t>(filename.size());
        std::copy(filename.begin(), filename.end(), bytes.begin() + record + 33);
        const std::string payload = "fixture!";
        std::copy(payload.begin(), payload.end(), bytes.begin() + 21u * 2048u);
        const auto path = root / "public.iso";
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }
};
void io_contracts() {
    Fixture f;
    PublicFiles files;
    mhp3rd::HleRegistrar hle(f.runtime);
    mhp3rd::register_io(hle, {}, files.root / "ms");
    auto call = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("IoFileMgrForUser", name, args);
    };
    auto path_call = [&](const char *name, std::string_view path, unsigned second = 0) {
        f.string(Fixture::text, path);
        return call(name, {Fixture::text, second});
    };
    constexpr unsigned badfd = 0x80010009u, missing = 0x80010002u, device = 0x80010013u, invalid = 0x80010016u,
                       readonly = 0x8001001eu;
    check(path_call("sceIoOpen", "unknown:/missing", 1) == device &&
            path_call("sceIoOpen", "disc0:/public.txt", 1) == device,
        "unsupported device and absent disc rejected");
    check(f.call("sceUmdUser", "sceUmdGetDriveStat") == 1, "absent disc reports no media");
    check(path_call("sceIoOpen", "ms0:/missing", 1) == missing, "missing memory-stick file rejected");
    const auto fd = path_call("sceIoOpen", "MS0:\\folder\\..\\folder\\sample.txt", 0x602);
    check(static_cast<int>(fd) > 0, "create normalizes path and creates parent directories");
    f.string(Fixture::output, "synthetic data");
    check(call("sceIoWrite", {fd, Fixture::output, 14}) == 14, "host write returns exact length");
    check(call("sceIoLseek", {fd, 0, 0, 0, 0}) == 0 && f.ctx.gpr[3] == 0, "seek to start returns 64-bit offset");
    check(call("sceIoRead", {fd, Fixture::output, 50}) == 14 &&
            mhp3rd::read_cstring(f.runtime.memory(), Fixture::output) == "synthetic data",
        "read clamps at EOF and preserves bytes");
    check(call("sceIoRead", {fd, Fixture::output, 1}) == 0, "EOF read returns zero");
    std::array<std::uint8_t, 5> prefix{};
    check(mhp3rd::read_open_file(fd, 0, prefix.data(), prefix.size()) == 5 &&
            std::string(prefix.begin(), prefix.end()) == "synth",
        "position-independent read returns requested prefix");
    check(call("sceIoLseek", {fd, 0, 0, 0, 1}) == 14, "position-independent read does not move guest offset");
    check(call("sceIoLseek", {fd, 0, 0xfffffffbu, 0xffffffffu, 2}) == 9, "negative offset relative to EOF succeeds");
    check(call("sceIoLseek", {fd, 0, 1, 0, 1}) == 10, "seek relative to current offset succeeds");
    check(call("sceIoLseek", {fd, 0, 0xffffffffu, 0xffffffffu, 0}) == invalid && f.ctx.gpr[3] == 0xffffffffu,
        "negative absolute seek returns signed wide error");
    check(call("sceIoLseek", {fd, 0, 0, 0, 9}) == invalid, "invalid seek base rejected");
    check(path_call("sceIoGetstat", "fatms0:/folder/sample.txt", Fixture::output) == 0,
        "stat accepts memory-stick alias");
    check(f.runtime.memory().load32(Fixture::output) == 0x21ff &&
            f.runtime.memory().load32(Fixture::output + 8) == 14 &&
            f.runtime.memory().load32(Fixture::output + 12) == 0,
        "stat reports file type and 64-bit size");
    check(path_call("sceIoGetstat", "ms0:/folder", Fixture::output) == 0 &&
            f.runtime.memory().load32(Fixture::output) == 0x11ff,
        "directory stat reports directory mode");
    const auto directory = path_call("sceIoDopen", "ms0:/folder");
    check(static_cast<int>(directory) > 0 && call("sceIoRead", {directory, Fixture::output, 1}) == badfd &&
            call("sceIoWrite", {directory, Fixture::output, 1}) == badfd,
        "directory descriptors reject byte I/O");
    check(mhp3rd::read_open_file(directory, 0, prefix.data(), 5) == 0, "position-independent read rejects directories");
    check(call("sceIoDclose", {directory}) == 0 && call("sceIoDclose", {directory}) == badfd,
        "directory close validates descriptor");
    check(path_call("sceIoDopen", "ms0:/missing") == missing &&
            path_call("sceIoGetstat", "ms0:/missing", Fixture::output) == missing,
        "missing directory/stat errors");
    check(call("sceIoClose", {fd}) == 0 && call("sceIoClose", {fd}) == badfd, "double close rejected");
    check(mhp3rd::read_open_file(fd, 0, prefix.data(), 5) == 0, "closed file cannot be read by module loader");
    for (const auto *name : {"sceIoRead", "sceIoWrite", "sceIoLseek"})
        check(call(name, {fd, Fixture::output, 1}) == badfd, "closed descriptor operations rejected");
    f.string(Fixture::text, "ms0:/folder/sample.txt");
    f.string(Fixture::output, "ms0:/folder/renamed.txt");
    check(call("sceIoRename", {Fixture::text, Fixture::output}) == 0 &&
            std::filesystem::exists(files.root / "ms/folder/renamed.txt"),
        "rename changes host file");
    check(call("sceIoRename", {Fixture::text, Fixture::output}) == missing, "rename missing source fails");
    f.string(Fixture::text, "disc0:/public.txt");
    check(call("sceIoRename", {Fixture::text, Fixture::output}) == readonly, "cross-device rename rejected");
    mhp3rd::register_io(hle, files.iso(), files.root / "ms");
    check(f.call("sceUmdUser", "sceUmdGetDriveStat") == 0x32 && f.call("sceUmdUser", "sceUmdActivate") == 0 &&
            f.call("sceUmdUser", "sceUmdGetErrorStat") == 0,
        "synthetic disc reports ready/readable");
    check(path_call("sceIoOpen", "disc0:/public.txt", 2) == readonly &&
            path_call("sceIoOpen", "disc0:/missing", 1) == missing,
        "disc rejects writes and missing files");
    const auto disc = path_call("sceIoOpen", "umd0:/public.txt", 1);
    check(static_cast<int>(disc) > 0 && call("sceIoRead", {disc, Fixture::output, 50}) == 8 &&
            mhp3rd::read_cstring(f.runtime.memory(), Fixture::output, 8) == "fixture!",
        "disc reads synthetic payload and clamps requested length");
    check(call("sceIoRead", {disc, Fixture::output, 1}) == 0 && mhp3rd::read_open_file(disc, 8, prefix.data(), 5) == 0,
        "disc EOF applies to both read interfaces");
    check(
        mhp3rd::read_open_file(disc, 2, prefix.data(), 5) == 5 && std::string(prefix.begin(), prefix.end()) == "xture",
        "disc independent read addresses original extent");
    check(path_call("sceIoGetstat", "isofs0:/PUBLIC.TXT", Fixture::output) == 0 &&
            f.runtime.memory().load32(Fixture::output + 8) == 8 &&
            f.runtime.memory().load32(Fixture::output + 64) == 21,
        "disc stat returns extent and length");
    const auto raw = path_call("sceIoOpen", "disc0:/sce_lbn0x15_size0x8", 1);
    check(static_cast<int>(raw) > 0 && call("sceIoRead", {raw, Fixture::output, 8}) == 8,
        "raw hexadecimal sector path reads synthetic payload");
    const auto rawdecimal = path_call("sceIoOpen", "disc0:/sce_lbn21_size8", 1);
    check(static_cast<int>(rawdecimal) > 0 && call("sceIoRead", {rawdecimal, Fixture::output, 8}) == 8,
        "decimal sector path accepts complete numbers");
    check(path_call("sceIoOpen", "disc0:/sce_lbnxx_size8", 1) == missing &&
            path_call("sceIoOpen", "disc0:/sce_lbn21_size8x", 1) == missing,
        "malformed sector numbers fail cleanly");
    const auto rawdevice = path_call("sceIoOpen", "disc0:", 1);
    check(static_cast<int>(rawdevice) > 0 && call("sceIoLseek", {rawdevice, 0, 0, 0, 2}) == 24 * 2048,
        "raw device exposes whole image length");
    const auto root = path_call("sceIoDopen", "disc0:");
    check(static_cast<int>(root) > 0, "disc root opens as directory");
    for (auto open : {disc, raw, rawdecimal, rawdevice, root})
        check(call("sceIoClose", {open}) == 0, "all synthetic descriptors close");
    f.string(Fixture::text, "ms0:");
    for (const auto command : {0x02025801u, 0x02025806u, 0x02425823u}) {
        f.runtime.memory().store32(Fixture::output, 99);
        check(call("sceIoDevctl", {Fixture::text, command, 0, 0, Fixture::output, 4}) == 0 &&
                f.runtime.memory().load32(Fixture::output) == (command == 0x02025801u ? 4u : 1u),
            "memory-stick devctl reports inserted state");
        f.runtime.memory().store32(Fixture::output, 99);
        call("sceIoDevctl", {Fixture::text, command, 0, 0, Fixture::output, 3});
        check(f.runtime.memory().load32(Fixture::output) == 99, "short devctl output remains untouched");
    }
    f.runtime.memory().store32(Fixture::output, Fixture::output + 16);
    check(call("sceIoDevctl", {Fixture::text, 0x02425818u, Fixture::output}) == 0 &&
            f.runtime.memory().load32(Fixture::output + 28) == 512 &&
            f.runtime.memory().load32(Fixture::output + 32) == 32,
        "free-space geometry written through nested pointer");
    const auto cb = f.thread("sceKernelCreateCallback", {0, 0x08826000, 0});
    f.runtime.memory().store32(Fixture::output, cb);
    check(call("sceIoDevctl", {Fixture::text, 0x02015804u, Fixture::output}) == 0 &&
            f.kernel.callbacks.at(cb).notify_argument == 1,
        "memory-stick insertion callback notified immediately");
    for (const auto command : {0x02415821u, 0x02015805u, 0x02415822u, 0x02425818u, 123u})
        check(call("sceIoDevctl", {Fixture::text, command}) == 0,
            "optional devctl pointers and unsupported command acknowledged");
    f.string(Fixture::output, "log");
    check(call("sceIoWrite", {1, Fixture::output, 3}) == 3 && call("sceIoWrite", {2, Fixture::output, 0}) == 0,
        "stdio handles guest output and empty writes");
}
bool wait_until(const std::function<bool()> &condition) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
    return condition();
}
struct ProtocolPeer {
    mhp3rd::adhoc::net::Socket socket = mhp3rd::adhoc::net::kNoSocket;
    explicit ProtocolPeer(std::uint16_t port) {
        using namespace mhp3rd::adhoc::net;
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const auto address = Address::ipv4_address(htonl(INADDR_LOOPBACK), port);
        check(socket != kNoSocket &&
                ::connect(socket, reinterpret_cast<const sockaddr *>(&address.storage), address.length) == 0,
            "loopback protocol peer connects");
        set_nonblocking(socket);
        no_sigpipe(socket);
    }
    ~ProtocolPeer() { close(); }
    void close() {
        if (socket != mhp3rd::adhoc::net::kNoSocket) mhp3rd::adhoc::net::close_socket(socket);
        socket = mhp3rd::adhoc::net::kNoSocket;
    }
    void send(const std::string &bytes) {
        check(::send(socket, bytes.data(), static_cast<int>(bytes.size()), mhp3rd::adhoc::net::kSendFlags) ==
                static_cast<int>(bytes.size()),
            "public protocol record sent completely");
    }
    bool closed() {
        return wait_until([&] {
            char buffer[256];
            const auto got = ::recv(socket, buffer, sizeof(buffer), 0);
            return got == 0 || (got < 0 && !mhp3rd::adhoc::net::would_block(mhp3rd::adhoc::net::socket_error()));
        });
    }
    std::string read(std::size_t size) {
        std::string bytes;
        check(wait_until([&] {
            char buffer[256];
            const auto got = ::recv(socket, buffer, static_cast<int>(std::min(sizeof(buffer), size - bytes.size())), 0);
            if (got > 0) bytes.append(buffer, static_cast<std::size_t>(got));
            return bytes.size() == size;
        }),
            "public protocol response arrives before bound");
        return bytes;
    }
};
void network_contracts() {
    using namespace mhp3rd::adhoc;
    Fixture f;
    Server server;
    struct Cleanup {
        ~Cleanup() { Client::get().stop(); }
    } cleanup;
    auto &settings = mhp3rd::settings::current();
    settings.adhoc = false;
    settings.adhoc_mac = "02:11:22:33:44:0a";
    settings.adhoc_nickname = "PublicA";
    const Mac macA{2, 0x11, 0x22, 0x33, 0x44, 0x0a}, macB{2, 0x11, 0x22, 0x33, 0x44, 0x0b};
    constexpr unsigned own = Fixture::output, peer = own + 8, length = own + 16, data = own + 32, port_out = own + 24;
    for (unsigned i = 0; i < 6; ++i) {
        f.runtime.memory().store8(own + i, macA[i]);
        f.runtime.memory().store8(peer + i, macB[i]);
    }
    mhp3rd::HleRegistrar hle(f.runtime);
    mhp3rd::register_adhoc(hle);
    auto net = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("sceNetAdhoc", name, args);
    };
    auto ctl = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("sceNetAdhocctl", name, args);
    };
    constexpr unsigned invalidid = 0x80410701u, addr = 0x80410702u, buflen = 0x80410704u, datalen = 0x80410705u,
                       wouldblock = 0x80410709u, portuse = 0x8041070au, invalid = 0x80410711u, timeout = 0x80410715u;
    check(net("sceNetAdhocPdpCreate", {own, 10000, 4096}) == 0x80410712u && net("sceNetAdhocPtpOpen") == 0x80410712u &&
            net("sceNetAdhocPtpListen") == 0x80410712u,
        "uninitialized ad hoc rejects socket creation");
    for (const auto *name :
        {"sceNetAdhocctlScan", "sceNetAdhocctlDisconnect", "sceNetAdhocctlGetPeerList", "sceNetAdhocctlGetScanInfo"})
        check(ctl(name) == 0x80410b08u, "uninitialized control API rejected");
    check(f.call("sceNet", "sceNetInit") == 0 && f.call("sceNet", "sceNetFreeThreadinfo") == 0 &&
            f.call("sceNet", "sceNetTerm") == 0,
        "base network initialization acknowledged");
    check(f.call("sceNet", "sceNetGetLocalEtherAddr", {data}) == 0 && f.runtime.memory().load8(data + 5) == macA[5],
        "configured local MAC exported to guest");
    check(
        f.call("sceWlanDrv", "sceWlanGetEtherAddr", {data}) == 0 && f.call("sceWlanDrv", "sceWlanGetSwitchState") == 0,
        "disabled WLAN state reflects settings");
    check(
        net("sceNetAdhocInit") == 0 && net("sceNetAdhocInit") == 0x80410713u, "ad hoc initialization rejects repeats");
    check(net("sceNetAdhocPdpCreate", {0, 10000, 4096}) == addr &&
            net("sceNetAdhocPdpCreate", {own, 10000, 0}) == buflen &&
            net("sceNetAdhocPdpCreate", {peer, 10000, 4096}) == addr,
        "PDP validates address ownership and buffer");
    const auto pdp = net("sceNetAdhocPdpCreate", {own, 10000, 4096});
    check(static_cast<int>(pdp) > 0 && net("sceNetAdhocPdpCreate", {own, 10000, 4096}) == portuse,
        "PDP reserves unique source port");
    check(net("sceNetAdhocPdpSend", {pdp, 0, 10000, data, 1}) == addr &&
            net("sceNetAdhocPdpSend", {pdp, peer, 10000, 0, 1}) == datalen &&
            net("sceNetAdhocPdpSend", {pdp, peer, 10000, data, 10241}) == datalen,
        "PDP send rejects missing address and oversized/missing payload");
    check(net("sceNetAdhocPdpRecv", {pdp, 0, 0, 0, length, 100, 1}) == invalid, "PDP recv requires data pointer");
    f.runtime.memory().store32(length, 64);
    check(net("sceNetAdhocPdpRecv", {pdp, own, port_out, data, length, 100, 1}) == wouldblock,
        "nonblocking empty PDP returns would-block");
    check(net("sceNetAdhocPdpRecv", {pdp, own, port_out, data, length, 100, 0}) == timeout,
        "blocking PDP timeout returns network error");
    const auto loader = f.kernel.current_uid();
    const auto cancellation_helper = f.helper();
    net("sceNetAdhocPdpRecv", {pdp, 0, 0, data, length, 0, 0});
    net("sceNetAdhocPdpDelete", {pdp});
    f.kernel.on_starvation(f.ctx);
    check(f.kernel.current_uid() == loader && f.ctx.gpr[2] == invalidid,
        "deleting PDP cancels blocked reader with socket error");
    check(f.kernel.terminate_thread(f.ctx, static_cast<int>(cancellation_helper), true) == 0,
        "cancellation helper exits before timed socket checks");
    for (const auto *name : {"sceNetAdhocPdpDelete", "sceNetAdhocPdpSend", "sceNetAdhocPdpRecv", "sceNetAdhocPtpAccept",
             "sceNetAdhocPtpConnect", "sceNetAdhocPtpSend", "sceNetAdhocPtpRecv", "sceNetAdhocPtpFlush",
             "sceNetAdhocPtpClose"})
        check(net(name, {0xffffffffu}) == invalidid, "unknown socket API rejected");
    check(ctl("sceNetAdhocctlAddHandler") == 0x80410b04u, "control handler requires nonzero function");
    std::array<unsigned, 4> handlers{};
    for (auto &handler : handlers) handler = ctl("sceNetAdhocctlAddHandler", {0x08826000, 17});
    check(ctl("sceNetAdhocctlAddHandler", {0x08826000}) == 0x80410b12u, "control handler capacity bounded");
    for (auto handler : handlers) check(ctl("sceNetAdhocctlDelHandler", {handler}) == 0, "control handler removed");
    check(ctl("sceNetAdhocctlDelHandler", {handlers[0]}) == 0x80410b06u, "repeated control handler delete rejected");
    std::uint16_t server_port = 0;
    for (std::uint16_t candidate = 37512; candidate < 37600; candidate += 2)
        if (server.start(ServerConfig{candidate, false})) {
            server_port = candidate;
            break;
        }
    check(server_port != 0, "public loopback relay starts");
    {
        ProtocolPeer unknown(relay_port_for(server_port));
        auto record = relay::init(relay::kInitPdp, macB, 19000, {}, 0);
        record[0] = static_cast<char>(0xff);
        unknown.send(record);
        check(unknown.closed(), "unknown relay initialization closes the connection");
        ProtocolPeer oversized(relay_port_for(server_port));
        oversized.send(relay::init(relay::kInitPdp, macB, 19001, {}, 0) +
            relay::pdp_header(macA, 10000, static_cast<unsigned>(relay::kPdpBlockMax * 2 + 1)));
        check(oversized.closed(), "oversized datagram header is rejected without waiting for its body");
        ProtocolPeer stray(relay_port_for(server_port));
        stray.send(relay::init(relay::kInitPtpAccept, macB, 19002, macA, 19003));
        check(stray.closed(), "accept without a matching pending connection is rejected");
    }
    settings.adhoc = true;
    settings.adhoc_server = "127.0.0.1:" + std::to_string(server_port);
    f.string(Fixture::text + 4, "ULJM05800");
    check(ctl("sceNetAdhocctlInit", {0, 0, Fixture::text}) == 0 && ctl("sceNetAdhocctlInit") == 0x80410b07u,
        "control initializes one client identity");
    check(wait_until([] { return Client::get().server_state() == ServerState::Online; }),
        "real HLE client logs into synthetic relay");
    check(ctl("sceNetAdhocctlGetPeerList") == 0x80410b04u && ctl("sceNetAdhocctlGetScanInfo") == 0x80410b04u,
        "list APIs require length pointer");
    Client::get().join("PUBLIC01");
    check(wait_until([] { return Client::get().in_group(); }), "real HLE client joins group");
    ProtocolPeer remote(server_port);
    remote.send(ctl::login(macB, "PublicB", "ULJM05800") + ctl::connect("PUBLIC01"));
    check(wait_until([] { return Client::get().peers().size() == 1; }), "HLE client observes public protocol peer");
    check(ctl("sceNetAdhocctlGetPeerList", {length, 0}) == 0 && f.runtime.memory().load32(length) == 152,
        "peer sizing query counts records");
    check(ctl("sceNetAdhocctlGetPeerList", {length, data}) == 0 && f.runtime.memory().load32(data) == 0 &&
            mhp3rd::read_cstring(f.runtime.memory(), data + 4) == "PublicB" &&
            f.runtime.memory().load8(data + 137) == macB[5],
        "peer record terminates list and exports nickname/MAC");
    check(ctl("sceNetAdhocctlScan") == 0 && wait_until([] { return !Client::get().scan_results().empty(); }),
        "control scan completes against real relay");
    check(ctl("sceNetAdhocctlGetScanInfo", {length, 0}) == 0 && f.runtime.memory().load32(length) == 28,
        "scan sizing query counts record");
    check(ctl("sceNetAdhocctlGetScanInfo", {length, data}) == 0 && f.runtime.memory().load32(data + 4) == 1 &&
            mhp3rd::read_cstring(f.runtime.memory(), data + 8, 8) == "PUBLIC01",
        "scan record exports channel and group");
    const auto livepdp = net("sceNetAdhocPdpCreate", {own, 10000, 4096});
    ProtocolPeer datagrams(relay_port_for(server_port));
    datagrams.send(relay::init(relay::kInitPdp, macB, 10000, {}, 0));
    check(wait_until([] { return Client::get().diagnostics().relay_links_up >= 1; }), "PDP relay is established");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    f.string(data, "hello");
    check(net("sceNetAdhocPdpSend", {livepdp, peer, 10000, data, 5}) == 0, "HLE sends public datagram");
    const auto packet = datagrams.read(relay::kPdpHeaderSize + 5);
    check(packet.substr(relay::kPdpHeaderSize) == "hello", "wire receives exact HLE datagram payload");
    datagrams.send(relay::pdp_header(macA, 10000, 5) + "reply");
    check(wait_until([&] { return Client::get().pdp_peek(static_cast<int>(livepdp)).has_value(); }),
        "incoming datagram reaches client queue");
    f.runtime.memory().store32(length, 2);
    check(net("sceNetAdhocPdpRecv", {livepdp, peer, port_out, data, length, 100, 1}) == 0x80400706u &&
            f.runtime.memory().load32(length) == 5,
        "short PDP buffer reports required size without consuming datagram");
    check(net("sceNetAdhocPdpRecv", {livepdp, peer, port_out, data, length, 100, 1}) == 0 &&
            mhp3rd::read_cstring(f.runtime.memory(), data, 5) == "reply" &&
            f.runtime.memory().load16(port_out) == 10000,
        "retry receives preserved datagram and sender port");
    check(net("sceNetAdhocPtpListen") == addr && net("sceNetAdhocPtpListen", {own, 20000, 0}) == buflen,
        "PTP listener validates source and buffer");
    const auto listener = net("sceNetAdhocPtpListen", {own, 20000, 4096, 100, 1, 1});
    check(static_cast<int>(listener) > 0 && net("sceNetAdhocPtpListen", {own, 20000, 4096}) == portuse,
        "PTP listener reserves unique port");
    check(net("sceNetAdhocPtpConnect", {listener, 100, 1}) == invalidid &&
            net("sceNetAdhocPtpAccept", {listener, peer, port_out, 100, 1}) == wouldblock,
        "listener rejects connect and empty nonblocking accept");
    check(net("sceNetAdhocPtpAccept", {listener, peer, port_out, 100, 0}) == timeout,
        "blocking accept respects PSP timeout");
    check(wait_until([] { return Client::get().diagnostics().relay_links_up >= 2; }), "listener relay is established");
    ProtocolPeer stream(relay_port_for(server_port));
    stream.send(relay::init(relay::kInitPtpConnect, macB, 30000, macA, 20000));
    unsigned accepted = wouldblock;
    check(wait_until([&] {
        accepted = net("sceNetAdhocPtpAccept", {listener, peer, port_out, 100, 1});
        return accepted != wouldblock;
    }) && static_cast<int>(accepted) > 0,
        "HLE accepts real peer connection");
    stream.read(relay::kPtpNoticeSize);
    check(wait_until(
              [&] { return Client::get().ptp_info(static_cast<int>(accepted)).state == StreamState::Established; }),
        "accepted stream enters established state");
    check(net("sceNetAdhocPtpConnect", {accepted, 100, 1}) == 0 &&
            net("sceNetAdhocPtpAccept", {accepted, 0, 0, 100, 1}) == 0x8040070eu,
        "established stream connects immediately but cannot accept");
    check(net("sceNetAdhocPtpSend", {accepted, 0, length}) == invalid &&
            net("sceNetAdhocPtpRecv", {accepted, data, 0}) == invalid,
        "stream I/O requires data and length pointers");
    f.runtime.memory().store32(length, 5);
    f.string(data, "quest");
    check(net("sceNetAdhocPtpSend", {accepted, data, length, 100, 1}) == 0 && f.runtime.memory().load32(length) == 5,
        "HLE stream queues exact send count");
    check(stream.read(9).substr(4) == "quest", "peer receives framed stream payload");
    f.runtime.memory().store32(length, 0);
    check(net("sceNetAdhocPtpSend", {accepted, data, length, 100, 1}) == 0, "empty established stream send succeeds");
    check(net("sceNetAdhocPtpFlush", {accepted, 100000, 0}) == 0, "flush waits for actual stream send queue");
    f.runtime.memory().store32(length, 64);
    check(net("sceNetAdhocPtpRecv", {accepted, data, length, 100, 1}) == wouldblock &&
            net("sceNetAdhocPtpRecv", {accepted, data, length, 100, 0}) == timeout,
        "empty established stream supports nonblocking and timed waits");
    std::string framed;
    wire::put32(framed, 5);
    stream.send(framed + "ready");
    check(wait_until([&] { return Client::get().ptp_info(static_cast<int>(accepted)).readable == 5; }),
        "stream reply reaches client buffer");
    check(net("sceNetAdhocPtpRecv", {accepted, data, length, 100, 1}) == 0 && f.runtime.memory().load32(length) == 5 &&
            mhp3rd::read_cstring(f.runtime.memory(), data, 5) == "ready",
        "HLE returns exact stream reply bytes");
    check(net("sceNetAdhocGetPtpStat") == invalid && net("sceNetAdhocGetPtpStat", {length, 0}) == 0 &&
            f.runtime.memory().load32(length) == 72,
        "stream stat query counts listener and accepted sockets");
    check(net("sceNetAdhocGetPtpStat", {length, data}) == 0 && f.runtime.memory().load32(data) == data + 36 &&
            f.runtime.memory().load32(data + 36) == 0 && f.runtime.memory().load32(data + 36 + 28) == 0 &&
            f.runtime.memory().load32(data + 36 + 32) == 4,
        "stream stats link records and expose current buffer occupancy");
    stream.close();
    check(wait_until(
              [&] { return Client::get().ptp_info(static_cast<int>(accepted)).state == StreamState::Disconnected; }),
        "peer close reaches actual stream state");
    check(net("sceNetAdhocPtpRecv", {accepted, data, length, 100, 1}) == 0x8041070cu &&
            net("sceNetAdhocPtpSend", {accepted, data, length, 100, 1}) == 0x8041070cu &&
            net("sceNetAdhocPtpFlush", {accepted, 100, 1}) == 0x8041070cu,
        "disconnected stream exposes network disconnect error");
    check(net("sceNetAdhocPtpClose", {accepted}) == 0 && net("sceNetAdhocPtpClose", {listener}) == 0,
        "stream sockets close");
    check(net("sceNetAdhocPtpOpen") == addr && net("sceNetAdhocPtpOpen", {own, 22000, own, 23000, 4096}) == addr &&
            net("sceNetAdhocPtpOpen", {peer, 22000, own, 23000, 4096}) == addr,
        "stream opener validates own and remote addresses");
    check(net("sceNetAdhocPtpOpen", {own, 22000, peer, 23000, 0}) == buflen, "stream opener rejects empty buffer");
    const auto opening = net("sceNetAdhocPtpOpen", {own, 22000, peer, 23000, 4096, 1000000, 10});
    check(static_cast<int>(opening) > 0, "opener returns pending stream identity");
    check(net("sceNetAdhocPtpConnect", {opening, 100, 1}) == wouldblock &&
            net("sceNetAdhocPtpConnect", {opening, 100, 0}) == timeout,
        "pending outgoing connect supports polling and timeout");
    check(net("sceNetAdhocPtpSend", {opening, data, length, 100, 1}) == 0x8041070bu,
        "unconnected stream send reports not connected");
    check(net("sceNetAdhocPtpClose", {opening}) == 0, "pending connection cancels on close");
    check(ctl("sceNetAdhocctlDisconnect") == 0 && ctl("sceNetAdhocctlTerm") == 0 && net("sceNetAdhocTerm") == 0 &&
            !mhp3rd::adhoc_networking_on(),
        "shutdown clears control and socket initialization");
    auto utility = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("sceUtility", name, args);
    };
    for (const auto *name : {"sceNetAdhocDiscoverInitStart", "sceNetAdhocDiscoverUpdate", "sceNetAdhocDiscoverStop",
             "sceNetAdhocDiscoverTerm"})
        check(f.call("sceNetAdhocDiscover", name) == 0, "discovery transitions acknowledge request");
    check(f.call("sceNetAdhocDiscover", "sceNetAdhocDiscoverGetStatus") == 0,
        "terminated discovery reports no active request");
    check(f.call("sceNetAdhocDiscover", "sceNetAdhocDiscoverInitStart") == 0 &&
            f.call("sceNetAdhocDiscover", "sceNetAdhocDiscoverGetStatus") == 2,
        "synthetic discovery completes without nearby peer");
    check(utility("sceUtilityNetconfInitStart") == invalid &&
            utility("sceUtilityNetconfShutdownStart") == mhp3rd::kErrorUtilityInvalidStatus,
        "netconf requires parameters and active dialog");
    constexpr unsigned params = Fixture::text + 0x400, group = params + 0x100;
    f.runtime.memory().store32(params + 0x30, 2);
    f.runtime.memory().store32(params + 0x34, group);
    f.string(group, "PUBLIC01");
    settings.adhoc = false;
    check(utility("sceUtilityNetconfInitStart", {params}) == 0 &&
            utility("sceUtilityNetconfInitStart", {params}) == mhp3rd::kErrorUtilityInvalidStatus,
        "netconf starts once");
    check(utility("sceUtilityNetconfGetStatus") == 1 && utility("sceUtilityNetconfUpdate") == 0 &&
            utility("sceUtilityNetconfGetStatus") == 3 && f.runtime.memory().load32(params + 0x1c) == 0x80410b05u,
        "disabled networking completes dialog with control timeout");
    check(utility("sceUtilityNetconfShutdownStart") == 0 && utility("sceUtilityNetconfGetStatus") == 4 &&
            utility("sceUtilityNetconfGetStatus") == 0,
        "netconf shutdown completes lifecycle");
    settings.adhoc = true;
    check(utility("sceUtilityNetconfInitStart", {params}) == 0 && utility("sceUtilityNetconfShutdownStart") == 0,
        "joining request can be cancelled immediately");
    check(utility("sceUtilityNetconfGetStatus") == 4 && utility("sceUtilityNetconfGetStatus") == 0,
        "cancelled join reaches inactive state");
    f.runtime.memory().store32(params + 0x30, 0);
    check(utility("sceUtilityNetconfInitStart", {params}) == 0 && utility("sceUtilityNetconfGetStatus") == 1 &&
            utility("sceUtilityNetconfGetStatus") == 2 && utility("sceUtilityNetconfGetStatus") == 3 &&
            f.runtime.memory().load32(params + 0x1c) == 1,
        "unsupported action returns cancellation result");
    check(utility("sceUtilityNetconfShutdownStart") == 0 && utility("sceUtilityNetconfGetStatus") == 4 &&
            utility("sceUtilityNetconfGetStatus") == 0,
        "unsupported action still shuts down cleanly");
    settings.adhoc = false;
}
void hosting_contracts() {
    using namespace mhp3rd;
    using namespace mhp3rd::adhoc;
    struct Cleanup {
        ~Cleanup() { adhoc_shutdown(); }
    } cleanup;
    auto &settings = settings::current();
    settings.adhoc = false;
    settings.adhoc_server = "127.0.0.1:1";
    settings.adhoc_recent = {"127.0.0.1:2", "127.0.0.1:3", "127.0.0.1:4", "127.0.0.1:5", "127.0.0.1:6"};
    check(!adhoc_hosting() && adhoc_server_address() == settings.adhoc_server,
        "non-hosting session uses configured server address");
    Server occupied;
    unsigned port = 0;
    for (unsigned candidate = 37612; candidate < 37700; candidate += 2)
        if (occupied.start(ServerConfig{static_cast<std::uint16_t>(candidate), false})) {
            port = candidate;
            break;
        }
    check(port != 0, "hosting fixture reserves a port pair");
    settings.adhoc_host_port = static_cast<int>(port);
    check(!adhoc_host_start() && !adhoc_hosting() && !adhoc_host_error().empty(),
        "busy host port reports failure without marking session hosted");
    occupied.stop();
    check(adhoc_host_start() && adhoc_hosting() && settings.adhoc && adhoc_host_error().empty(),
        "hosting enables network and clears previous startup error");
    check(adhoc_host_status().running && adhoc_host_status().adhocctl_port == port &&
            adhoc_server_address() == "127.0.0.1:" + std::to_string(port) && adhoc_host_start(),
        "hosting status and loopback address agree and repeated start is idempotent");
    adhoc_join("");
    check(adhoc_hosting(), "empty join does not interrupt hosted session");
    const auto address = "127.0.0.1:" + std::to_string(port);
    adhoc_join(address);
    check(!adhoc_hosting() && settings.adhoc_server == address && settings.adhoc_recent.size() == 5 &&
            settings.adhoc_recent.front() == address,
        "join stops hosting, updates destination and caps recent addresses");
    adhoc_join(address);
    check(settings.adhoc_recent.size() == 5 &&
            std::count(settings.adhoc_recent.begin(), settings.adhoc_recent.end(), address) == 1,
        "repeated join moves an existing address without duplicates");
    adhoc_host_stop();
    check(!adhoc_hosting(), "stopping inactive host is idempotent");
    check(adhoc_host_start(), "stopped host can restart");
    adhoc_host_stop();
    check(!adhoc_hosting() && !adhoc_host_status().running && adhoc_server_address() == address,
        "stop clears hosted state and restores configured destination");
}
void system_contracts() {
    Fixture f;
    for (const auto *name : {"sceKernelDcacheWritebackAll", "sceKernelDcacheWritebackInvalidateAll",
             "sceKernelDcacheInvalidateRange", "sceKernelDcacheWritebackRange", "sceKernelSetGPO",
             "sceKernelIcacheInvalidateAll", "sceKernelIcacheInvalidateRange"})
        check(f.call("UtilsForUser", name) == 0, "cache maintenance completes on public host");
    const auto seconds = f.call("UtilsForUser", "sceKernelLibcTime", {Fixture::output});
    check(seconds == f.runtime.memory().load32(Fixture::output), "libc time return and output agree");
    check(f.call("UtilsForUser", "sceKernelLibcTime") != 0, "libc time accepts optional output");
    check(f.call("UtilsForUser", "sceKernelLibcGettimeofday", {Fixture::output, Fixture::output + 16}) == 0,
        "gettimeofday writes guest clock and timezone");
    const auto before = static_cast<std::uint64_t>(f.runtime.memory().load32(Fixture::output)) * 1000000 +
        f.runtime.memory().load32(Fixture::output + 4);
    check(f.runtime.memory().load32(Fixture::output + 16) == 0 && f.runtime.memory().load32(Fixture::output + 20) == 0,
        "timezone reports UTC offset and no DST");
    f.kernel.on_starvation(f.ctx);
    f.call("UtilsForUser", "sceKernelLibcGettimeofday", {Fixture::output});
    const auto after = static_cast<std::uint64_t>(f.runtime.memory().load32(Fixture::output)) * 1000000 +
        f.runtime.memory().load32(Fixture::output + 4);
    check(after - before == 1000 && f.call("UtilsForUser", "sceKernelLibcClock") == 1000,
        "libc clock follows virtual time independent of host wait");
    check(f.call("UtilsForUser", "sceKernelLibcGettimeofday") == 0, "gettimeofday accepts absent outputs");
    for (unsigned index = 0; index < 3; ++index)
        check(f.call("StdioForUser",
                  index == 0       ? "sceKernelStdin"
                      : index == 1 ? "sceKernelStdout"
                                   : "sceKernelStderr") == index,
            "stdio descriptors are stable");
    check(f.call("ModuleMgrForUser", "sceKernelGetModuleId") == 0x01000001 &&
            f.call("ModuleMgrForUser", "sceKernelGetModuleIdByAddress", {0x08820000}) == 0x01000001,
        "main module identity is stable");
    check(static_cast<int>(f.call("ModuleMgrForUser", "sceKernelLoadModuleByID", {0xffffffffu})) > 0,
        "stock HLE module acknowledgment assigns an ID");
    for (const auto *name : {"sceKernelStartModule", "sceKernelStopModule", "sceKernelUnloadModule"})
        check(f.call("ModuleMgrForUser", name, {0x1234}) == 0x1234, "module lifecycle acknowledges supplied identity");
    const auto cb = f.thread("sceKernelCreateCallback", {Fixture::text, 0x08826000, 0});
    check(f.call("LoadExecForUser", "sceKernelRegisterExitCallback", {cb}) == 0 &&
            f.call("scePower", "scePowerRegisterCallback", {0, cb}) == 0 &&
            f.kernel.callbacks.at(cb).notify_argument == 0x1084,
        "power callback reports AC and healthy battery");
    for (const auto *name : {"scePowerSetClockFrequency630", "scePowerCheckWlanCoexistenceClock"})
        check(f.call("scePower", name) == 0, "power clock settings acknowledged");
    check(f.call("sceRtc", "sceRtcGetCurrentClockLocalTime", {Fixture::output}) == 0 &&
            f.runtime.memory().load16(Fixture::output) >= 2000 && f.runtime.memory().load16(Fixture::output + 2) >= 1 &&
            f.runtime.memory().load16(Fixture::output + 2) <= 12 &&
            f.runtime.memory().load32(Fixture::output + 12) < 1000000,
        "RTC reports valid date and fractional microseconds");
    check(f.call("sceImpose", "sceImposeSetLanguageMode") == 0 &&
            f.call("sceOpenPSID", "sceOpenPSIDGetOpenPSID", {Fixture::output}) == 0,
        "platform locale and public identifier calls complete");
    for (unsigned i = 0; i < 16; ++i)
        check(
            f.runtime.memory().load8(Fixture::output + i) == 0x10 + i, "synthetic identifier bytes are deterministic");
    for (auto parameter : {2u, 4u, 5u, 8u, 9u, 99u})
        check(f.call("sceUtility", "sceUtilityGetSystemParamInt", {parameter, Fixture::output}) == 0 &&
                f.runtime.memory().load32(Fixture::output) == (parameter == 2 ? 1u : 0u),
            "system preferences return supported defaults");
    check(f.call("sceUtility", "sceUtilityLoadModule") == 0 && f.call("sceUtility", "sceUtilityUnloadModule") == 0,
        "utility module loading acknowledged");
    f.call("LoadExecForUser", "sceKernelExitGame");
    check(f.runtime.stopped() && f.runtime.stop_reason().find("ExitGame") != std::string::npos,
        "guest exit stops runtime with reason");
}
void thread_end_and_dispatch_contracts() {
    Fixture f;
    const auto loader = f.kernel.current_uid();
    const auto helper = f.helper();
    mhp3rd::WaitState join{};
    join.type = mhp3rd::WaitType::ThreadEnd;
    join.object = static_cast<int>(helper);
    f.kernel.block(f.ctx, join);
    check(f.kernel.current_uid() == static_cast<int>(helper), "thread-end wait schedules its target");
    f.kernel.exit_current_thread(f.ctx, 17, false);
    check(f.kernel.current_uid() == loader && f.ctx.gpr[2] == 17, "thread exit wakes joiner with exit status");
    check(f.thread("sceKernelGetThreadExitStatus", {helper}) == 17 &&
            f.thread("sceKernelGetThreadExitStatus", {static_cast<unsigned>(loader)}) == mhp3rd::error::kNotDormant &&
            f.thread("sceKernelGetThreadExitStatus", {0xffffffffu}) == mhp3rd::error::kUnknownThid,
        "exit status API distinguishes dormant, live and missing threads");
    check(f.thread("sceKernelChangeThreadPriority", {helper, 1}) == mhp3rd::error::kDormant,
        "dormant priority change rejected");
    check(f.thread("sceKernelDeleteThread", {helper}) == 0 &&
            f.thread("sceKernelDeleteThread", {static_cast<unsigned>(loader)}) == mhp3rd::error::kNotDormant,
        "delete HLE distinguishes dormant and current thread");
    check(f.thread("sceKernelChangeCurrentThreadAttr", {0xffffffffu, 0x100000}) == 0 &&
            f.kernel.current_thread()->attributes == 0x100000,
        "thread attribute masks are applied");
    const auto child = f.thread("sceKernelCreateThread", {Fixture::text, 0x08824000, 0x30, 4096, 0x100000});
    check(static_cast<int>(child) > 0 && f.thread("sceKernelStartThread", {child}) == 0,
        "thread HLE creates and starts child");
    check(
        f.thread("sceKernelChangeThreadPriority", {child, 0x40}) == 0 && f.kernel.find_thread(child)->priority == 0x40,
        "ready thread priority changes");
    check(f.thread("sceKernelTerminateThread", {child}) == 0 &&
            f.thread("sceKernelTerminateDeleteThread", {child}) == 0 && f.kernel.find_thread(child) == nullptr,
        "termination resets and deletes child");
    f.kernel.set_dispatch_enabled(false);
    const auto high = f.helper(0x10);
    f.kernel.finish(f.ctx, 81);
    f.kernel.on_starvation(f.ctx);
    check(f.kernel.current_uid() == loader && f.ctx.gpr[2] == 81,
        "disabled dispatch prevents finish and starvation preemption");
    f.kernel.set_dispatch_enabled(true);
    f.kernel.finish(f.ctx, 82);
    check(f.kernel.current_uid() == static_cast<int>(high), "reenabling dispatch honors ready priority");
    f.thread("sceKernelExitDeleteThread", {29});
    check(f.kernel.find_thread(high) == nullptr && f.kernel.current_uid() == loader,
        "exit-delete frees current child and resumes loader");
    const auto normal = f.helper(0x10);
    f.kernel.finish(f.ctx, 0);
    f.thread("sceKernelExitThread", {31});
    check(f.kernel.find_thread(normal)->exit_status == 31 && f.kernel.current_uid() == loader,
        "exit HLE preserves dormant thread status");
    check(f.call("Kernel_Library", "sceKernelGetThreadId") == static_cast<unsigned>(loader),
        "kernel library identity shares scheduler state");
    const auto exhausted = f.kernel.allocate_block("exhausted", 0, f.kernel.free_memory(), 0);
    check(exhausted > 0 &&
            static_cast<unsigned>(f.kernel.create_thread("no-stack", 0x08824000, 0x30, 4096, 0, 0)) ==
                mhp3rd::error::kNoMemory,
        "thread creation rejects unavailable stack memory");
    check(f.kernel.free_block(exhausted) == 0, "stack exhaustion fixture restores all available memory");
}
void scheduler_deadlock_contracts() {
    Fixture f;
    const auto dormant = f.kernel.create_thread("not-started", 0x08824000, 0x30, 4096, 0, 0);
    check(dormant > 0, "deadlock fixture retains a dormant thread");
    f.thread("sceKernelSleepThread");
    check(f.runtime.stopped() && f.ctx.pc == mhp3rd::kIdleStub,
        "permanent sleep without runnable threads eventually stops in idle");
    const auto &reason = f.runtime.stop_reason();
    check(reason.find("PSP scheduler deadlock: no runnable thread") != std::string::npos &&
            reason.find("not-started status=dormant") != std::string::npos &&
            reason.find("status=waiting wait=sleep") != std::string::npos &&
            reason.find("resume=") != std::string::npos,
        "deadlock diagnostic identifies dormant and waiting threads with resume address");
}
void utility_and_savedata_contracts() {
    struct Capture {
        std::ostringstream output;
        std::streambuf *previous = std::cout.rdbuf(output.rdbuf());
        ~Capture() { std::cout.rdbuf(previous); }
    } capture;
    Fixture f;
    PublicFiles files;
    mhp3rd::HleRegistrar hle(f.runtime);
    mhp3rd::register_utility(hle, files.root / "ms");
    auto call = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("sceUtility", name, args);
    };
    auto &memory = f.runtime.memory();
    constexpr unsigned params = Fixture::text + 0x1000, field = params + 0x800, data = params + 0x1000,
                       aux = data + 0x1000;
    check(call("sceUtilityOskShutdownStart") == mhp3rd::kErrorUtilityInvalidStatus, "inactive OSK cannot shut down");
    memory.store32(params, 0x40);
    memory.store32(params + 0x30, 1);
    memory.store32(params + 0x34, field);
    memory.store32(field + 0x24, 8);
    memory.store32(field + 0x28, data);
    mhp3rd::settings::current().name = "A\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80Z";
    auto finish_osk = [&] {
        check(call("sceUtilityOskGetStatus") == 1 && call("sceUtilityOskGetStatus") == 2 &&
                call("sceUtilityOskGetStatus") == 3,
            "headless OSK converges init/visible/quit");
        check(
            memory.load32(field + 0x2c) == 2 && memory.load32(params + 0x1c) == 0 && memory.load32(params + 0x38) == 3,
            "OSK publishes changed result and shared state");
        check(call("sceUtilityOskShutdownStart") == 0 && call("sceUtilityOskGetStatus") == 4 &&
                call("sceUtilityOskGetStatus") == 0,
            "OSK shutdown converges to none");
    };
    check(call("sceUtilityOskInitStart", {params}) == 0 &&
            call("sceUtilityOskInitStart", {params}) == mhp3rd::kErrorUtilityInvalidStatus,
        "OSK cannot start a second active request");
    finish_osk();
    const std::array<unsigned, 7> units{'A', 0xe9, 0x65e5, 0xd83d, 0xde00, 'Z', 0};
    for (unsigned i = 0; i < units.size(); ++i)
        check(memory.load16(data + i * 2) == units[i], "UTF-8 fixed name converted to expected UTF-16 units");
    memory.store32(field + 0x24, 5);
    check(call("sceUtilityOskInitStart", {params}) == 0 && call("sceUtilityOskGetStatus") == 1 &&
            call("sceUtilityOskUpdate") == 0 && call("sceUtilityOskGetStatus") == 3,
        "Update can deliver headless answer");
    check(memory.load16(data + 6) == 0, "insufficient room never splits surrogate pair");
    check(call("sceUtilityOskShutdownStart") == 0 && call("sceUtilityOskGetStatus") == 4 &&
            call("sceUtilityOskGetStatus") == 0,
        "updated OSK shuts down");
    memory.store32(field + 0x24, 8);
    memory.store32(field + 0x30, 2);
    check(call("sceUtilityOskInitStart", {params}) == 0, "limited name request starts");
    finish_osk();
    check(memory.load16(data + 4) == 0, "explicit output limit clamps text length");
    memory.store32(field + 0x30, 0);
    memory.store32(field + 0x20, aux);
    for (unsigned i = 0; i < units.size(); ++i) memory.store16(aux + i * 2, static_cast<std::uint16_t>(units[i]));
    check(call("sceUtilityOskInitStart", {params}) == 0, "existing text request starts");
    finish_osk();
    for (unsigned i = 0; i < units.size(); ++i)
        check(memory.load16(data + i * 2) == units[i], "initial UTF-16 text round trips through host answer");
    memory.store32(params + 0x30, 0);
    check(call("sceUtilityOskInitStart", {params}) == 0 && call("sceUtilityOskGetStatus") == 1 &&
            call("sceUtilityOskGetStatus") == 2 && call("sceUtilityOskGetStatus") == 3,
        "zero-field OSK still reaches quit");
    check(call("sceUtilityOskShutdownStart") == 0 && call("sceUtilityOskGetStatus") == 4 &&
            call("sceUtilityOskGetStatus") == 0,
        "zero-field OSK cleans up");
    if (std::getenv("MHP3RD_TRACE_OSK") != nullptr) {
        const auto diagnostic = capture.output.str();
        check(diagnostic.find("[osk-trace] InitStart") != std::string::npos &&
                diagnostic.find("field+") != std::string::npos &&
                diagnostic.find("[osk-trace] Update") != std::string::npos &&
                diagnostic.find("[osk-trace] ShutdownStart") != std::string::npos,
            "OSK diagnostics describe request fields and complete lifecycle");
        check(diagnostic.size() < 32768, "OSK trace stays bounded for synthetic requests");
    }
    check(call("sceUtilityMsgDialogShutdownStart") == mhp3rd::kErrorUtilityInvalidStatus,
        "inactive message dialog rejects shutdown");
    memory.store32(params, 0x244);
    memory.store32(params + 0x34, 1);
    f.string(params + 0x3c, "public synthetic question");
    memory.store32(params + 0x23c, 0x110);
    check(call("sceUtilityMsgDialogInitStart", {params}) == 0 && memory.load32(params + 0x240) == 1 &&
            memory.load32(params + 0x1c) == 0,
        "message question answers yes with successful result");
    check(call("sceUtilityMsgDialogGetStatus") == 1 && call("sceUtilityMsgDialogUpdate") == 0 &&
            call("sceUtilityMsgDialogGetStatus") == 3 && call("sceUtilityMsgDialogShutdownStart") == 0 &&
            call("sceUtilityMsgDialogGetStatus") == 4 && call("sceUtilityMsgDialogGetStatus") == 0,
        "message dialog obeys complete lifecycle");
    memory.store32(params, 0x40);
    memory.store32(params + 0x34, 0);
    memory.store32(params + 0x38, 0x1234);
    memory.store32(params + 0x240, 0xfeed);
    check(call("sceUtilityMsgDialogInitStart", {params}) == 0 && memory.load32(params + 0x240) == 0xfeed &&
            call("sceUtilityMsgDialogShutdownStart") == 0,
        "short error dialog preserves absent button field and allows early shutdown");
    check(call("sceUtilityMsgDialogGetStatus") == 4 && call("sceUtilityMsgDialogGetStatus") == 0,
        "early message shutdown still converges");
    for (unsigned i = 0; i < 0x600; i += 4) memory.store32(params + i, 0);
    memory.store32(params, 0x600);
    f.string(params + 0x3c, "PUB000001");
    f.string(params + 0x4c, "SLOT0");
    f.string(params + 0x64, "DATA.DAT");
    memory.store32(params + 0x74, data);
    memory.store32(params + 0x78, 64);
    memory.store32(params + 0x7c, 12);
    f.string(data, "public save!");
    f.string(params + 0x80, "Public title");
    f.string(params + 0x100, "Synthetic slot");
    f.string(params + 0x180, "No game content");
    auto request = [&](unsigned mode) {
        memory.store32(params + 0x30, mode);
        check(call("sceUtilitySavedataInitStart", {params}) == 0, "save dialog acknowledges request");
        const auto result = memory.load32(params + 0x1c);
        check(call("sceUtilitySavedataGetStatus") == 1 && call("sceUtilitySavedataUpdate") == 0 &&
                call("sceUtilitySavedataGetStatus") == 3 && call("sceUtilitySavedataShutdownStart") == 0 &&
                call("sceUtilitySavedataGetStatus") == 4 && call("sceUtilitySavedataGetStatus") == 0,
            "save success/error lifecycle always converges");
        return result;
    };
    check(call("sceUtilitySavedataShutdownStart") == mhp3rd::kErrorUtilityInvalidStatus && request(0) == 0x80110307u,
        "missing save reports no data");
    memory.store32(params + 0x74, 0);
    check(request(0) == 0x80110308u && request(1) == 0x80110388u, "load/save require guest buffer");
    memory.store32(params + 0x74, data);
    memory.store32(params + 0x584, aux);
    memory.store32(params + 0x588, 4);
    memory.store32(params + 0x58c, 8);
    f.string(aux, "ICON");
    check(request(1) == 0 && std::filesystem::exists(files.root / "ms/PSP/SAVEDATA/PUB000001SLOT0/DATA.DAT"),
        "autosave writes public PSP layout");
    check(std::filesystem::file_size(files.root / "ms/PSP/SAVEDATA/PUB000001SLOT0/ICON0.PNG") == 4,
        "icon data clamps requested size to guest capacity");
    f.string(data, "overwritten");
    memory.store32(params + 0x78, 5);
    check(request(2) == 0 && memory.load32(params + 0x7c) == 5 && mhp3rd::read_cstring(memory, data, 5) == "publi" &&
            mhp3rd::read_cstring(memory, params + 0x80) == "Public title",
        "load truncates payload and restores metadata");
    memory.store32(params + 0x78, 64);
    memory.store32(params + 0x5d0, aux + 0x100);
    memory.store32(params + 0x5d4, aux + 0x200);
    memory.store32(params + 0x5d8, aux + 0x300);
    f.string(aux + 0x200, "PUB000001");
    f.string(aux + 0x210, "SLOT0");
    check(request(8) == 0 && memory.load32(aux + 0x100) == 0x8000 &&
            mhp3rd::read_cstring(memory, aux + 0x10c) == "1 GB" && memory.load32(aux + 0x300) >= 1,
        "sizes reports free-space geometry and needed clusters");
    f.string(aux + 0x210, "MISSING");
    check(request(8) == 0x801103c7u, "sizes reports absent slot");
    memory.store32(params + 0x5d4, 0);
    memory.store32(params + 0x7c, 2 * 1024 * 1024);
    check(request(8) == 0 && mhp3rd::read_cstring(memory, aux + 0x308).find("MB") != std::string::npos,
        "needed-space formatting supports megabytes");
    memory.store32(params + 0x7c, 12);
    memory.store32(params + 0x60, aux + 0x400);
    f.string(aux + 0x400, "MISSING");
    f.string(aux + 0x414, "SLOT0");
    check(request(4) == 0 && mhp3rd::read_cstring(memory, params + 0x4c) == "SLOT0",
        "list load chooses first existing slot");
    check(request(6) == 0 && request(6) == 0x80110347u && request(4) == 0x80110307u,
        "list delete removes selected save and missing list paths report correct error");
    f.string(aux + 0x400, "SLOT1");
    memory.store8(aux + 0x414, 0);
    f.string(data, "public save!");
    check(request(5) == 0 && mhp3rd::read_cstring(memory, params + 0x4c) == "SLOT1",
        "list save chooses first provided slot");
    memory.store32(params + 0x60, 0);
    for (auto mode : {7u, 9u, 10u}) {
        check(request(3) == 0 && request(mode) == 0 && request(mode) == 0x80110347u,
            "delete variants remove one slot and report missing data");
    }
    f.string(params + 0x4c, "<>");
    check(request(5) == 0 && std::filesystem::exists(files.root / "ms/PSP/SAVEDATA/PUB000001/DATA.DAT"),
        "empty-save-name marker maps to game directory");
    check(request(11) == 0x80110308u && request(99) == 0x80110308u, "unsupported save mode reports parameter error");
    f.string(params + 0x4c, "SECURE");
    for (unsigned i = 0; i < 16; ++i) memory.store8(params + 0x5dc + i, static_cast<std::uint8_t>(i + 1));
    check(request(1) == 0 && request(0) == 0 && memory.load32(params + 0x7c) == 16 &&
            mhp3rd::read_cstring(memory, data, 12) == "public save!",
        "synthetic secure save encrypts and decrypts through HLE");
    {
        std::ofstream corrupt(
            files.root / "ms/PSP/SAVEDATA/PUB000001SECURE/DATA.DAT", std::ios::binary | std::ios::trunc);
        corrupt << "broken";
    }
    check(request(0) == 0x80110306u, "corrupted encrypted save reports broken data");
    const auto blocked = files.root / "blocked";
    {
        std::ofstream block(blocked);
        block << "not a directory";
    }
    mhp3rd::register_savedata(hle, blocked);
    check(request(1) == 0x80110385u, "non-directory memory stick reports save access error");
}

// An independently authored minimal sfnt with one square glyph, no external font.
static std::array<unsigned char, 640> contract_font() {
    std::array<unsigned char, 640> data{};
    auto u16 = [&](int at, unsigned value) {
        data[at] = static_cast<unsigned char>(value >> 8);
        data[at + 1] = static_cast<unsigned char>(value);
    };
    auto u32 = [&](int at, unsigned value) {
        u16(at, value >> 16);
        u16(at + 2, value);
    };
    u32(0, 0x00010000);
    u16(4, 7);
    const char *tags[] = {"cmap", "head", "hhea", "hmtx", "maxp", "loca", "glyf"};
    const unsigned offsets[] = {128, 416, 480, 528, 544, 560, 576};
    const unsigned lengths[] = {274, 54, 36, 8, 6, 12, 34};
    for (int i = 0; i < 7; ++i) {
        std::memcpy(data.data() + 12 + i * 16, tags[i], 4);
        u32(20 + i * 16, offsets[i]);
        u32(24 + i * 16, lengths[i]);
    }
    u16(130, 1);
    u16(132, 3);
    u16(134, 1);
    u32(136, 12);
    u16(142, 262); // Format 0 cmap with a public square at glyph 1.
    for (unsigned code = 0x21; code < 0x7f; ++code) data[146 + code] = 1;
    u16(434, 20);
    u16(466, 1); // units per em, long loca offsets.
    u16(484, 20);
    u16(514, 2);
    u16(528, 20);
    u16(532, 20); // ascent and horizontal metrics.
    u16(548, 2);
    u32(564, 0);
    u32(568, 34);
    u16(576, 1);
    u16(582, 20);
    u16(584, 20);                                  // contour and bounding box.
    u16(586, 3);                                   // last point of the four-point contour; no instructions.
    for (int i = 0; i < 4; ++i) data[590 + i] = 1; // on-curve, signed deltas.
    u16(596, 20);
    u16(600, static_cast<unsigned>(-20));
    u16(606, 20);
    return data;
}
void font_contracts() {
    Fixture f;
    PublicFiles files;
    const auto bytes = contract_font();
    const auto path = files.root / "square.ttf";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    auto &settings = mhp3rd::settings::current();
    settings.font = path.string();
    settings.font_weight = 0;
    mhp3rd::fonts::reload();
    check(mhp3rd::fonts::ready() && mhp3rd::fonts::metrics('A').found, "independently authored square font loads");
    mhp3rd::HleRegistrar hle(f.runtime);
    mhp3rd::register_font(hle);
    auto call = [&](const char *name, std::initializer_list<unsigned> args = {}) {
        return f.call("sceLibFont", name, args);
    };
    auto &memory = f.runtime.memory();
    constexpr unsigned info = Fixture::output, image = info + 256, pixels = info + 512;
    check(call("sceFontNewLib", {info, info + 128}) == 0xf0f000 && memory.load32(info + 128) == 0,
        "font library returns stable handle and success output");
    check(call("sceFontGetNumFontList", {0xf0f000, info + 128}) == 1 && memory.load32(info + 128) == 0,
        "one host font is exposed");
    check(call("sceFontFindOptimumFont", {0xf0f000, info, info + 128}) == 0 && memory.load32(info + 128) == 0,
        "optimum search selects host font index");
    const auto font = call("sceFontOpen", {0xf0f000, 0, 0, info + 128});
    check(font == 0xf0f100 && memory.load32(info + 128) == 0, "font opens with ready handle");
    check(call("sceFontGetFontInfo", {font, info}) == 0 && memory.load16(info + 80) == 20 &&
            memory.load16(info + 82) == 20 && memory.load32(info + 84) == 0x10000,
        "font info reports safe cell dimensions and glyph count");
    check(call("sceFontGetFontInfo", {font, 0}) == 0 && call("sceFontGetCharInfo", {font, 'A', 0}) == 0,
        "font info APIs accept absent output");
    check(call("sceFontGetCharInfo", {font, 'A', info}) == 0 && memory.load32(info) > 0 && memory.load32(info) <= 20 &&
            memory.load32(info + 4) <= 20 && memory.load32(info + 16) == memory.load32(info) * 64,
        "glyph metrics agree in pixels and 26.6 fixed point");
    for (unsigned format = 0; format < 5; ++format) {
        const unsigned stride = format < 2 ? 10 : format == 2 ? 20 : format == 3 ? 60 : 80;
        memory.store32(image, format);
        memory.store32(image + 4, 0);
        memory.store32(image + 8, 15 * 64);
        memory.store16(image + 12, 20);
        memory.store16(image + 14, 20);
        memory.store16(image + 16, static_cast<std::uint16_t>(stride));
        memory.store32(image + 20, pixels);
        for (unsigned i = 0; i < stride * 20; ++i) memory.store8(pixels + i, 0);
        check(call("sceFontGetCharGlyphImage", {font, 'A', image}) == 0, "glyph image call supports all pixel formats");
        bool ink = false;
        std::vector<std::uint8_t> before(stride * 20);
        for (unsigned i = 0; i < before.size(); ++i) {
            before[i] = memory.load8(pixels + i);
            ink |= before[i] != 0;
        }
        check(ink, "synthetic square produces observable ink");
        check(call("sceFontGetCharGlyphImage", {font, 'A', image}) == 0, "second glyph pass succeeds");
        for (unsigned i = 0; i < before.size(); ++i)
            check(memory.load8(pixels + i) == before[i], "repeated ink pass is idempotent");
        memory.store32(image + 4, 0xffffffe0u);
        memory.store32(image + 8, 0xffffffe0u);
        check(call("sceFontGetCharGlyphImage", {font, 'A', image}) == 0,
            "negative fractional positions clip without invalid memory access");
    }
    memory.store32(image + 20, 0);
    check(call("sceFontGetCharGlyphImage", {font, 'A', image}) == 0 &&
            call("sceFontGetCharGlyphImage", {font, 'A', 0}) == 0,
        "missing glyph buffer/image safely ignored");
    constexpr unsigned atlas = 0x08b00000u, table = atlas + 22168;
    memory.store8(atlas + 276, 20);
    memory.store8(atlas + 277, 20);
    memory.store16(atlas + 286, 12 * 11 * 8);
    f.return_address = 0x088ea3a4;
    f.ctx.set_gpr(17, atlas);
    call("sceFontGetCharGlyphImage", {font, 'A', 0});
    check(mhp3rd::fonts::game_atlas() == atlas, "synthetic recognized atlas object is recorded");
    memory.store16(table, 1);
    memory.store16(table + 0x1000, 2);
    memory.store16(table + (0xfff0 - 1) * 2, 3);
    mhp3rd::fonts::reload();
    check(memory.load16(table) == 0xffff && memory.load16(table + 0x1000) == 0xffff &&
            memory.load16(table + (0xfff0 - 1) * 2) == 0xffff,
        "font reload invalidates complete recognized guest glyph map");
    check(call("sceFontClose", {font}) == 0 && call("sceFontDoneLib", {0xf0f000}) == 0,
        "font and library close successfully");
    mhp3rd::fonts::set_reload_hook(nullptr);
}

}
int main() {
    try {
        PublicFiles environment;
        mhp3rd::install::set_data_directory_override(environment.root / "data");
        memory_contracts();
        common_hle_contracts();
        scheduler_contracts();
        thread_end_and_dispatch_contracts();
        semaphore_contracts();
        event_flag_contracts();
        mutex_contracts();
        interrupt_and_host_wait_contracts();
        vtimer_contracts();
        sysmem_contracts();
        io_contracts();
        network_contracts();
        system_contracts();
        scheduler_deadlock_contracts();
        utility_and_savedata_contracts();
        font_contracts();
        hosting_contracts();
        std::cout << "kernel/HLE contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
