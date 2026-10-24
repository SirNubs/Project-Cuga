#include "hle/hle_common.hpp"
#include "audio/atrac_decoder.hpp"
#include "movie/avc_decoder.hpp"

#include <cstdlib>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
class Fixture {
public:
    static constexpr std::uint32_t data = 0x08840000u;
    static constexpr std::uint32_t output = 0x08860000u;
    psprecomp::Runtime runtime{64u * 1024u * 1024u};
    mhp3rd::Kernel &kernel = mhp3rd::kernel();
    psprecomp::AllegrexContext &cpu = runtime.cpu();
    std::map<std::pair<std::string, std::string>, std::uint32_t> nids;
    Fixture() {
        kernel = mhp3rd::Kernel{};
        kernel.install(runtime, 0x08801000u, 0x08820000u);
        const char *csv_path = std::getenv("PSPRECOMP_TEST_NIDS_CSV");
        runtime.nids().load_csv(csv_path ? csv_path : PSPRECOMP_TEST_NIDS_CSV);
        for (const auto &symbol : runtime.nids().all()) nids[{symbol.library, symbol.name}] = symbol.nid;
        mhp3rd::HleRegistrar hle(runtime);
        mhp3rd::register_media(hle);
        mhp3rd::register_atrac(hle);
        mhp3rd::register_mpeg(hle);
        kernel.start_loader_thread(cpu, 0x08820000u, 0);
    }
    ~Fixture() {
        kernel = mhp3rd::Kernel{};
        psprecomp::set_runtime_starvation_hook(nullptr, 0);
    }
    std::uint32_t call(std::string library, std::string name, std::initializer_list<std::uint32_t> args = {}) {
        for (unsigned i = 0; i < 8; ++i) cpu.set_gpr(i + 4, 0);
        unsigned i = 0;
        for (auto value : args) cpu.set_gpr(i++ + 4, value);
        cpu.set_gpr(31, 0x08822000u);
        auto found = nids.find({library, name});
        if (found == nids.end()) throw std::runtime_error("Unknown test NID: " + library + "::" + name);
        runtime.invoke_import(library, found->second, cpu);
        check(!runtime.stopped(), "HLE import unexpectedly stopped guest execution");
        return cpu.gpr[2];
    }
    std::uint32_t atrac(std::string name, std::initializer_list<std::uint32_t> args = {}) {
        return call("sceAtrac3plus", std::move(name), args);
    }
    std::uint32_t mpeg(std::string name, std::initializer_list<std::uint32_t> args = {}) {
        return call("sceMpeg", std::move(name), args);
    }
};
void le32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::vector<std::uint8_t> wave(bool looping = false) {
    std::vector<std::uint8_t> bytes(12, 0);
    le32(bytes, 0, 0x46464952u);
    le32(bytes, 8, 0x45564157u);
    const auto chunk = [&](std::uint32_t id, std::vector<std::uint8_t> body) {
        const auto offset = bytes.size();
        bytes.resize(offset + 8 + body.size());
        le32(bytes, offset, id);
        le32(bytes, offset + 4, static_cast<std::uint32_t>(body.size()));
        std::copy(body.begin(), body.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8));
    };
    std::vector<std::uint8_t> format(32, 0);
    format[0] = 0x70;
    format[1] = 2;
    format[2] = 1;
    format[12] = 192;
    format[16] = 14;
    format[18] = 1;
    format[21] = 4;
    format[28] = 1;
    chunk(0x20746D66u, format);
    std::vector<std::uint8_t> fact(8, 0);
    le32(fact, 0, 2000);
    chunk(0x74636166u, fact);
    if (looping) {
        std::vector<std::uint8_t> loops(60, 0);
        le32(loops, 28, 1);
        le32(loops, 44, 100);
        le32(loops, 48, 199);
        chunk(0x6C706D73u, loops);
    }
    std::vector<std::uint8_t> silence(192 * 3, 0);
    for (unsigned frame = 0; frame < 3; ++frame) silence[frame * 192] = 0xA0;
    chunk(0x61746164u, silence);
    le32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
    return bytes;
}
void atrac_contracts(Fixture &f) {
    if (!mhp3rd::audio::AtracDecoder::available()) return;
    auto &memory = f.runtime.memory();
    check(f.atrac("sceAtracSetDataAndGetID", {f.data, 0}) == 0x80630011u, "Short RIFF buffer rejected");
    check(f.atrac("sceAtracSetDataAndGetID", {f.data, 12}) == 0x80630006u, "Wrong RIFF signature rejected");
    auto bytes = wave();
    memory.copy_in(f.data, bytes);
    const auto id = f.atrac("sceAtracSetDataAndGetID", {f.data, static_cast<std::uint32_t>(bytes.size())});
    check(id < 6, "Synthetic ATRAC WAVE did not create a stream");
    check(f.atrac("sceAtracGetSoundSample", {id, f.output, f.output + 4, f.output + 8}) == 0 &&
            memory.load32(f.output) == 1999 && memory.load32(f.output + 4) == 0xFFFFFFFFu,
        "ATRAC fact sample count and no-loop metadata changed");
    check(f.atrac("sceAtracGetRemainFrame", {id, f.output}) == 0 && memory.load32(f.output) == 0xFFFFFFFFu,
        "Memory-resident stream must report all data buffered");
    check(f.atrac("sceAtracGetBitrate", {id, f.output}) == 0 && memory.load32(f.output) == 66,
        "ATRAC bitrate rounding changed");
    check(f.atrac("sceAtracSetLoopNum", {id, 1}) == 0x80630021u, "Stream without loop cannot enable looping");
    check(f.atrac("sceAtracGetStreamDataInfo", {id, f.output, f.output + 4, f.output + 8}) == 0 &&
            memory.load32(f.output) == f.data && memory.load32(f.output + 4) == 0 &&
            memory.load32(f.output + 8) == bytes.size(),
        "Full-memory stream write capacity changed");
    check(f.atrac("sceAtracGetStreamDataInfo", {id, 0, 0, 0}) == 0, "Optional output pointers must be allowed");
    check(f.atrac("sceAtracGetBufferInfoForResetting", {id, 0, 0}) == 0x80630001u &&
            f.atrac("sceAtracGetBufferInfoForResetting", {id, 2000, f.output}) == 0x80630015u,
        "Reset buffer null pointer and sample bounds rejected");
    check(f.atrac("sceAtracGetBufferInfoForResetting", {id, 0, f.output}) == 0 && memory.load32(f.output) == f.data,
        "Valid reset buffer information changed");
    check(f.atrac("sceAtracResetPlayPosition", {id, 0xFFFFFFFFu}) == 0x80630015u, "Negative sample seek rejected");
    check(f.atrac("sceAtracGetNextDecodePosition", {id, f.output}) == 0 && memory.load32(f.output) == 0,
        "Initial decode position changed");
    unsigned decoded = 0;
    for (unsigned iteration = 0; iteration < 4; ++iteration) {
        auto result = f.atrac("sceAtracDecodeData", {id, f.output + 128, f.output, f.output + 4, f.output + 8});
        if (result == 0x80630024u) break;
        check(result == 0, "Valid ATRAC frame decode failed");
        auto count = memory.load32(f.output);
        decoded += count;
        check(count > 0 && count <= 1024 && memory.load16(f.output + 128) == 0,
            "ATRAC decoded frame count or silence changed");
    }
    check(decoded == 2000 && f.atrac("sceAtracGetNextDecodePosition", {id, f.output}) == 0x80630024u,
        "ATRAC must stop at fact sample count");
    check(f.atrac("sceAtracResetPlayPosition", {id, 1200}) == 0 && f.atrac("sceAtracDecodeData", {id, 0, 0, 0, 0}) == 0,
        "Seek and optional decode outputs changed");
    check(f.atrac("sceAtracResetPlayPosition", {id, 0}) == 0 && f.atrac("sceAtracDecodeData", {id, 0, 0, 0, 0}) == 0,
        "Backward seek must reseed decoder");
    check(f.atrac("sceAtracReleaseAtracID", {id}) == 0 && f.atrac("sceAtracReleaseAtracID", {id}) == 0x80630010u,
        "Released stream IDs must report no data");
    for (const char *name :
        {"sceAtracReleaseAtracID", "sceAtracDecodeData", "sceAtracGetRemainFrame", "sceAtracGetSoundSample",
            "sceAtracGetBitrate", "sceAtracSetLoopNum", "sceAtracGetLoopStatus", "sceAtracGetNextDecodePosition",
            "sceAtracGetStreamDataInfo", "sceAtracGetBufferInfoForResetting", "sceAtracResetPlayPosition"})
        check(f.atrac(name, {6u}) == 0x80630005u, "Out-of-range stream IDs rejected consistently");
    bytes = wave(true);
    memory.copy_in(f.data, bytes);
    auto loop = f.atrac("sceAtracSetDataAndGetID", {f.data, static_cast<std::uint32_t>(bytes.size())});
    check(loop < 6 && f.atrac("sceAtracSetLoopNum", {loop, 1}) == 0, "Looped stream did not enable repetition");
    check(f.atrac("sceAtracGetLoopStatus", {loop, f.output, f.output + 4}) == 0 && memory.load32(f.output + 4) == 1,
        "Active loop status changed");
    check(f.atrac("sceAtracDecodeData", {loop, 0, f.output, 0, 0}) == 0 && memory.load32(f.output) == 200,
        "First decode must stop at inclusive loop end");
    check(f.atrac("sceAtracGetNextDecodePosition", {loop, f.output}) == 0 && memory.load32(f.output) == 100,
        "Loop must resume at declared start");
    f.atrac("sceAtracReleaseAtracID", {loop});
    for (unsigned slot = 0; slot < 6; ++slot)
        check(f.atrac("sceAtracSetDataAndGetID", {f.data, static_cast<std::uint32_t>(bytes.size())}) == slot,
            "ATRAC ID table allocation order changed");
    check(f.atrac("sceAtracSetDataAndGetID", {f.data, static_cast<std::uint32_t>(bytes.size())}) == 0x80630003u,
        "Exhausted ATRAC ID table rejected");
    for (unsigned slot = 0; slot < 6; ++slot) f.atrac("sceAtracReleaseAtracID", {slot});
}
void audio_display_contracts(Fixture &f) {
    auto &memory = f.runtime.memory();
    check(f.call("sceDisplay", "sceDisplaySetMode", {0, 480, 272}) == 0 &&
            f.call("sceDisplay", "sceDisplaySetFrameBuf", {0x04000000, 512, 3, 1}) == 0,
        "Headless frame presentation must complete");
    check(f.call("sceCtrl", "sceCtrlSetSamplingCycle", {100}) == 0 &&
            f.call("sceCtrl", "sceCtrlSetSamplingCycle", {0}) == 100,
        "Controller sampling cycle returns previous setting");
    check(
        f.call("sceCtrl", "sceCtrlSetSamplingMode", {1}) == 0 && f.call("sceCtrl", "sceCtrlSetSamplingMode", {0}) == 1,
        "Controller sampling mode returns previous setting");
    check(f.call("sceGe_user", "sceGeEdramGetAddr") == 0x04000000u &&
            f.call("sceGe_user", "sceGeEdramGetSize") == 0x00200000u &&
            f.call("sceGe_user", "sceGeEdramSetAddrTranslation", {0}) == 0,
        "GE EDRAM interface changed");
    memory.store32(f.data, 0x0F000000u);
    memory.store32(f.data + 4, 0x0C000000u);
    auto list = f.call("sceGe_user", "sceGeListEnQueue", {f.data, f.data, 0xFFFFFFFFu, 0});
    check(f.call("sceGe_user", "sceGeListSync", {list, 0}) == 2, "Stalled GE list must report drawing");
    check(f.call("sceGe_user", "sceGeListUpdateStallAddr", {list, 0}) == 0 &&
            f.call("sceGe_user", "sceGeListSync", {list, 0}) == 0,
        "Released GE stall must finish list");
    check(f.call("sceGe_user", "sceGeListUpdateStallAddr", {0xFFFFFFFFu, 0}) == mhp3rd::error::kIllegalArgument,
        "Unknown GE list must reject stall update");
    memory.store32(f.data + 32, 0);
    memory.store32(f.data + 36, 1);
    memory.store32(f.data + 40, 0);
    memory.store32(f.data + 44, 2);
    auto callback = f.call("sceGe_user", "sceGeSetCallback", {f.data + 32});
    check(f.call("sceGe_user", "sceGeListEnQueueHead", {f.data, 0, callback, 0}) > 0, "Head-enqueued GE list failed");
    check(f.call("sceGe_user", "sceGeUnsetCallback", {callback}) == 0 &&
            f.call("sceGe_user", "sceGeDrawSync", {0}) == 0 && f.call("sceGe_user", "sceGeBreak", {0}) == 0 &&
            f.call("sceGe_user", "sceGeContinue") == 0,
        "GE callbacks/list cleanup failed");
    const auto call = [&](const char *name, std::initializer_list<std::uint32_t> args = {}) {
        return f.call("sceAudio", name, args);
    };
    check(call("sceAudioChReserve", {0, 16, 0}) == 0 && call("sceAudioChReserve", {0, 16, 0}) == 0x80260002u &&
            call("sceAudioChReserve", {8, 16, 0}) == 0x80260002u,
        "Audio channel reservation exclusivity changed");
    for (unsigned channel = 1; channel < 8; ++channel)
        check(call("sceAudioChReserve", {0xFFFFFFFFu, 16, 0}) == channel, "Automatic audio reservation order changed");
    check(call("sceAudioChReserve", {0xFFFFFFFFu, 16, 0}) == 0x80260002u, "Audio channel exhaustion must fail");
    check(call("sceAudioGetChannelRestLength", {8}) == 0x80260002u && call("sceAudioGetChannelRestLength", {0}) == 0,
        "Audio rest length invalid/idle contract changed");
    for (unsigned sample = 0; sample < 32; ++sample) memory.store16(f.data + sample * 2, 1000);
    check(call("sceAudioOutputPannedBlocking", {0, 0x8000, 0x8000, f.data}) == 16 &&
            call("sceAudioGetChannelRestLength", {0}) <= 16,
        "Audio output frame count/timing changed");
    check(call("sceAudioSetChannelDataLen", {0, 8}) == 0 && call("sceAudioChangeChannelConfig", {0, 0x10}) == 0 &&
            call("sceAudioChangeChannelVolume", {0, 0x1000, 0x1000}) == 0,
        "Audio channel settings failed");
    for (unsigned channel = 0; channel < 8; ++channel)
        check(call("sceAudioChRelease", {channel}) == 0, "Audio release failed");
    check(call("sceAudioOutputPannedBlocking", {8, 0, 0, 0}) == 0x80260002u, "Unknown audio channel output accepted");
    constexpr auto core = 0x1234u;
    const auto sas = [&](const char *name, std::initializer_list<std::uint32_t> args = {}) {
        return f.call("sceSasCore", name, args);
    };
    check(sas("__sceSasInit", {core, 16, 1, 0, 44100}) == 0 && sas("__sceSasGetOutputmode", {core}) == 0,
        "SAS initialization output mode changed");
    memory.store8(f.data, 0u);
    memory.store8(f.data + 1u, 3u);
    for (unsigned byte = 2; byte < 16; ++byte) memory.store8(f.data + byte, 0x11u);
    check(sas("__sceSasSetVoice", {core, 0, f.data, 16, 1}) == 0 && sas("__sceSasSetPitch", {core, 0, 0x1000}) == 0 &&
            sas("__sceSasSetVolume", {core, 0, 0x1000, 0x1000, 0, 0}) == 0 && sas("__sceSasSetKeyOn", {core, 0}) == 0,
        "SAS PCM setup failed");
    check((sas("__sceSasGetEndFlag", {core}) & 1u) == 0 && sas("__sceSasCore", {core, f.output}) == 0 &&
            memory.load16(f.output) > 990,
        "SAS HLE must write mixed PCM into guest output");
    check(sas("__sceSasCore", {core, 0xFFFFFFFFu}) == 0, "Unavailable SAS destination must safely drop grain");
    check(sas("__sceSasSetKeyOff", {core, 0}) == 0 && (sas("__sceSasGetEndFlag", {core}) & 1u), "SAS key off failed");
    memory.store8(f.data, 0);
    memory.store8(f.data + 1, 7);
    check(sas("__sceSasSetVoice", {core, 0, f.data, 16, 0}) == 0 && sas("__sceSasSetSimpleADSR", {core, 0, 0, 0}) == 0,
        "SAS VAG/ADSR imports failed");
    for (const char *name : {"__sceSasRevType", "__sceSasRevParam", "__sceSasRevEVOL", "__sceSasRevVON"})
        check(sas(name, {core, 0, 0}) == 0, "Unmodeled reverb sends must succeed");
}

void mpeg_contracts(Fixture &f) {
    if (!mhp3rd::movie::AvcDecoder::available() || !mhp3rd::audio::AtracDecoder::available()) {
        check(f.mpeg("sceMpegGetAvcAu", {0, 0, 0, 0}) == 0x80618001u, "Disabled movies must report no data");
        return;
    }
    auto &memory = f.runtime.memory();
    constexpr auto handle = Fixture::data;
    constexpr auto ring = Fixture::data + 256u;
    constexpr auto stream_data = Fixture::data + 2048u;
    constexpr auto work = Fixture::data + 0x10000u;
    constexpr auto header = Fixture::output;
    check(f.mpeg("sceMpegInit") == 0 && f.mpeg("sceMpegFinish") == 0, "MPEG initialization failed");
    check(f.mpeg("sceMpegQueryMemSize") == 0x10000u &&
            f.mpeg("sceMpegRingbufferQueryMemSize", {3}) == 3u * (2048u + 104u),
        "MPEG memory sizing changed");
    check(f.mpeg("sceMpegCreate", {handle, work, 65535u, 0, 16, 0, 0}) == 0x80610022u,
        "Insufficient MPEG work memory accepted");
    check(f.mpeg("sceMpegRingbufferConstruct", {ring, 2, stream_data, 4096, 0, 123}) == 0,
        "MPEG ring construction failed");
    check(memory.load32(ring + 16) == 2048u && memory.load32(ring + 32) == stream_data + 4096u &&
            f.mpeg("sceMpegRingbufferAvailableSize", {ring}) == 2,
        "Ring geometry or initial capacity changed");
    check(f.mpeg("sceMpegRingbufferPut", {ring, 0, 2}) == 0, "Zero-packet feed must return immediately");
    memory.store32(ring + 12, 2);
    check(f.mpeg("sceMpegRingbufferAvailableSize", {ring}) == 0 && f.mpeg("sceMpegRingbufferPut", {ring, 2, 0}) == 0,
        "Full ring must not call guest feeder");
    memory.store32(ring + 12, 0);
    check(f.mpeg("sceMpegQueryStreamOffset", {handle, header, header + 64}) == 0x806101FEu &&
            memory.load32(header + 64) == 0,
        "Invalid PSMF signature rejected");
    memory.store32(header, 0x464D5350u);
    memory.store32(header + 8, 0x00080000u);
    memory.store32(header + 12, 0x00080000u);
    check(f.mpeg("sceMpegQueryStreamOffset", {handle, header, header + 64}) == 0 && memory.load32(header + 64) == 2048u,
        "PSMF big-endian stream offset changed");
    check(f.mpeg("sceMpegQueryStreamSize", {header, header + 64}) == 0 && memory.load32(header + 64) == 2048u,
        "PSMF stream size changed");
    memory.store8(header + 15, 1);
    check(f.mpeg("sceMpegQueryStreamSize", {header, header + 64}) == 0x806101FEu, "Unaligned PSMF size accepted");
    memory.store8(header + 15, 0);
    check(f.mpeg("sceMpegCreate", {handle, work, 65536, ring, 16, 0, 0}) == 0 && mhp3rd::mpeg_active() &&
            memory.load32(handle) == work + 0x30u,
        "MPEG handle lifetime changed");
    const auto stream = f.mpeg("sceMpegRegistStream", {handle, 0, 0});
    check(stream == work + 0x100u && f.mpeg("sceMpegRegistStream", {0, 0, 0}) == 0, "MPEG stream registration changed");
    const auto au = header + 128;
    check(f.mpeg("sceMpegInitAu", {handle, 77, au}) == 0 && memory.load32(au) == 0xFFFFFFFFu &&
            memory.load32(au + 16) == 77u,
        "Access-unit initialization changed");
    check(f.mpeg("sceMpegQueryAtracEsSize", {handle, header + 64, header + 68}) == 0 &&
            memory.load32(header + 64) == 2112u && memory.load32(header + 68) == 8192u,
        "Audio ES/output sizing changed");
    check(f.mpeg("sceMpegGetAvcAu", {0, stream, au, 0}) == 0x806101FEu &&
            f.mpeg("sceMpegGetAvcAu", {handle, stream, au, 0}) == 0x80618001u,
        "Empty/invalid MPEG stream must report no data");
    check(f.mpeg("sceMpegGetAtracAu", {handle, 0, au, 0}) == 0x80618001u, "Empty audio queue must report no data");
    check(
        f.mpeg("sceMpegAvcQueryYCbCrSize", {handle, 0, 16, 16, header + 64}) == 0 && memory.load32(header + 64) == 512u,
        "YCbCr buffer size changed");
    check(f.mpeg("sceMpegAvcInitYCbCr", {handle, 0, 16, 16, header + 512}) == 0, "YCbCr initialization failed");
    check(f.mpeg("sceMpegAvcDecodeYCbCr", {0, au, header + 64, header + 68}) == 0x806101FEu,
        "Unknown MPEG handle decode accepted");
    // Guest feeder writes one public pack into the documented ring storage.
    const std::array<std::uint8_t, 46> black{0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10,
        0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x28, 0xf1, 0x22, 0x6a, 0x00, 0x00, 0x00, 0x01, 0x68,
        0xce, 0x0f, 0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0};
    std::vector<std::uint8_t> packet(2048, 0);
    packet[2] = 1;
    packet[3] = 0xBA;
    std::vector<std::uint8_t> elementary{0, 0, 1, 9, 0xF0};
    elementary.insert(elementary.end(), black.begin(), black.end());
    const auto body = elementary.size() + 3;
    packet[16] = 1;
    packet[17] = 0xE0;
    packet[18] = static_cast<std::uint8_t>(body >> 8);
    packet[19] = static_cast<std::uint8_t>(body);
    packet[20] = 0x80;
    std::copy(elementary.begin(), elementary.end(), packet.begin() + 23);
    memory.copy_in(stream_data, packet);
    memory.store32(ring + 12, 1);
    check(f.mpeg("sceMpegGetAvcAu", {handle, stream, au, header + 64}) == 0 &&
            memory.load32(au + 20) == elementary.size(),
        "Ring demux did not produce final video access unit");
    check(memory.load32(ring + 12) == 0 && f.mpeg("sceMpegRingbufferAvailableSize", {ring}) == 2,
        "Consumed ring packet was not retired");
    memory.store32(header + 64, header + 512);
    check(
        f.mpeg("sceMpegAvcDecodeYCbCr", {handle, au, header + 64, header + 68}) == 0 && memory.load32(header + 68) == 1,
        "MPEG HLE did not decode synthetic black picture");
    check(f.mpeg("sceMpegAvcDecodeDetail", {handle, header + 80}) == 0 && memory.load32(header + 84) == 1 &&
            memory.load32(header + 88) == 16 && memory.load32(header + 92) == 16,
        "Decoded picture details changed");
    check(f.mpeg("sceMpegAvcConvertToYuv420", {handle, header + 1024, header + 512, 0}) == 0,
        "Owned YCbCr conversion failed");
    check(f.call("sceJpeg", "sceJpegCsc", {header + 2048, header + 1024, (16u << 16u) | 16u, 16, 0}) == 0 &&
            memory.load32(header + 2048) == 0xFF000000u,
        "Black YCbCr conversion must yield opaque black RGBA");
    check(f.call("sceJpeg", "sceJpegCsc", {header + 2048, header + 1024, (16u << 16u) | 16u, 15, 0}) == 0,
        "Short output stride must be safely ignored");
    check(
        f.mpeg("sceMpegAvcDecodeStopYCbCr", {handle, header + 64, header + 68}) == 0 && memory.load32(header + 68) == 0,
        "Drain must not duplicate final picture");
    check(f.mpeg("sceMpegAtracDecode", {0, au, header + 4096, 0}) == 0x806101FEu &&
            f.mpeg("sceMpegAtracDecode", {handle, au, header + 4096, 0}) == 0 && memory.load32(header + 4096) == 0,
        "Absent MPEG audio must produce silence");
    check(f.mpeg("sceMpegMallocAvcEsBuf", {handle}) == 1 && f.mpeg("sceMpegFreeAvcEsBuf", {handle, 1}) == 0,
        "Video ES sentinel allocation changed");
    check(f.mpeg("sceMpegFlushAllStream", {handle}) == 0 && f.mpeg("sceMpegUnRegistStream", {handle, stream}) == 0,
        "Stream flush/unregister failed");
    check(f.mpeg("sceMpegDelete", {handle}) == 0 && !mhp3rd::mpeg_active() &&
            f.mpeg("sceMpegRingbufferDestruct", {ring}) == 0,
        "MPEG lifetime cleanup failed");
}

}
int main() {
    try {
        Fixture fixture;
        audio_display_contracts(fixture);
        atrac_contracts(fixture);
        mpeg_contracts(fixture);
        std::cout << "Media HLE contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
