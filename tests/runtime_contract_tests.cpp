#include "psprecomp/decoder.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/deflate.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/interpreter.hpp"
#include "psprecomp/program_analysis.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <optional>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace psprecomp {
void set_write_watch(std::uint32_t address, std::uint32_t size);
}

namespace {
constexpr std::uint32_t base = 0x08804000u;
constexpr std::uint32_t scratch = base + 0x1000u;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
constexpr std::uint32_t r(unsigned rs, unsigned rt, unsigned rd, unsigned shift, unsigned function) {
    return rs << 21u | rt << 16u | rd << 11u | shift << 6u | function;
}
constexpr std::uint32_t i(unsigned opcode, unsigned rs, unsigned rt, std::uint16_t immediate) {
    return opcode << 26u | rs << 21u | rt << 16u | immediate;
}
void step(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &cpu, std::uint32_t word) {
    runtime.memory().store32(base, word);
    cpu.pc = base;
    check(psprecomp::interpret_allegrex(runtime, cpu, 1u) == psprecomp::InterpreterExit::Budget,
        "Single instruction did not advance to its bounded next PC");
    check(cpu.pc == base + 4u && cpu.gpr[0] == 0u, "Instruction corrupted PC or zero register");
}
class Environment {
public:
    std::string name;
    std::optional<std::string> previous;
    Environment(const char *key, const char *value) : name(key) {
        if (const char *old = std::getenv(key)) previous = old;
        set(value);
    }
    void set(const char *value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value)
            setenv(name.c_str(), value, 1);
        else
            unsetenv(name.c_str());
#endif
    }
    ~Environment() { set(previous ? previous->c_str() : nullptr); }
};
void next(psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) {
    cpu.gpr[2]++;
    cpu.pc += 4;
}
void stop(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &) {
    runtime.stop("completed public dispatch contract");
}
void fault(psprecomp::Runtime &, psprecomp::AllegrexContext &) {
    throw psprecomp::Error("synthetic guest memory fault");
}
void stay(psprecomp::Runtime &, psprecomp::AllegrexContext &) {}
void entry(
    psprecomp::Runtime &, psprecomp::AllegrexContext &cpu, std::uint16_t, psprecomp::GuestMemory::AotFastView &memory) {
    cpu.gpr[2] = memory.aot_load32(scratch);
    cpu.pc += 4;
}
unsigned heartbeat_hits = 0;
void heartbeat(std::uint64_t, std::uint32_t) {
    ++heartbeat_hits;
}
void dispatch_contracts() {
    {
        psprecomp::Runtime runtime;
        runtime.register_function(base + 0x10000, next, "recomp_unit_far");
        runtime.register_function(base, next, "recomp_unit_low");
        runtime.register_function(base + 1, next, "unaligned_override");
        check(runtime.has_function(base) && runtime.has_function(base + 1) && !runtime.has_function(base + 2),
            "Exact and aligned dispatch lookup changed");
        psprecomp::AllegrexContext cpu{};
        check(!runtime.invoke_isolated_aot(base + 2, cpu) && runtime.invoke_isolated_aot(base, cpu) && cpu.gpr[2] == 1,
            "Isolated AOT dispatch contract changed");
        runtime.unregister_functions(base, base + 2);
        check(!runtime.has_function(base) && !runtime.has_function(base + 1) && runtime.has_function(base + 0x10000),
            "Unregister range must be half-open");
        bool threw = false;
        try {
            runtime.register_function(base, nullptr, "invalid");
        } catch (const psprecomp::Error &) {
            threw = true;
        }
        check(threw, "Null generated callbacks must be rejected");
        runtime.register_generated_unit(0, base, 0x4000, next, entry);
        runtime.register_function(base, next, "recomp_unit_zero");
        runtime.memory().store32(scratch, 42);
        auto memory = runtime.memory().aot_fast_view();
        cpu.pc = base;
        check(runtime.invoke_chained_unit(cpu, 0, &memory) && cpu.gpr[2] == 42,
            "Shared-memory generated entry must execute");
        cpu.pc = base;
        check(runtime.invoke_chained_call(cpu, &memory) && cpu.gpr[2] == 42,
            "Indirect chain must use registered shared-memory entry");
        check(!runtime.invoke_chained_unit(cpu, 4096), "Out-of-range generated unit must not dispatch");
        runtime.register_generated_unit(1, base + 0x5000, 0x4000, next);
        cpu.pc = base;
        check(!runtime.invoke_chained_unit(cpu, 0), "Conflicting generated layouts must disable dense path");
        check(runtime.invoke_chained_call(cpu), "Conflicting layout must retain exact registered dispatch");
        runtime.register_native_fast_path(base, [](auto &, auto &state) {
            state.gpr[2] = 77;
            state.pc += 4;
        });
        cpu.pc = base;
        runtime.invoke_native_fast_path(base, cpu);
        check(cpu.gpr[2] == 77, "Native profile override did not run");
        runtime.register_native_fast_path(base, {});
        cpu.pc = base;
        runtime.invoke_native_fast_path(base, cpu);
        check(cpu.gpr[2] == 78, "Removed native override must fall back to AOT");
        cpu.pc = base + 0x10000;
        runtime.invoke_native_fast_path(base, cpu);
        check(cpu.gpr[2] == 79, "Varying indirect target must preserve AOT fallback");
        cpu.pc = 0;
        runtime.invoke_native_fast_path(base, cpu);
        cpu.pc = base + 2;
        runtime.invoke_native_fast_path(base, cpu);
        cpu.pc = 0x08F00000;
        runtime.invoke_native_fast_path(base, cpu);
        bool missing = false;
        cpu.pc = base + 4;
        try {
            runtime.invoke_native_fast_path(base + 4, cpu);
        } catch (const psprecomp::Error &) {
            missing = true;
        }
        check(missing, "Missing native AOT fallback must diagnose the missing function");
    }
    {
        Environment no_chain("PSPRECOMP_NO_CHAIN", "1");
        psprecomp::Runtime runtime;
        runtime.register_function(base, next, "recomp_unit_zero");
        auto cpu = runtime.cpu();
        cpu.pc = base;
        check(!runtime.invoke_chained_call(cpu), "Disabled chaining must not execute a guest function");
    }
    {
        Environment histogram("PSPRECOMP_HLE_HISTOGRAM", "1");
        psprecomp::Runtime runtime;
        runtime.register_hle("public", 1, [](auto &, auto &cpu) { cpu.gpr[2] = 99; });
        runtime.invoke_import("public", 1, runtime.cpu());
        runtime.invoke_import_cached(300, "public", 1, runtime.cpu());
        runtime.invoke_import_cached(300, "public", 1, runtime.cpu());
        check(
            runtime.cpu().gpr[2] == 99 && runtime.hle_histogram().size() == 1 && runtime.hle_histogram()[0].second == 3,
            "Bound import cache and histogram must agree");
        std::ostringstream diagnostics;
        auto *original = std::cerr.rdbuf(diagnostics.rdbuf());
        runtime.report_hle_histogram();
        std::cerr.rdbuf(original);
        check(diagnostics.str().find("calls=3") != std::string::npos,
            "Import histogram must report actual invocation count");
        runtime.invoke_import_cached(500, "absent", 2, runtime.cpu());
        check(runtime.stopped() && runtime.stop_reason().find("Missing HLE import") != std::string::npos,
            "Missing import must stop with diagnostic");
    }
    for (const char *mode : {"ordinary", "trace", "deferred"}) {
        Environment trace("PSPRECOMP_TRACE_ON_ERROR", std::string(mode) == "trace" ? "1" : nullptr);
        Environment profile("PSPRECOMP_PROFILE_DISPATCH", std::string(mode) == "deferred" ? "1" : nullptr);
        Environment start("PSPRECOMP_PROFILE_DISPATCH_START", std::string(mode) == "deferred" ? "1" : nullptr);
        Environment count("PSPRECOMP_PROFILE_DISPATCH_COUNT", "2");
        Environment strict("PSPRECOMP_STRICT_PC_PROGRESS", "1");
        std::ostringstream diagnostics;
        auto *original = std::cerr.rdbuf(diagnostics.rdbuf());
        psprecomp::Runtime runtime;
        runtime.register_function(base, next, "public_next");
        runtime.register_function(base + 4, fault, "public_fault");
        bool faulted = false;
        try {
            runtime.run(base, 4);
        } catch (const psprecomp::Error &error) {
            faulted = std::string(error.what()).find("public_fault") != std::string::npos &&
                std::string(error.what()).find("gpr:") != std::string::npos;
        }
        std::cerr.rdbuf(original);
        check(faulted, "Guest exception must preserve function identity and architectural diagnostic");
        psprecomp::Runtime stalled;
        stalled.register_function(base, stay, "public_stall");
        stalled.run(base, 3);
        check(stalled.stopped() && stalled.stop_reason().find("without changing PC") != std::string::npos,
            "Strict PC progress must stop fallthrough");
    }
    {
        Environment profile("PSPRECOMP_PROFILE_DISPATCH", "1");
        Environment start("PSPRECOMP_PROFILE_DISPATCH_START", "1");
        Environment count("PSPRECOMP_PROFILE_DISPATCH_COUNT", "2");
        psprecomp::Runtime runtime;
        runtime.register_function(base, next, "first");
        runtime.register_function(base + 4, next, "second");
        runtime.register_function(base + 8, next, "third");
        runtime.run(base, 10);
        check(runtime.cpu().gpr[2] == 3 && runtime.stop_reason().find("profile window complete") != std::string::npos,
            "Deferred profile window must stop at requested bound");
    }
    heartbeat_hits = 0;
    psprecomp::set_runtime_heartbeat_hook(heartbeat, 1);
    psprecomp::Runtime runtime;
    runtime.register_function(base, next, "first");
    runtime.register_function(base + 4, stop, "last");
    runtime.run(base, 4);
    psprecomp::set_runtime_heartbeat_hook(nullptr, 0);
    check(heartbeat_hits > 0 && runtime.stop_reason() == "completed public dispatch contract",
        "Heartbeat must not alter successful guest dispatch");
}
static std::string shell_quote(const std::filesystem::path &path) {
#ifdef _WIN32
    std::string value = path.string();
    std::string escaped = "\"";
    for (const char c : value) escaped += c == '"' ? "\\\"" : std::string(1, c);
    return escaped + "\"";
#else
    std::string value = path.string();
    std::string escaped = "'";
    for (const char c : value) escaped += c == '\'' ? "'\\''" : std::string(1, c);
    return escaped + "'";
#endif
}

// std::system runs the line through `cmd /c` on Windows, and cmd strips the
// outermost quote pair when the line starts with one.  A build directory such
// as "Nova pasta (4)" then splits at the first space and the tool is not found.
// Wrapping the whole line in one more quote pair is the documented workaround.
static std::string shell_command(const std::string &command) {
#ifdef _WIN32
    return "\"" + command + "\"";
#else
    return command;
#endif
}

void tool_contracts() {
#if defined(PSPRECOMP_ANALYZE_PATH) && !defined(__ANDROID__)
    const auto root = std::filesystem::temp_directory_path() / "psprecomp_public_tool_contract";
    std::filesystem::create_directories(root);
    const auto fixture = root / "synthetic.elf";
    std::vector<std::uint32_t> words;
    for (unsigned function : {0u, 2u, 3u, 4u, 6u, 7u, 0xAu, 0xBu, 0xFu, 0x10u, 0x11u, 0x12u, 0x13u, 0x16u, 0x17u, 0x18u,
             0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x2Au, 0x2Bu,
             0x2Cu, 0x2Du, 0x2Eu, 0x2Fu})
        words.push_back(r(8, 9, 10, 0, function));
    for (unsigned opcode : {9u, 10u, 11u, 12u, 13u, 14u, 15u, 0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x28u,
             0x29u, 0x2Au, 0x2Bu, 0x2Eu, 0x2Fu, 0x31u, 0x39u})
        words.push_back(i(opcode, 8, 9, 4));
    for (unsigned fn : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 12u, 13u, 14u, 15u, 36u})
        words.push_back((0x11u << 26u) | r(16, 9, 8, 10, fn));
    for (unsigned opcode : {0x18u, 0x19u, 0x1Bu})
        for (unsigned operation = 0; operation < 8; ++operation) {
            auto word = (opcode << 26u) | (operation << 23u) | (1u << 16u) | 2u;
            if (psprecomp::decode_allegrex(word).kind != psprecomp::OpcodeKind::Vfpu) words.push_back(word);
        }
    for (unsigned group = 0; group < 22; ++group)
        for (unsigned op = 0; op < 32; ++op) {
            auto word = (0x34u << 26u) | (group << 21u) | (op << 16u) | 2u;
            auto kind = psprecomp::decode_allegrex(word).kind;
            if (kind != psprecomp::OpcodeKind::Vfpu && kind != psprecomp::OpcodeKind::Unsupported)
                words.push_back(word);
        }
    for (unsigned group : {0u, 4u, 16u, 20u, 28u, 29u})
        for (unsigned op : {0u, 3u, 6u, 7u}) {
            auto word = (0x3Cu << 26u) | (group << 21u) | (op << 16u) | 0x8082u;
            if (psprecomp::decode_allegrex(word).kind != psprecomp::OpcodeKind::Vfpu) words.push_back(word);
        }
    // Conditional targets coincide with fallthrough, so every emitted branch
    // and delay-slot shape remains reachable without jumping outside the fixture.
    const auto branch = [&](std::uint32_t word) {
        words.push_back(word);
        words.push_back(i(9, 0, 9, 1));
    };
    for (unsigned opcode : {4u, 5u, 6u, 7u, 0x14u, 0x15u, 0x16u, 0x17u})
        branch(i(opcode, 8, opcode == 6u || opcode == 7u || opcode == 0x16u || opcode == 0x17u ? 0u : 9u, 1));
    for (unsigned operation : {0u, 1u, 2u, 3u, 16u, 17u, 18u, 19u}) branch(i(1, 8, operation, 1));
    for (unsigned opcode : {0x11u, 0x12u})
        for (unsigned operation = 0; operation < 4; ++operation) branch(i(opcode, 8, operation, 1));
    for (unsigned format : {0u, 2u, 4u, 6u}) words.push_back((0x11u << 26u) | r(format, 9, 31, 0, 0));
    words.push_back((0x11u << 26u) | r(20, 0, 8, 10, 32));
    for (unsigned condition = 0; condition < 16; ++condition)
        words.push_back((0x11u << 26u) | r(16, 9, 8, 0, 0x30u + condition));
    for (unsigned format : {3u, 7u}) words.push_back((0x12u << 26u) | r(format, 9, 0, 0, 0));
    words.push_back(r(31, 0, 0, 0, 8));
    words.push_back(0);
    std::vector<std::uint8_t> bytes(84u + words.size() * 4u, 0);
    const auto write = [&](std::size_t offset, std::uint32_t value, unsigned width = 4) {
        for (unsigned index = 0; index < width; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    };
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;
    write(16, 2, 2);
    write(18, 8, 2);
    write(20, 1);
    write(24, base);
    write(28, 52);
    write(40, 52, 2);
    write(42, 32, 2);
    write(44, 1, 2);
    write(46, 40, 2);
    write(52, 1);
    write(56, 84);
    write(60, base);
    write(64, base);
    write(68, static_cast<std::uint32_t>(words.size() * 4));
    write(72, static_cast<std::uint32_t>(words.size() * 4));
    write(76, 5);
    write(80, 16);
    for (unsigned index = 0; index < words.size(); ++index) write(84 + index * 4, words[index]);
    {
        std::ofstream file(fixture, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto invoke = [&](const std::filesystem::path &tool, const std::vector<std::filesystem::path> &arguments,
                            const std::filesystem::path &output) {
        std::string command = shell_quote(tool);
        for (const auto &argument : arguments) command += ' ' + shell_quote(argument);
        command += " > " + shell_quote(output) + " 2>&1";
        return std::system(shell_command(command).c_str());
    };
    const auto text = [](const std::filesystem::path &path) {
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto report = root / "report.json";
    check(invoke(PSPRECOMP_ANALYZE_PATH, {fixture, report, "0x08804000"}, root / "analyze.log") == 0,
        "Analyzer rejected synthetic instruction corpus");
    check(text(report).find("\"entry_runtime\": \"0x08804000\"") != std::string::npos &&
            std::filesystem::exists(root / "report_functions_auto.csv"),
        "Analyzer must publish structured report and automatic functions");
    check(invoke(PSPRECOMP_DUMP_PATH, {fixture, "08804000", "1"}, root / "dump.log") == 0 &&
            text(root / "dump.log").find("0x08804000") != std::string::npos,
        "Disassembler must print requested guest address");
    const auto csv = root / "functions.csv";
    const auto cpp = root / "generated.cpp";
    {
        std::ofstream file(csv);
        file << "name,address,size\npublic_instruction_corpus,0x08804000," << words.size() * 4 << "\n";
    }
    check(invoke(PSPRECOMP_RECOMP_PATH, {fixture, csv, cpp}, root / "recomp.log") == 0,
        "Manual code generation rejected supported synthetic corpus");
    check(text(cpp).find("public_instruction_corpus") != std::string::npos &&
            text(cpp).find("register_function") != std::string::npos,
        "Manual generator must register emitted guest function");
    const auto automatic = root / "automatic";
    check(invoke(PSPRECOMP_RECOMP_PATH, {fixture, "--auto", automatic, "0x08804000", "2048"}, root / "automatic.log") ==
            0,
        "Automatic generation rejected supported synthetic instruction corpus");
    check(std::filesystem::exists(automatic / "generated_registry.cpp"),
        "Automatic generator must emit registration table");
    check(invoke(PSPRECOMP_RECOMP_PATH, {fixture, "--auto", automatic, "0x08804000", "2048"},
              root / "automatic-repeat.log") == 0 &&
            text(root / "automatic-repeat.log").find("rewritten units:   0") != std::string::npos,
        "Repeated automatic generation must preserve unchanged units");
#if defined(PSPRECOMP_TEST_CMAKE_PATH)
    // Compile an object target through CMake so MSVC receives its normal SDK
    // environment too. The fixture never links or executes a game corpus.
    {
        std::ofstream file(root / "CMakeLists.txt");
        file << "cmake_minimum_required(VERSION 3.24)\nproject(public_codegen LANGUAGES CXX)\n"
             << "file(GLOB automatic_sources automatic/*.cpp)\n"
             << "add_library(public_codegen OBJECT generated.cpp ${automatic_sources})\n"
             << "target_compile_features(public_codegen PRIVATE cxx_std_20)\n"
             << "target_include_directories(public_codegen PRIVATE \"${PSPRECOMP_INCLUDE_ROOT}\")\n";
    }
    check(invoke(PSPRECOMP_TEST_CMAKE_PATH,
              {"-S", root, "-B", root / "compile", "-G", PSPRECOMP_TEST_GENERATOR,
                  std::string("-DCMAKE_CXX_COMPILER=") + PSPRECOMP_TEST_COMPILER_PATH,
                  std::string("-DPSPRECOMP_INCLUDE_ROOT=") + PSPRECOMP_TEST_INCLUDE_PATH},
              root / "configure.log") == 0,
        "Public generated-code compile fixture failed to configure");
    if (invoke(PSPRECOMP_TEST_CMAKE_PATH,
            {"--build", root / "compile", "--config", "Debug", "--target", "public_codegen", "-j2"},
            root / "compile.log") != 0) {
        std::cerr << text(root / "compile.log");
        check(false, "Generated supported instruction corpus must compile as valid C++");
    }
#endif
    check(invoke(PSPRECOMP_ANALYZE_PATH, {root / "absent.elf", report}, root / "error.log") != 0 &&
            text(root / "error.log").find("Cannot open PSP executable") != std::string::npos,
        "Analyzer missing-file diagnostic changed");
    check(invoke(PSPRECOMP_RECOMP_PATH, {fixture, root / "absent.csv", cpp}, root / "error.log") != 0,
        "Missing function map must not generate silently");
    std::filesystem::remove_all(root);
#endif
}
void elf_contracts() {
    const auto write = [](std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value,
                           unsigned width = 4u) {
        for (unsigned index = 0; index < width; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8u));
    };
    const auto image = [&] {
        std::vector<std::uint8_t> bytes(320, 0);
        bytes[0] = 0x7F;
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
        bytes[4] = 1;
        bytes[5] = 1;
        bytes[6] = 1;
        write(bytes, 16, 2, 2);
        write(bytes, 18, 8, 2);
        write(bytes, 20, 1);
        write(bytes, 24, base);
        write(bytes, 28, 52);
        write(bytes, 40, 52, 2);
        write(bytes, 42, 32, 2);
        write(bytes, 44, 1, 2);
        write(bytes, 46, 40, 2);
        write(bytes, 52, 1);
        write(bytes, 56, 128);
        write(bytes, 60, base);
        write(bytes, 64, base);
        write(bytes, 68, 16);
        write(bytes, 72, 32);
        write(bytes, 76, 5);
        write(bytes, 80, 16);
        write(bytes, 128, 0x12345678u);
        return bytes;
    };
    const auto rejects = [&](std::vector<std::uint8_t> bytes, const char *diagnostic) {
        try {
            (void)psprecomp::Elf32Image::from_bytes(std::move(bytes), "public malformed fixture");
        } catch (const psprecomp::Error &error) {
            check(std::string(error.what()).find(diagnostic) != std::string::npos, "ELF rejection diagnostic changed");
            return;
        }
        check(false, "Malformed ELF was accepted");
    };
    rejects({}, "Truncated");
    rejects({'~', 'P', 'S', 'P'}, "Encrypted");
    rejects({0, 'P', 'B', 'P'}, "PBP");
    auto bad = image();
    bad[0] = 0;
    rejects(bad, "Not an ELF");
    bad = image();
    bad[4] = 2;
    rejects(bad, "ELF32");
    bad = image();
    write(bad, 18, 3, 2);
    rejects(bad, "MIPS");
    bad = image();
    write(bad, 42, 31, 2);
    rejects(bad, "program header size");
    bad = image();
    write(bad, 46, 39, 2);
    rejects(bad, "section header size");
    bad = image();
    write(bad, 28, 319);
    rejects(bad, "Truncated");
    bad = image();
    write(bad, 56, 320);
    rejects(bad, "Truncated");
    auto bytes = image();
    auto elf = psprecomp::Elf32Image::from_bytes(bytes, "public fixture");
    psprecomp::GuestMemory memory;
    check(elf.runtime_entry() == base && !elf.is_psp_prx() && elf.entry() == base && elf.type() == 2u &&
            elf.source_name() == "public fixture" && elf.bytes() == bytes,
        "Executable identity changed");
    memory.store32(base + 16, 0xFFFFFFFFu);
    elf.load_into(memory);
    check(memory.load32(base) == 0x12345678u && memory.load32(base + 16) == 0,
        "ELF segment must preserve file data and zero BSS");
    check(elf.read_word_at_vaddr(base) == 0x12345678u, "Virtual ELF word read changed");
    bool absent = false;
    try {
        (void)elf.read_word_at_vaddr(base + 64);
    } catch (const psprecomp::Error &) {
        absent = true;
    }
    check(absent, "Unmapped virtual ELF read must fail");
    {
        auto control = image();
        write(control, 68, 64);
        write(control, 72, 64);
        write(control, 128, i(5, 0, 0, 3));
        write(control, 132, i(9, 0, 9, 1));
        write(control, 136, (3u << 26u) | ((base + 48) >> 2u));
        write(control, 140, 0);
        write(control, 144, r(8, 0, 31, 0, 9));
        write(control, 148, 0);
        write(control, 152, 0x0000003Fu);
        write(control, 156, r(31, 0, 0, 0, 8));
        write(control, 160, 0);
        write(control, 176, r(31, 0, 0, 0, 8));
        write(control, 180, 0);
        const auto control_elf = psprecomp::Elf32Image::from_bytes(control);
        control_elf.load_into(memory);
        const auto program = psprecomp::analyze_program(control_elf, memory, 0);
        const auto function = std::find_if(program.functions.begin(), program.functions.end(),
            [](const auto &analysis) { return analysis.entry == base; });
        check(function != program.functions.end() && function->direct_calls.contains(base + 48) &&
                function->indirect_call_sites.contains(base + 16) && function->unsupported_instruction_count == 1 &&
                !function->labels.contains(base + 4) && !function->labels.contains(base + 12) &&
                function->entry_labels.contains(base + 16) && !function->truncated,
            "Program analysis must preserve branch continuations/call sites and exclude delay slots");
        const auto limited = psprecomp::analyze_program(control_elf, memory, 0, 2);
        check(limited.functions.front().truncated && limited.functions.front().labels.size() == 2,
            "Instruction budget must bound analysis and mark truncation");
        check(psprecomp::is_executable_address(program.executable_ranges, base) &&
                !psprecomp::is_executable_address(program.executable_ranges, base + 1) &&
                !psprecomp::is_executable_address(program.executable_ranges, base + 64) &&
                !psprecomp::is_executable_address({}, base),
            "Executable ranges must be aligned and half-open");
        control.resize(384);
        write(control, 68, 80);
        write(control, 72, 80);
        write(control, 32, 208);
        write(control, 48, 3, 2);
        // A loaded RWX segment contains distinct executable and data sections.
        // Only .text is executable; two data pointers identify function-shaped
        // entries, while a coincidental mid-function pointer is ignored.
        for (const auto &[header, address, offset, size, flags] : {std::array<std::uint32_t, 5>{248, base, 128, 64, 6},
                 std::array<std::uint32_t, 5>{288, base + 64, 192, 16, 2}}) {
            write(control, header + 4, 1);
            write(control, header + 8, flags);
            write(control, header + 12, address);
            write(control, header + 16, offset);
            write(control, header + 20, size);
        }
        write(control, 160, r(31, 0, 0, 0, 8));
        write(control, 168, r(31, 0, 0, 0, 8));
        write(control, 176, i(9, 29, 29, 0xFFF0));
        write(control, 180, r(31, 0, 0, 0, 8));
        write(control, 192, base + 40);
        write(control, 196, base + 48);
        write(control, 200, base + 20);
        const auto section_elf = psprecomp::Elf32Image::from_bytes(control);
        section_elf.load_into(memory);
        const auto sections = psprecomp::analyze_program(section_elf, memory, 0);
        check(sections.executable_ranges.size() == 1 && sections.executable_ranges[0].end == base + 64 &&
                sections.seeds.at(base + 40) == "data_section_code_pointer" && !sections.seeds.contains(base + 20),
            "Data pointers must discover function shapes without treating data or mid-function constants as code");
        write(control, 128, i(15, 0, 8, 0x0880));
        write(control, 132, i(13, 8, 8, 0x4030));
        write(control, 136, r(8, 0, 0, 0, 8));
        const auto pointer_elf = psprecomp::Elf32Image::from_bytes(control);
        pointer_elf.load_into(memory);
        const auto pointers = psprecomp::analyze_program(pointer_elf, memory, 0);
        check(pointers.seeds.at(base + 48) == "materialized_code_pointer",
            "An address assembled by LUI/ORI must discover its executable function entry");
    }
    bad = image();
    write(bad, 72, 8);
    auto short_segment = psprecomp::Elf32Image::from_bytes(bad);
    absent = false;
    try {
        short_segment.load_into(memory);
    } catch (const psprecomp::Error &) {
        absent = true;
    }
    check(absent, "Segment cannot have file data larger than memory");
    bad = image();
    write(bad, 52, 0);
    auto no_load = psprecomp::Elf32Image::from_bytes(bad);
    absent = false;
    try {
        no_load.load_into(memory);
    } catch (const psprecomp::Error &) {
        absent = true;
    }
    check(absent, "Executable without PT_LOAD must fail");
    bytes = image();
    write(bytes, 16, psprecomp::kElfTypePspPrx, 2);
    write(bytes, 24, 4);
    write(bytes, 60, 0);
    write(bytes, 64, 0);
    write(bytes, 32, 192);
    write(bytes, 48, 1, 2);
    write(bytes, 196, psprecomp::kSectionTypePspRel);
    write(bytes, 208, 256);
    write(bytes, 212, 40);
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> rels{
        {{0, 0x102u}, {0, 0x10002u}, {1, 4u}, {0, 15u}, {0, 5u}}};
    for (unsigned index = 0; index < rels.size(); ++index) {
        write(bytes, 256 + index * 8, rels[index].first);
        write(bytes, 260 + index * 8, rels[index].second);
    }
    auto prx = psprecomp::Elf32Image::from_bytes(bytes);
    check(prx.is_psp_prx() && prx.runtime_entry() == base + 4 && prx.segment_runtime_address(0) == base,
        "PRX addresses must relocate against load base");
    auto stats = prx.load_and_relocate(memory);
    check(stats.total == 5u && stats.invalid == 4u && stats.unsupported == 1u,
        "Malformed relocation records must be counted without patching memory");
    check(memory.load32(base) == 0x12345678u, "Invalid relocations must preserve original instruction");
    check(prx.section_runtime_address(prx.sections()[0]) == base, "PRX section address must relocate");
    (void)prx.relocation_sites();
    write(bytes, 212, 39);
    auto malformed_relocations = psprecomp::Elf32Image::from_bytes(bytes);
    absent = false;
    try {
        (void)malformed_relocations.apply_relocations(memory);
    } catch (const psprecomp::Error &) {
        absent = true;
    }
    check(absent, "Mis-sized relocation section must be rejected");
}
void integer_contracts() {
    psprecomp::Runtime runtime;
    psprecomp::AllegrexContext cpu{};
    struct Case {
        std::uint32_t word;
        std::uint32_t left;
        std::uint32_t right;
        std::uint32_t expected;
        unsigned destination;
    };
    const Case cases[] = {{r(8, 9, 10, 0, 0x20), 17, 3, 20, 10}, {r(8, 9, 10, 0, 0x21), 0xFFFFFFFF, 1, 0, 10},
        {r(8, 9, 10, 0, 0x22), 17, 3, 14, 10}, {r(8, 9, 10, 0, 0x23), 0, 1, 0xFFFFFFFF, 10},
        {r(8, 9, 10, 0, 0x24), 0xF0, 0x3C, 0x30, 10}, {r(8, 9, 10, 0, 0x25), 0xF0, 0x3C, 0xFC, 10},
        {r(8, 9, 10, 0, 0x26), 0xF0, 0x3C, 0xCC, 10}, {r(8, 9, 10, 0, 0x27), 0xF0, 0x3C, 0xFFFFFF03, 10},
        {r(8, 9, 10, 0, 0x2A), 0xFFFFFFFF, 1, 1, 10}, {r(8, 9, 10, 0, 0x2B), 0xFFFFFFFF, 1, 0, 10},
        {r(8, 9, 10, 0, 0x2C), 0xFFFFFFFF, 1, 1, 10}, {r(8, 9, 10, 0, 0x2D), 0xFFFFFFFF, 1, 0xFFFFFFFF, 10},
        {r(8, 9, 10, 0, 0x0A), 123, 0, 123, 10}, {r(8, 9, 10, 0, 0x0B), 123, 1, 123, 10},
        {r(0, 9, 10, 4, 0), 0, 0x123, 0x1230, 10}, {r(0, 9, 10, 4, 2), 0, 0x123, 0x12, 10},
        {r(0, 9, 10, 4, 3), 0, 0x80000000, 0xF8000000, 10}, {r(1, 9, 10, 4, 2), 0, 0x12345678, 0x81234567, 10},
        {r(8, 9, 10, 0, 4), 36, 0x123, 0x1230, 10}, {r(8, 9, 10, 0, 6), 36, 0x123, 0x12, 10},
        {r(8, 9, 10, 0, 7), 36, 0x80000000, 0xF8000000, 10}, {r(8, 9, 10, 1, 6), 36, 0x12345678, 0x81234567, 10},
        {r(8, 0, 10, 0, 0x16), 0x00FFFFFF, 0, 8, 10}, {r(8, 0, 10, 0, 0x17), 0xFF000000, 0, 8, 10},
        {i(9, 8, 10, 0xFFFF), 123, 0, 122, 10}, {i(10, 8, 10, 0), 0xFFFFFFFF, 0, 1, 10},
        {i(11, 8, 10, 0xFFFF), 1, 0, 1, 10}, {i(12, 8, 10, 0xFF), 0x1234, 0, 0x34, 10},
        {i(13, 8, 10, 0xFF), 0x1234, 0, 0x12FF, 10}, {i(14, 8, 10, 0xFF), 0x1234, 0, 0x12CB, 10},
        {i(15, 0, 10, 0x1234), 0, 0, 0x12340000, 10}, {(0x1Fu << 26) | r(8, 9, 7, 8, 0), 0x12345678, 0, 0x56, 9},
        {(0x1Fu << 26) | r(8, 9, 15, 8, 4), 0xAB, 0x12345678, 0x1234AB78, 9},
        {(0x1Fu << 26) | r(0, 9, 10, 16, 0x20), 0, 0x80, 0xFFFFFF80, 10},
        {(0x1Fu << 26) | r(0, 9, 10, 24, 0x20), 0, 0x8000, 0xFFFF8000, 10},
        {(0x1Fu << 26) | r(0, 9, 10, 20, 0x20), 0, 1, 0x80000000, 10},
        {(0x1Fu << 26) | r(0, 9, 10, 2, 0x20), 0, 0x12345678, 0x34127856, 10},
        {(0x1Fu << 26) | r(0, 9, 10, 3, 0x20), 0, 0x12345678, 0x78563412, 10}};
    for (const auto &test : cases) {
        cpu.gpr[8] = test.left;
        cpu.gpr[9] = test.right;
        cpu.gpr[10] = 0;
        step(runtime, cpu, test.word);
        if (cpu.gpr[test.destination] != test.expected) {
            std::cerr << "word=" << std::hex << test.word << " actual=" << cpu.gpr[test.destination]
                      << " expected=" << test.expected << '\n';
            check(false, "Integer instruction disagrees with architectural result");
        }
    }
    for (unsigned function : {0x0Au, 0x0Bu}) {
        cpu.gpr[8] = 123;
        cpu.gpr[9] = function == 0x0Au ? 1u : 0u;
        cpu.gpr[10] = 77;
        step(runtime, cpu, r(8, 9, 10, 0, function));
        check(cpu.gpr[10] == 77, "Conditional move must preserve destination when condition is false");
    }
    for (unsigned function : {0x20u, 0x22u}) {
        psprecomp::Runtime overflow;
        psprecomp::AllegrexContext state{};
        state.gpr[8] = 0x7FFFFFFFu;
        state.gpr[9] = function == 0x20u ? 1u : 0xFFFFFFFFu;
        overflow.memory().store32(base, r(8, 9, 10, 0, function));
        state.pc = base;
        check(psprecomp::interpret_allegrex(overflow, state, 1) == psprecomp::InterpreterExit::Stopped &&
                overflow.stop_reason().find("overflow") != std::string::npos,
            "Signed overflow must stop with diagnostic");
    }
    for (unsigned function : {0x1Au, 0x1Bu}) {
        for (auto pair :
            {std::pair{17u, 5u}, std::pair{0x80000000u, 0xFFFFFFFFu}, std::pair{17u, 0u}, std::pair{0xFFFFFFEFu, 0u}}) {
            cpu.gpr[8] = pair.first;
            cpu.gpr[9] = pair.second;
            step(runtime, cpu, r(8, 9, 0, 0, function));
            if (pair.second == 0)
                check(cpu.hi == pair.first &&
                        cpu.lo == (function == 0x1Bu || static_cast<std::int32_t>(pair.first) >= 0 ? 0xFFFFFFFFu : 1u),
                    "Divide zero result changed");
            else if (function == 0x1Au && pair.first == 0x80000000u)
                check(cpu.lo == 0x80000000u && cpu.hi == 0, "Signed division overflow result changed");
            else
                check(cpu.lo == pair.first / pair.second && cpu.hi == pair.first % pair.second,
                    "Division quotient or remainder changed");
        }
    }
    cpu.gpr[8] = 123;
    step(runtime, cpu, r(8, 0, 0, 0, 0x11));
    check(cpu.hi == 123, "MTHI failed");
    step(runtime, cpu, r(8, 0, 0, 0, 0x13));
    check(cpu.lo == 123, "MTLO failed");
    for (unsigned function : {0x1Cu, 0x1Du, 0x2Eu, 0x2Fu}) {
        cpu.gpr[8] = 3;
        cpu.gpr[9] = 5;
        cpu.hi = 0;
        cpu.lo = 20;
        step(runtime, cpu, r(8, 9, 0, 0, function));
        check(cpu.lo == (function == 0x1Cu || function == 0x1Du ? 35u : 5u) && cpu.hi == 0,
            "Multiply accumulator result changed");
    }
}
void memory_contracts() {
    psprecomp::Runtime runtime;
    psprecomp::AllegrexContext cpu{};
    cpu.gpr[8] = scratch;
    runtime.memory().store32(scratch, 0x87654380u);
    for (auto entry : {std::pair{0x20u, 0xFFFFFF80u}, std::pair{0x24u, 0x80u}, std::pair{0x21u, 0x4380u},
             std::pair{0x25u, 0x4380u}, std::pair{0x23u, 0x87654380u}}) {
        step(runtime, cpu, i(entry.first, 8, 9, 0));
        check(cpu.gpr[9] == entry.second, "Signed/unsigned guest load changed");
    }
    cpu.gpr[9] = 0xFEDCBA98;
    for (auto entry : {std::pair{0x28u, 0x98u}, std::pair{0x29u, 0xBA98u}, std::pair{0x2Bu, 0xFEDCBA98u}}) {
        runtime.memory().store32(scratch, 0);
        step(runtime, cpu, i(entry.first, 8, 9, 0));
        check(runtime.memory().load32(scratch) == entry.second, "Guest store wrote wrong width");
    }
    cpu.fpr[9] = 1.5f;
    step(runtime, cpu, i(0x39u, 8, 9, 0));
    cpu.fpr[9] = 0;
    step(runtime, cpu, i(0x31u, 8, 9, 0));
    check(cpu.fpr[9] == 1.5f, "COP1 memory transfer changed bits");
    step(runtime, cpu, i(0x2Fu, 8, 0, 0));
    step(runtime, cpu, r(0, 0, 0, 0, 0xFu));
}
void vector_contracts() {
    psprecomp::Runtime runtime;
    psprecomp::AllegrexContext cpu{};
    const auto vector = [](unsigned opcode, unsigned operation, unsigned destination = 2u) {
        return (opcode << 26u) | (operation << 23u) | (1u << 16u) | (0u << 8u) | destination;
    };
    const auto scalar = [](unsigned group, unsigned operation, unsigned destination = 2u) {
        return (0x34u << 26u) | (group << 21u) | (operation << 16u) | destination;
    };
    const auto source = [&](float left, float right) {
        cpu.eat_vfpu_prefixes();
        cpu.set_vfpu_scalar_bits(0u, std::bit_cast<std::uint32_t>(left));
        cpu.set_vfpu_scalar_bits(1u, std::bit_cast<std::uint32_t>(right));
    };
    const auto result = [&] { return std::bit_cast<float>(cpu.vfpu_scalar_bits(2u)); };
    struct Arithmetic {
        unsigned opcode;
        unsigned operation;
        float expected;
    };
    for (auto test : {Arithmetic{0x18, 0, 8}, Arithmetic{0x18, 1, 4}, Arithmetic{0x18, 7, 3}, Arithmetic{0x19, 0, 12},
             Arithmetic{0x19, 1, 12}, Arithmetic{0x19, 2, 12}, Arithmetic{0x19, 4, 2}, Arithmetic{0x1B, 2, 2},
             Arithmetic{0x1B, 3, 6}, Arithmetic{0x1B, 5, 1}, Arithmetic{0x1B, 6, 1}, Arithmetic{0x1B, 7, 0}}) {
        source(6, 2);
        step(runtime, cpu, vector(test.opcode, test.operation));
        check(result() == test.expected, "VFPU scalar arithmetic disagrees with known operands");
    }
    struct Unary {
        unsigned operation;
        float input;
        float expected;
    };
    for (auto test : {Unary{0, -2, -2}, Unary{1, -2, 2}, Unary{2, -2, 2}, Unary{4, -2, 0}, Unary{4, 2, 1},
             Unary{4, 0.5f, 0.5f}, Unary{5, -2, -1}, Unary{5, 2, 1}, Unary{5, 0.5f, 0.5f}, Unary{16, 2, 0.5f},
             Unary{17, 4, 0.5f}, Unary{18, 1, 1}, Unary{19, 0, 1}, Unary{20, 3, 8}, Unary{21, 8, 3}, Unary{22, 4, 2},
             Unary{23, 1, 1}, Unary{24, 2, -0.5f}, Unary{26, 1, -1}, Unary{28, 3, 0.125f}}) {
        source(test.input, 0);
        step(runtime, cpu, scalar(0, test.operation));
        check(std::abs(result() - test.expected) < 0.00001f, "VFPU unary known value changed");
    }
    for (unsigned op : {6u, 7u}) {
        source(123, 0);
        step(runtime, cpu, scalar(0, op));
        check(result() == (op == 6 ? 0 : 1), "VFPU fill changed");
    }
    source(0.25f, 0);
    step(runtime, cpu, scalar(2, 4));
    check(result() == 0.75f, "VFPU one complement changed");
    for (float value : {-2.0f, 0.0f, 2.0f}) {
        source(value, 0);
        step(runtime, cpu, scalar(2, 10));
        check(result() == value / 2.0f, "VFPU sign changed");
    }
    for (unsigned op : {6u, 7u}) {
        source(3, 0);
        step(runtime, cpu, scalar(2, op));
        check(result() == (op == 6u ? 3.0f : 0.0f), "VFPU scalar horizontal sum/average hardware weighting changed");
    }
    for (unsigned mode : {16u, 17u, 18u, 19u}) {
        for (float input : {3.75f, std::numeric_limits<float>::quiet_NaN(), 1.0e30f, -1.0e30f}) {
            source(input, 0);
            step(runtime, cpu, scalar(mode, 0));
            auto bits = cpu.vfpu_scalar_bits(2);
            check(bits ==
                    (std::isnan(input) || input > 1.0e20f ? 0x7FFFFFFFu
                            : input < -1.0e20f            ? 0x80000000u
                            : mode == 17u || mode == 19u  ? 3u
                                                          : 4u),
                "VFPU integer conversion rounding/saturation changed");
        }
    }
    cpu.eat_vfpu_prefixes();
    cpu.set_vfpu_scalar_bits(0, 6);
    step(runtime, cpu, scalar(20, 1));
    check(result() == 3, "VFPU scaled integer conversion changed");
    step(runtime, cpu, (0x37u << 26u) | (6u << 23u) | (2u << 16u) | 0xFFFDu);
    check(result() == -3, "VIIM sign extension changed");
    for (auto pair : {std::pair{0x3C00u, 1.0f}, std::pair{0x8000u, -0.0f}, std::pair{0x0001u, std::ldexp(1.0f, -24)}}) {
        cpu.eat_vfpu_prefixes();
        step(runtime, cpu, (0x37u << 26u) | (7u << 23u) | (2u << 16u) | pair.first);
        check(std::bit_cast<std::uint32_t>(result()) == std::bit_cast<std::uint32_t>(pair.second),
            "VFIM half expansion changed");
    }
    source(0, 0);
    step(runtime, cpu, scalar(3, 8));
    check(std::abs(result() - 1.5707963f) < 0.00001f, "VFPU hardware pi/2 constant changed");
    cpu.gpr[8] = scratch;
    runtime.memory().store32(scratch, 0x3FC00000);
    step(runtime, cpu, i(0x32, 8, 2, 0));
    check(result() == 1.5f, "LV.S did not load bits");
    step(runtime, cpu, i(0x3A, 8, 2, 4));
    check(runtime.memory().load32(scratch + 4) == 0x3FC00000u, "SV.S did not store bits");
    for (unsigned lane = 0; lane < 4; ++lane)
        runtime.memory().store32(scratch + lane * 4, std::bit_cast<std::uint32_t>(static_cast<float>(lane + 1)));
    step(runtime, cpu, i(0x36, 8, 0, 0));
    step(runtime, cpu, i(0x3E, 8, 0, 16));
    for (unsigned lane = 0; lane < 4; ++lane)
        check(runtime.memory().load32(scratch + 16 + lane * 4) == runtime.memory().load32(scratch + lane * 4),
            "Quad transfer changed lane ordering");
    for (unsigned control = 0; control < 3; ++control) {
        step(runtime, cpu, (0x37u << 26u) | (control << 24u) | 0xFFFFFu);
        check(cpu.vfpu_ctrl[control] == (control == 2u ? 0xFFFu : 0xFFFFFu), "VFPU prefix control mask changed");
    }
    cpu.eat_vfpu_prefixes();
    for (unsigned operation : {3u, 6u, 7u}) {
        step(runtime, cpu, (0x3Cu << 26u) | (28u << 21u) | (operation << 16u) | 0x8080u | 4u);
        float matrix[16]{};
        cpu.read_vfpu_matrix(matrix, 4u, 4u);
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row)
                check(matrix[column * 4 + row] ==
                        (operation == 3u          ? (row == column ? 1.0f : 0.0f)
                                : operation == 7u ? 1.0f
                                                  : 0.0f),
                    "VFPU matrix initialization disagrees with identity/zero/one contract");
    }
    cpu.eat_vfpu_prefixes();
    step(runtime, cpu, (0x37u << 26u) | (2u << 24u) | 0xFFFu);
    step(runtime, cpu, 0xFFFF0000u);
    check(cpu.vfpu_ctrl[2] == 0xFFFu, "VFLUSH special encoding must preserve prefixes");
    step(runtime, cpu, 0xFC000000u);
    check(cpu.vfpu_ctrl[2] == 0u, "VFLUSH must consume ordinary prefixes");
}
void format_contracts() {
    {
        const auto root = std::filesystem::temp_directory_path() / "psprecomp_public_nid_contract";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const auto csv = root / std::filesystem::path(u8"nids-\u00e9.csv");
        psprecomp::NidRegistry registry;
        registry.add("Public", 1, "original");
        registry.add("Public", 1, "replacement");
        check(registry.resolve("Public", 1) == "replacement" && !registry.resolve("Absent", 1),
            "NID definitions must replace matching keys and preserve library identity");
        const auto write_csv = [&](const char *contents) {
            std::ofstream file(csv);
            file << contents;
        };
        write_csv("# comment\n\nPublic,0X00000002,two\nPublic,00000003,three\n");
        registry.load_csv(csv);
        check(registry.resolve("Public", 2) == "two" && registry.resolve("Public", 3) == "three",
            "NID CSV must accept hexadecimal prefixes and ignore comments/empty lines");
        const auto symbols = registry.all();
        check(std::is_sorted(symbols.begin(), symbols.end(),
                  [](const auto &a, const auto &b) {
                      return a.library < b.library || (a.library == b.library && a.nid < b.nid);
                  }),
            "NID enumeration must be deterministic by library and identifier");
        for (const auto &[contents, diagnostic] : {std::pair{"broken\n", "Invalid NID CSV line 1"},
                 std::pair{"# comment\nPublic,not_hex,name\n", "Invalid NID at CSV line 2"},
                 std::pair{"Public,0x123junk,name\n", "Invalid NID at CSV line 1"},
                 std::pair{"Public,100000000,name\n", "Invalid NID at CSV line 1"}}) {
            write_csv(contents);
            bool rejected = false;
            try {
                registry.load_csv(csv);
            } catch (const psprecomp::Error &error) {
                rejected = std::string(error.what()).find(diagnostic) != std::string::npos;
            }
            check(rejected, "Malformed NID CSV must identify its failing line");
        }
        std::filesystem::remove(csv);
        bool absent = false;
        try {
            registry.load_csv(csv);
        } catch (const psprecomp::Error &error) {
            absent = std::string(error.what()).find("Cannot open NID CSV") != std::string::npos;
        }
        check(absent, "Missing Unicode CSV path must retain the intended diagnostic");
        std::filesystem::remove_all(root);
    }
    {
        psprecomp::GuestMemory memory;
        const auto decode = [&](std::span<const std::uint8_t> input, unsigned capacity) {
            memory.zero(base, 256);
            memory.zero(scratch, 5000);
            memory.copy_in(base, input);
            return psprecomp::inflate_raw_deflate(memory, scratch, capacity, base);
        };
        const std::array<std::uint8_t, 12> stored{0, 1, 0, 0xFE, 0xFF, 'A', 1, 1, 0, 0xFE, 0xFF, 'B'};
        auto result = decode(stored, 2);
        check(result.status == psprecomp::RawDeflateStatus::Ok && result.output_size == 2 &&
                result.input_consumed == stored.size() && memory.load16(scratch) == 0x4241,
            "Stored blocks must concatenate and terminate exactly at BFINAL");
        const std::array<std::uint8_t, 5> empty{1, 0, 0, 0xFF, 0xFF};
        result = decode(empty, 0);
        check(result.status == psprecomp::RawDeflateStatus::Ok && result.output_size == 0 && result.input_consumed == 5,
            "Empty stored block must fit a zero-capacity output");
        result = decode(stored, 0);
        check(result.status == psprecomp::RawDeflateStatus::OutputOverflow && result.output_size == 0,
            "Stored-block overflow must not write a partial block");
        for (const auto &invalid : {std::vector<std::uint8_t>{7}, std::vector<std::uint8_t>{1, 1, 0, 0, 0},
                 std::vector<std::uint8_t>{0xFD, 0}})
            check(decode(invalid, 5000).status == psprecomp::RawDeflateStatus::InvalidData,
                "Reserved blocks, inconsistent stored lengths and excessive HLIT must be rejected");
        check(psprecomp::inflate_raw_deflate(memory, scratch, 1, 0).status == psprecomp::RawDeflateStatus::InvalidData,
            "Unmapped compressed input must return InvalidData");
        const auto end = psprecomp::GuestMemory::kPhysicalBase + memory.size() - 1;
        memory.store8(end, 1);
        check(
            psprecomp::inflate_raw_deflate(memory, scratch, 1, end).status == psprecomp::RawDeflateStatus::InvalidData,
            "Truncated stored header must return InvalidData at the RAM boundary");
        // Independently generated with Python zlib.compressobj(wbits=-15) for
        // exactly 4096 'a' bytes; dynamic Huffman framing is RFC 1951.
        const std::array<std::uint8_t, 22> dynamic{
            0xED, 0xC1, 1, 0x0D, 0, 0, 0, 0xC2, 0xA0, 0xAC, 0xEF, 0x5F, 0xC2, 0x1E, 0x0E, 0x28, 0, 0, 0, 0xE0, 0xDD, 0};
        result = decode(dynamic, 4096);
        std::vector<std::uint8_t> actual(4096);
        memory.copy_out(scratch, actual);
        check(result.status == psprecomp::RawDeflateStatus::Ok && result.output_size == 4096 &&
                result.input_consumed == dynamic.size() &&
                std::all_of(actual.begin(), actual.end(), [](auto byte) { return byte == 'a'; }),
            "Dynamic Huffman repeat codes and overlapping matches must reconstruct exact independent plaintext");
        check(decode(dynamic, 4).status == psprecomp::RawDeflateStatus::OutputOverflow,
            "Dynamic match expansion must respect output capacity");
    }
}
void diagnostic_contracts() {
    const auto captured_run = [](psprecomp::Runtime &runtime, std::uint32_t address) {
        std::ostringstream diagnostics;
        auto *original = std::cerr.rdbuf(diagnostics.rdbuf());
        try {
            runtime.run(address, 2);
        } catch (...) {
            std::cerr.rdbuf(original);
            throw;
        }
        std::cerr.rdbuf(original);
        return diagnostics.str();
    };
    {
        Environment trace("PSPRECOMP_TRACE_PC", "0x08804000");
        Environment words("PSPRECOMP_TRACE_MEM", "1");
        Environment dispatch("PSPRECOMP_TRACE", "1");
        psprecomp::Runtime runtime;
        runtime.register_function(base, next, "public_traced");
        runtime.register_function(base + 4, stop, "public_stop");
        runtime.cpu().gpr[4] = scratch;
        runtime.cpu().gpr[5] = scratch + 0x40;
        runtime.memory().copy_in(scratch, std::array<std::uint8_t, 4>{'a', 'b', 'c', 0});
        runtime.memory().copy_in(scratch + 0x40, std::array<std::uint8_t, 4>{'d', 'e', 'f', 0});
        auto output = captured_run(runtime, base);
        check(output.find("[trace-pc] 0x08804000 public_traced") != std::string::npos &&
                output.find("a0str=\"abc\"") != std::string::npos &&
                output.find("a1str=\"def\"") != std::string::npos &&
                output.find("a0mem=0x00636261") != std::string::npos &&
                output.find("a1mem=0x00666564") != std::string::npos &&
                output.find("[dispatch]") != std::string::npos && runtime.cpu().gpr[2] == 1,
            "PC diagnostics must report bounded strings/words without changing execution");
        runtime.memory().copy_in(scratch, std::vector<std::uint8_t>(256, 'x'));
        runtime.cpu().gpr[5] = 0;
        output = captured_run(runtime, base);
        check(runtime.cpu().gpr[2] == 2 && output.find("public_traced") != std::string::npos,
            "Unterminated diagnostic strings must not abort guest execution");
    }
    {
        // These are existing diagnostic trigger addresses, with independent
        // zero-filled RAM records rather than any game's heap or file contents.
        Environment heap("PSPRECOMP_HEAP_DIAG", "1");
        psprecomp::Runtime runtime;
        constexpr std::uint32_t callback = 0x089345B0;
        constexpr std::uint32_t manager = 0x08BC6500;
        runtime.memory().store32(manager + 8, scratch);
        runtime.memory().store32(scratch, 16);
        runtime.memory().store32(scratch + 12, 1);
        runtime.register_function(callback, stop, "synthetic_heap_callback");
        const auto output = captured_run(runtime, callback);
        check(output.find("[heapdiag-free] index=0") != std::string::npos &&
                output.find("size=0x00000010") != std::string::npos &&
                output.find("invalid=0x00000001") != std::string::npos && runtime.stopped(),
            "Heap diagnostic must report valid blocks and stop traversal at an invalid link");
    }
    {
        Environment files("PSPRECOMP_FILE_OBJECT_DIAG", "1");
        Environment stop_null("PSPRECOMP_FILE_OBJECT_STOP_ON_NULL", "1");
        psprecomp::Runtime runtime;
        constexpr std::uint32_t null_seek = 0x08938F7C;
        runtime.register_function(null_seek, next, "synthetic_null_seek");
        auto output = captured_run(runtime, null_seek);
        check(runtime.stopped() && runtime.cpu().gpr[2] == 0 &&
                runtime.stop_reason().find("before null seek") != std::string::npos &&
                output.find("[fileobj]") != std::string::npos,
            "Null-file diagnostic stop must precede the guest callback");
        runtime.cpu().gpr[4] = scratch;
        runtime.memory().store32(scratch, 42);
        runtime.register_function(null_seek + 4, stop, "synthetic_stop");
        output = captured_run(runtime, null_seek);
        check(runtime.cpu().gpr[2] == 1 && runtime.stop_reason().find("completed") != std::string::npos,
            "A valid file object must not trigger the null-file stop");
        constexpr std::uint32_t file_open = 0x08938F04;
        runtime.register_function(file_open, stop, "synthetic_file_open");
        runtime.memory().copy_in(scratch, std::array<std::uint8_t, 5>{'f', 'i', 'l', 'e', 0});
        output = captured_run(runtime, file_open);
        check(output.find("path=\"file\"") != std::string::npos,
            "File diagnostic must report a valid bounded guest path");
    }
    {
        Environment world("PSPRECOMP_WORLD_STREAM_DIAG", "1");
        Environment stop_callback("PSPRECOMP_WORLD_STREAM_STOP_AT_CALLBACK", "1");
        Environment request("PSPRECOMP_REQUEST_ALLOC_DIAG", "1");
        psprecomp::Runtime runtime;
        constexpr std::uint32_t callback = 0x089563C0;
        runtime.cpu().gpr[4] = scratch;
        runtime.cpu().gpr[5] = scratch + 0x200;
        runtime.register_function(callback, next, "synthetic_world_callback");
        const auto output = captured_run(runtime, callback);
        check(runtime.cpu().gpr[2] == 0 && runtime.stopped() &&
                runtime.stop_reason().find("stop at callback") != std::string::npos &&
                output.find("[worlddiag]") != std::string::npos,
            "World-stream stop must preserve CPU state before the guest callback");
        for (auto address : {0x08939590u, 0x089395D4u, 0x0893961Cu, 0x089396D8u, 0x089397CCu, 0x08956258u}) {
            runtime.cpu().gpr[16] = scratch;
            runtime.cpu().gpr[18] = scratch + 0x200;
            runtime.cpu().gpr[2] = scratch + 0x200;
            runtime.memory().store32(scratch + 0x200 + 8, 123);
            runtime.register_function(address, stop, "synthetic_allocation");
            const auto allocation = captured_run(runtime, address);
            check(allocation.find("[reqalloc]") != std::string::npos && runtime.stopped(),
                "Request diagnostics must report guest fields without requiring a real allocator");
            if (address == 0x089396D8u || address == 0x089397CCu || address == 0x08956258u)
                check(allocation.find("size=123") != std::string::npos,
                    "Request diagnostic must preserve the actual requested size");
        }
    }
}
void guest_memory_contracts(bool armed) {
    using Memory = psprecomp::GuestMemory;
    bool invalid_size = false;
    try {
        Memory invalid(1);
    } catch (const psprecomp::Error &) {
        invalid_size = true;
    }
    check(invalid_size, "Unsupported PSP RAM size must be rejected");
    Memory memory;
    check(memory.size() == 32u * 1024u * 1024u && memory.vram_size() == 2u * 1024u * 1024u,
        "RAM and EDRAM physical sizes changed");
    const Memory &read_only = memory;
    const auto rejects = [&](auto operation) {
        bool rejected = false;
        try {
            operation();
        } catch (const psprecomp::Error &) {
            rejected = true;
        }
        check(rejected, "Invalid guest memory range must raise an error");
    };
    const auto vram = Memory::kVramPhysicalBase;
    const auto wrap = vram + Memory::kVramSize - 1;
    for (auto address : {base, vram, wrap}) {
        memory.aot_store8(address, 0x12);
        check(memory.aot_load8(address) == 0x12, "AOT byte store/load must preserve bits");
        memory.aot_store16(address, 0x3456);
        check(memory.aot_load16(address) == 0x3456, "AOT halfword store/load must preserve EDRAM wrapping");
        memory.aot_store32(address, 0x12345678);
        check(memory.aot_load32(address) == 0x12345678, "AOT word store/load must preserve EDRAM wrapping");
    }
    check(read_only.raw_pointer(wrap, 4) == nullptr && memory.raw_pointer(0, 1) == nullptr,
        "Guest-contiguous mirrored EDRAM is not host-contiguous");
    const std::array<std::uint8_t, 6> crossing{1, 2, 3, 4, 5, 6};
    memory.copy_in(wrap - 2, crossing);
    std::array<std::uint8_t, 6> copied{};
    memory.copy_out(wrap - 2, copied);
    check(copied == crossing, "Bulk guest copies must span EDRAM mirror boundaries");
    memory.zero(wrap - 2, copied.size());
    memory.copy_out(wrap - 2, copied);
    check(copied == std::array<std::uint8_t, 6>{}, "Zeroing must span EDRAM mirror boundaries");
    rejects([&] { memory.copy_in(0, crossing); });
    rejects([&] { memory.copy_out(0, copied); });
    rejects([&] { memory.zero(0, 1); });
    for (auto address : {0u, base + memory.size() - 1, vram + Memory::kVramAddressSpan - 1}) {
        rejects([&] { static_cast<void>(memory.aot_load32(address)); });
        rejects([&] { memory.aot_store32(address, 1); });
    }
    rejects([&] { static_cast<void>(memory.aot_load8(0)); });
    rejects([&] { static_cast<void>(memory.aot_load16(0)); });
    rejects([&] { memory.aot_store8(0, 1); });
    rejects([&] { memory.aot_store16(0, 1); });
    memory.copy_in(base, std::array<std::uint8_t, 4>{'t', 'e', 's', 't'});
    rejects([&] { static_cast<void>(memory.read_c_string(base, 4)); });
    memory.store8(base + 4, 0);
    check(memory.read_c_string(base, 5) == "test", "Guest string must stop at its bounded terminator");
    for (unsigned offset = 0; offset < 4; ++offset) {
        constexpr std::array<std::uint32_t, 4> left{0x78BBCCDD, 0x5678CCDD, 0x345678DD, 0x12345678};
        constexpr std::array<std::uint32_t, 4> right{0x12345678, 0xAA123456, 0xAABB1234, 0xAABBCC12};
        memory.store32(vram, 0x12345678);
        check(memory.aot_load_word_left(vram + offset, 0xAABBCCDD) == left[offset] &&
                memory.aot_load_word_right(vram + offset, 0xAABBCCDD) == right[offset],
            "Unaligned AOT word loads must merge byte lanes in little-endian order");
        memory.aot_store_word_left(vram + offset, 0xAABBCCDD);
        check(memory.aot_load_word_left(vram + offset, 0) == (0xAABBCCDDu & (0xFFFFFFFFu << ((3 - offset) * 8))),
            "Unaligned left word store must retain selected source lanes");
        memory.aot_store_word_right(vram + offset, 0xAABBCCDD);
        check(memory.aot_load_word_right(vram + offset, 0) == (0xAABBCCDDu & (0xFFFFFFFFu >> (offset * 8))),
            "Unaligned right word store must retain selected source lanes");
    }
    memory.store8(base, 'a');
    memory.aot_copy_lz_match(base + 1, base, 12);
    std::array<std::uint8_t, 13> repeated{};
    memory.copy_out(base, repeated);
    check(std::all_of(repeated.begin(), repeated.end(), [](auto byte) { return byte == 'a'; }),
        "Overlapping LZ copy must use freshly produced bytes");
    memory.aot_copy_lz_match(0, 0, 0);
    rejects([&] { memory.aot_copy_lz_match(base, base + 1, 1); });
    memory.store8(wrap - 1, 'z');
    memory.aot_copy_lz_match(wrap, wrap - 1, 5);
    memory.copy_out(wrap - 1, copied);
    check(std::all_of(copied.begin(), copied.end(), [](auto byte) { return byte == 'z'; }),
        "Overlapping LZ copy must retain bytewise EDRAM mirror semantics");
    std::ostringstream diagnostics;
    auto *original = std::cerr.rdbuf(diagnostics.rdbuf());
    psprecomp::set_write_watch(base, 0);
    memory.aot_store8(base, 0x12);
    memory.aot_store16(base, 0x3456);
    memory.aot_store32(base, 0x12345678);
    std::cerr.rdbuf(original);
    if (armed) {
        check(diagnostics.str().find("now watching") != std::string::npos &&
                diagnostics.str().find("op=store8") != std::string::npos &&
                diagnostics.str().find("op=store16") != std::string::npos &&
                diagnostics.str().find("op=store32") != std::string::npos,
            "Armed write watch must observe all AOT store widths");
    } else {
        check(diagnostics.str().find("not armed at start-up") != std::string::npos,
            "Unarmed write watch must explain why observation cannot be enabled");
    }
    memory.memory_barrier();
    check(memory.bytes().size() == memory.size() && memory.vram_bytes().size() == memory.vram_size(),
        "Backing spans must represent exact physical RAM sizes");
}
void floating_contracts() {
    psprecomp::Runtime runtime;
    psprecomp::AllegrexContext cpu{};
    const auto cop = [](unsigned format, unsigned ft, unsigned fs, unsigned fd, unsigned fn) {
        return (0x11u << 26) | r(format, ft, fs, fd, fn);
    };
    for (auto entry : {std::pair{0u, 7.5f}, std::pair{1u, 4.5f}, std::pair{2u, 9.0f}, std::pair{3u, 4.0f},
             std::pair{4u, std::sqrt(6.0f)}, std::pair{5u, 6.0f}, std::pair{6u, 6.0f}, std::pair{7u, -6.0f}}) {
        cpu.fpr[8] = 6.0f;
        cpu.fpr[9] = 1.5f;
        step(runtime, cpu, cop(16, 9, 8, 10, entry.first));
        check(cpu.fpr[10] == entry.second, "COP1 arithmetic result changed");
    }
    for (auto entry :
        {std::pair{12u, 4u}, std::pair{13u, 3u}, std::pair{14u, 4u}, std::pair{15u, 3u}, std::pair{36u, 4u}}) {
        cpu.fpr[8] = 3.75f;
        step(runtime, cpu, cop(16, 0, 8, 10, entry.first));
        check(cpu.fpr_bits(10) == entry.second, "COP1 integer rounding changed");
    }
    cpu.set_fpr_bits(8, 0xFFFFFFFDu);
    step(runtime, cpu, cop(20, 0, 8, 10, 32));
    check(cpu.fpr[10] == -3.0f, "COP1 word conversion changed");
    cpu.gpr[9] = 0x3FC00000u;
    step(runtime, cpu, cop(4, 9, 8, 0, 0));
    check(cpu.fpr[8] == 1.5f, "MTC1 changed bits");
    step(runtime, cpu, cop(0, 10, 8, 0, 0));
    check(cpu.gpr[10] == 0x3FC00000u, "MFC1 changed bits");
    cpu.gpr[9] = 0xFFFFFFFFu;
    step(runtime, cpu, cop(6, 9, 31, 0, 0));
    step(runtime, cpu, cop(2, 10, 31, 0, 0));
    check(cpu.gpr[10] == 0x0181FFFFu, "FCR31 write mask changed");
    cpu.fpr[8] = std::numeric_limits<float>::infinity();
    cpu.fpr[9] = 0;
    step(runtime, cpu, cop(16, 9, 8, 10, 2));
    check(cpu.fpr_bits(10) == 0x7FC00000u, "Infinity times zero must produce architectural quiet NaN");
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--memory-watch") {
            guest_memory_contracts(true);
            return 0;
        }
        guest_memory_contracts(false);
        tool_contracts();
        elf_contracts();
        dispatch_contracts();
        diagnostic_contracts();
        format_contracts();
        integer_contracts();
        memory_contracts();
        floating_contracts();
        vector_contracts();
        std::cout << "Runtime instruction contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
