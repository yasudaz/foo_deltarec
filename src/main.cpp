// main.cpp - foo_deltarec: PCM to 1-bit DSD (DSF/DFF) converter component for foobar2000.
#include "stdafx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsdwriter.hpp"
#include "id3v2_builder.hpp"
#include "modulator.hpp"
#include "ntf.hpp"
#include "resampler.hpp"

DECLARE_COMPONENT_VERSION(
    "deltarec DSD Converter",
    "0.1.0",
    "Offline PCM to 1-bit DSD (DSF / DSDIFF) converter for foobar2000.\n"
    "Two-stage polyphase FIR upsampler + 7th-order noise-shaping delta-sigma modulator.\n\n"
    "Right-click tracks and select 'Convert to DSD (deltarec)...'."
);

VALIDATE_COMPONENT_FILENAME("foo_deltarec.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;

using namespace deltarec;

namespace {

// ---- Persistent configuration variables ----------------------------------
// {8D42E610-3C7A-4F81-9E12-A5B6C7D8E901} .. {8D42E619-...}
static const GUID guid_cfg_rate        = { 0x8d42e610, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x01 } };
static const GUID guid_cfg_base_mode   = { 0x8d42e611, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x02 } };
static const GUID guid_cfg_format      = { 0x8d42e612, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x03 } };
static const GUID guid_cfg_order       = { 0x8d42e613, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x04 } };
static const GUID guid_cfg_gain_str    = { 0x8d42e614, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x05 } };
static const GUID guid_cfg_dither      = { 0x8d42e615, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x06 } };
static const GUID guid_cfg_copy_tags   = { 0x8d42e616, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x07 } };
static const GUID guid_cfg_same_folder = { 0x8d42e617, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x08 } };
static const GUID guid_cfg_out_dir     = { 0x8d42e618, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x09 } };
static const GUID guid_cfg_naming      = { 0x8d42e619, 0x3c7a, 0x4f81, { 0x9e, 0x12, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0x0a } };

static cfg_int    g_cfg_rate(guid_cfg_rate, 256);
static cfg_int    g_cfg_base_mode(guid_cfg_base_mode, 0);      // 0: Auto, 1: 44100, 2: 48000
static cfg_int    g_cfg_format(guid_cfg_format, 0);            // 0: DSF, 1: DFF
static cfg_int    g_cfg_order(guid_cfg_order, 7);              // 2..8
static cfg_string g_cfg_gain_str(guid_cfg_gain_str, "0.0");
static cfg_int    g_cfg_dither(guid_cfg_dither, 1);
static cfg_int    g_cfg_copy_tags(guid_cfg_copy_tags, 1);
static cfg_int    g_cfg_same_folder(guid_cfg_same_folder, 1);
static cfg_string g_cfg_out_dir(guid_cfg_out_dir, "");
static cfg_int    g_cfg_naming(guid_cfg_naming, 0);            // 0: source name, 1: %tracknumber% - %title%, 2: %artist% - %title%

struct ConvertSettings {
    int rate = 256;          // 64, 128, 256, 512
    int baseMode = 0;        // 0: Auto, 1: 44100, 2: 48000
    int format = 0;          // 0: DSF, 1: DFF
    int order = 7;           // 2..8
    double hinf = 1.4;
    double gainDb = 0.0;
    double limit = 0.6;
    double dither = 0.05;
    double attenuation = 160.0;
    bool copyTags = true;
    bool sameFolder = true;
    std::string outDir;
    int namingMode = 0;
};

// ---- UTF-8 <-> Wide string helpers ---------------------------------------
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
    return s;
}

std::string sanitizeFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ||
            static_cast<unsigned char>(c) < 32) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "output";
    return out;
}

bool getTrackSourceFolderAndStem(const metadb_handle_ptr& track, std::string& folderOut, std::string& stemOut) {
    pfc::string8 nativePath;
    if (filesystem::g_get_native_path(track->get_path(), nativePath)) {
        std::string p(nativePath.c_str());
        // Strip possible "\\?\" prefix if present
        if (p.rfind("\\\\?\\", 0) == 0 && p.size() > 6 && p[5] == ':') {
            p = p.substr(4);
        }
        size_t slash = p.find_last_of("/\\");
        if (slash != std::string::npos) {
            folderOut = p.substr(0, slash);
            std::string fname = p.substr(slash + 1);
            size_t dot = fname.find_last_of('.');
            stemOut = (dot != std::string::npos && dot > 0) ? fname.substr(0, dot) : fname;
            return true;
        }
    }
    return false;
}

// ---- DSP Plan & High-Speed Pipeline --------------------------------------
uint32_t gcd32(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

struct Plan {
    uint32_t fsIn = 0, fsDsd = 0, fi = 0;
    uint32_t L1 = 1, M1 = 1, L2 = 1;
    double passband = 0, stop1 = 0, stop2 = 0;
    double osr = 0;
    PolyphaseResampler::Spec s1, s2;
    NtfDesign ntf;
};

Plan makePlan(const ConvertSettings& o, uint32_t fsIn) {
    Plan p;
    p.fsIn = fsIn;
    uint32_t base = 44100;
    if (o.baseMode == 1) {
        base = 44100;
    } else if (o.baseMode == 2) {
        base = 48000;
    } else {
        // Auto-select 48 kHz family if input is a multiple of 48000 or 32000 Hz
        if (fsIn % 48000 == 0 || fsIn == 32000 || fsIn == 64000) base = 48000;
        else base = 44100;
    }

    p.fsDsd = base * static_cast<uint32_t>(o.rate);
    p.L2 = 1;
    while ((p.L2 * 2) <= 64 && p.fsDsd % (p.L2 * 2) == 0 && p.fsDsd / (p.L2 * 2) >= 8 * fsIn) p.L2 *= 2;
    p.fi = p.fsDsd / p.L2;
    uint32_t g = gcd32(p.fi, fsIn);
    p.L1 = p.fi / g;
    p.M1 = fsIn / g;
    p.passband = 0.4535 * fsIn;
    if (p.passband >= 0.5 * fsIn) p.passband = 0.49 * fsIn;
    p.stop1 = std::min(0.5 * fsIn, p.fi - p.passband);
    p.stop2 = p.fi - p.passband;
    p.s1 = {p.L1, p.M1, p.passband, p.stop1, double(fsIn), o.attenuation};
    p.s2 = {p.L2, 1, p.passband, p.stop2, double(p.fi), o.attenuation};
    double band = std::max(20000.0, p.passband);
    p.osr = p.fsDsd / (2.0 * band);
    p.ntf = designNtf(o.order, p.osr, o.hinf);
    return p;
}

struct ChannelPipe {
    PolyphaseResampler s1, s2;
    bool twoStage = true;
    DeltaSigmaModulator mod;
    std::vector<double> buf1, buf2;
    std::vector<uint8_t> bits;

    static constexpr size_t kRingSlots = 8;
    static constexpr size_t kRingMask = kRingSlots - 1;
    struct TileSlot {
        std::vector<double> x;
        std::vector<double> d;
        size_t count = 0;
        bool isEnd = false;
    };
    TileSlot ring[kRingSlots];
    std::atomic<uint64_t> writeIdx{0};
    std::atomic<uint64_t> readIdx{0};

    std::thread prepThread;
    std::mutex prepMu;
    std::condition_variable prepCv;
    const double* prepIn = nullptr;
    size_t prepN = 0;
    bool prepFlush = false;
    bool prepHasWork = false;
    bool prepStop = false;

    ChannelPipe() {
        prepThread = std::thread([this] { prepLoop(); });
    }

    ~ChannelPipe() {
        {
            std::lock_guard<std::mutex> lk(prepMu);
            prepStop = true;
        }
        prepCv.notify_one();
        if (prepThread.joinable()) prepThread.join();
    }

    ChannelPipe(const ChannelPipe&) = delete;
    ChannelPipe& operator=(const ChannelPipe&) = delete;

    static inline void spinPause(uint32_t& spins) {
#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64)
        if (spins < 1024) {
            _mm_pause();
            ++spins;
            return;
        }
#endif
        std::this_thread::yield();
    }

    void pushSlot(const double* src, size_t count, bool isEnd) {
        uint64_t w = writeIdx.load(std::memory_order_relaxed);
        uint32_t spins = 0;
        while (w - readIdx.load(std::memory_order_acquire) >= kRingSlots) {
            spinPause(spins);
        }
        TileSlot& slot = ring[w & kRingMask];
        slot.count = count;
        slot.isEnd = isEnd;
        if (count > 0) {
            if (slot.x.size() < count) {
                slot.x.resize(count);
                slot.d.resize(count);
            }
            mod.prepare(src, slot.x.data(), slot.d.data(), count);
        }
        writeIdx.store(w + 1, std::memory_order_release);
    }

    void prepLoop() {
        while (true) {
            const double* in = nullptr;
            size_t n = 0;
            bool flush = false;
            {
                std::unique_lock<std::mutex> lk(prepMu);
                prepCv.wait(lk, [this] { return prepStop || prepHasWork; });
                if (prepStop) return;
                in = prepIn;
                n = prepN;
                flush = prepFlush;
                prepHasWork = false;
            }

            constexpr size_t kInTile = 512;
            constexpr size_t kTile = 256;
            for (size_t inPos = 0; inPos < n; inPos += kInTile) {
                const size_t inChunk = std::min(kInTile, n - inPos);
                const bool lastIn = (inPos + inChunk == n);
                buf1.clear();
                s1.process(in + inPos, inChunk, buf1);
                if (flush && lastIn) s1.flush(buf1);

                const double* p1 = buf1.data();
                const size_t n1 = buf1.size();
                if (!twoStage) {
                    constexpr size_t kChunk = 4096;
                    for (size_t pos = 0; pos < n1; pos += kChunk) {
                        pushSlot(p1 + pos, std::min(kChunk, n1 - pos), false);
                    }
                } else {
                    for (size_t pos = 0; pos < n1; pos += kTile) {
                        const size_t chunk = std::min(kTile, n1 - pos);
                        buf2.clear();
                        s2.process(p1 + pos, chunk, buf2);
                        pushSlot(buf2.data(), buf2.size(), false);
                    }
                    if (flush && lastIn) {
                        buf2.clear();
                        s2.flush(buf2);
                        if (!buf2.empty()) pushSlot(buf2.data(), buf2.size(), false);
                    }
                }
            }
            if (n == 0 && flush) {
                buf1.clear();
                s1.flush(buf1);
                if (!twoStage) {
                    if (!buf1.empty()) pushSlot(buf1.data(), buf1.size(), false);
                } else {
                    if (!buf1.empty()) {
                        buf2.clear();
                        s2.process(buf1.data(), buf1.size(), buf2);
                        if (!buf2.empty()) pushSlot(buf2.data(), buf2.size(), false);
                    }
                    buf2.clear();
                    s2.flush(buf2);
                    if (!buf2.empty()) pushSlot(buf2.data(), buf2.size(), false);
                }
            }
            pushSlot(nullptr, 0, true);
        }
    }

    void run(const double* in, size_t n, bool flush) {
        bits.clear();
        const uint32_t totalL = twoStage ? (s1.L() * s2.L()) : s1.L();
        bits.reserve((n * totalL) / (8 * std::max(1u, s1.M())) + 256);

        {
            std::lock_guard<std::mutex> lk(prepMu);
            prepIn = in;
            prepN = n;
            prepFlush = flush;
            prepHasWork = true;
        }
        prepCv.notify_one();

        uint64_t r = readIdx.load(std::memory_order_relaxed);
        while (true) {
            uint32_t spins = 0;
            while (writeIdx.load(std::memory_order_acquire) == r) {
                spinPause(spins);
            }
            TileSlot& slot = ring[r & kRingMask];
            const size_t cnt = slot.count;
            const bool end = slot.isEnd;
            if (cnt > 0) {
                mod.modulatePrepared(slot.x.data(), slot.d.data(), cnt, bits);
            }
            readIdx.store(++r, std::memory_order_release);
            if (end) break;
        }
        if (flush) mod.finish(bits);
    }
};

class ChannelWorkerPool {
public:
    explicit ChannelWorkerPool(std::vector<std::unique_ptr<ChannelPipe>>& pipes) : pipes_(pipes) {
        const size_t workers = pipes_.size() > 1 ? pipes_.size() - 1 : 0;
        threads_.reserve(workers);
        for (size_t w = 0; w < workers; ++w) {
            const uint32_t chIdx = static_cast<uint32_t>(w + 1);
            threads_.emplace_back([this, chIdx] { workerLoop(chIdx); });
        }
    }

    ~ChannelWorkerPool() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cvStart_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
    }

    void runAll(const std::vector<std::vector<double>>& chan, size_t got, bool eof) {
        const uint32_t ch = static_cast<uint32_t>(pipes_.size());
        if (ch > 1) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                chanPtr_ = &chan;
                got_ = got;
                eof_ = eof;
                pending_ = ch - 1;
                ++gen_;
            }
            cvStart_.notify_all();
        }
        pipes_[0]->run(chan[0].data(), got, eof);

        if (ch > 1) {
            std::unique_lock<std::mutex> lk(mu_);
            cvDone_.wait(lk, [this] { return pending_ == 0; });
        }
    }

private:
    void workerLoop(uint32_t c) {
        uint64_t myGen = 0;
        while (true) {
            const std::vector<std::vector<double>>* chan = nullptr;
            size_t got = 0;
            bool eof = false;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cvStart_.wait(lk, [&] { return stop_ || gen_ != myGen; });
                if (stop_) return;
                myGen = gen_;
                chan = chanPtr_;
                got = got_;
                eof = eof_;
            }
            pipes_[c]->run((*chan)[c].data(), got, eof);
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (--pending_ == 0) cvDone_.notify_one();
            }
        }
    }

    std::vector<std::unique_ptr<ChannelPipe>>& pipes_;
    std::vector<std::thread> threads_;
    std::mutex mu_;
    std::condition_variable cvStart_;
    std::condition_variable cvDone_;
    const std::vector<std::vector<double>>* chanPtr_ = nullptr;
    size_t got_ = 0;
    bool eof_ = false;
    uint32_t pending_ = 0;
    uint64_t gen_ = 0;
    bool stop_ = false;
};

// ---- Background Conversion Worker (threaded_process_callback) ------------
class DsdConvertWorker : public threaded_process_callback {
public:
    DsdConvertWorker(metadb_handle_list_cref items, const ConvertSettings& settings)
        : items_(items), settings_(settings) {}

    void on_init(ctx_t p_wnd) override {}

    void run(threaded_process_status& p_status, abort_callback& p_abort) override {
        const t_size trackCount = items_.get_count();
        const double gain = 0.5 * std::pow(10.0, settings_.gainDb / 20.0);
        const char* ext = (settings_.format == 1) ? ".dff" : ".dsf";

        // Detect if multiple items share the same physical path (e.g., CUE sheet subsongs)
        bool hasDuplicatePaths = false;
        for (t_size i = 1; i < trackCount; ++i) {
            if (std::strcmp(items_[i]->get_path(), items_[i - 1]->get_path()) == 0) {
                hasDuplicatePaths = true;
                break;
            }
        }

        titleformat_object::ptr tfTrackTitle;
        titleformat_object::ptr tfArtistTitle;
        titleformat_compiler::get()->compile_safe_ex(tfTrackTitle, "[%tracknumber% - ]%title%");
        titleformat_compiler::get()->compile_safe_ex(tfArtistTitle, "[%artist% - ]%title%");

        for (t_size idx = 0; idx < trackCount; ++idx) {
            p_abort.check();
            metadb_handle_ptr track = items_[idx];

            // Retrieve track metadata
            file_info_impl info;
            track->get_info(info);
            const double lengthSec = info.get_length();

            // Determine output folder & filename
            std::string srcFolder, srcStem;
            bool hasLocal = getTrackSourceFolderAndStem(track, srcFolder, srcStem);

            std::string outFolder = (settings_.sameFolder && hasLocal) ? srcFolder : settings_.outDir;
            while (!outFolder.empty() && (outFolder.back() == '\\' || outFolder.back() == '/')) {
                outFolder.pop_back();
            }
            if (!outFolder.empty()) {
                std::wstring wDir = utf8ToWide(outFolder);
                SHCreateDirectoryExW(nullptr, wDir.c_str(), nullptr);
            }

            std::string outStem;
            int effectiveNaming = settings_.namingMode;
            if (effectiveNaming == 0 && (hasDuplicatePaths || !hasLocal || srcStem.empty())) {
                effectiveNaming = 1;
            }
            if (effectiveNaming == 0) {
                outStem = sanitizeFilename(srcStem);
            } else if (effectiveNaming == 1) {
                pfc::string8 formatted;
                track->format_title(nullptr, formatted, tfTrackTitle, nullptr);
                outStem = sanitizeFilename(formatted.c_str());
            } else {
                pfc::string8 formatted;
                track->format_title(nullptr, formatted, tfArtistTitle, nullptr);
                outStem = sanitizeFilename(formatted.c_str());
            }

            std::string outPath = outFolder + "\\" + outStem + ext;

            // Open foobar2000 decoder for this track
            input_decoder::ptr decoder;
            input_entry::g_open_for_decoding(decoder, nullptr, track->get_path(), p_abort);
            decoder->initialize(track->get_subsong_index(), input_flag_simpledecode, p_abort);

            audio_chunk_impl chunk;
            if (!decoder->run(chunk, p_abort)) {
                decoder.release();
                continue;
            }

            const uint32_t fsIn = chunk.get_sample_rate();
            const uint32_t ch = chunk.get_channels();
            if (ch == 0 || ch > 6) {
                FB2K_console_formatter() << "foo_deltarec: skipping track (unsupported channel count " << ch << "): " << track->get_path();
                decoder.release();
                continue;
            }

            Plan plan = makePlan(settings_, fsIn);

            std::vector<std::unique_ptr<ChannelPipe>> pipes(ch);
            for (uint32_t c = 0; c < ch; ++c) {
                pipes[c] = std::make_unique<ChannelPipe>();
                pipes[c]->s1.init(plan.s1);
                pipes[c]->twoStage = plan.L2 > 1;
                if (pipes[c]->twoStage) pipes[c]->s2.init(plan.s2);
                pipes[c]->mod = DeltaSigmaModulator(plan.ntf, settings_.limit, settings_.dither, 8.0, 0x5eed1234u + c * 7919u);
            }

            const uint64_t estTotalFrames = lengthSec > 0.0 ? static_cast<uint64_t>(lengthSec * fsIn) : 0;
            uint64_t doneFrames = 0;
            auto t0 = std::chrono::steady_clock::now();

            try {
                DsdWriter writer(
                    outPath,
                    settings_.format == 1 ? DsdWriter::Format::DFF : DsdWriter::Format::DSF,
                    ch,
                    plan.fsDsd
                );
                ChannelWorkerPool pool(pipes);

                constexpr size_t kBatchFrames = 16384;
                std::vector<std::vector<double>> chan(ch);
                std::vector<std::vector<uint8_t>> outBits(ch);
                for (uint32_t c = 0; c < ch; ++c) chan[c].reserve(kBatchFrames);

                bool hasChunk = true;
                while (hasChunk) {
                    p_abort.check();
                    for (uint32_t c = 0; c < ch; ++c) chan[c].clear();
                    size_t batchFrames = 0;

                    while (hasChunk && batchFrames < kBatchFrames) {
                        const audio_sample* src = chunk.get_data();
                        const size_t frames = chunk.get_sample_count();
                        if (chunk.get_channels() == ch && chunk.get_sample_rate() == fsIn && frames > 0) {
                            for (uint32_t c = 0; c < ch; ++c) {
                                const size_t prev = chan[c].size();
                                chan[c].resize(prev + frames);
                                double* __restrict dst = chan[c].data() + prev;
                                const audio_sample* __restrict s = src + c;
                                for (size_t i = 0; i < frames; ++i) {
                                    dst[i] = static_cast<double>(s[i * ch]) * gain;
                                }
                            }
                            batchFrames += frames;
                        }
                        if (batchFrames >= kBatchFrames) {
                            hasChunk = decoder->run(chunk, p_abort);
                            break;
                        }
                        hasChunk = decoder->run(chunk, p_abort);
                    }

                    const bool eof = !hasChunk;
                    pool.runAll(chan, batchFrames, eof);
                    for (uint32_t c = 0; c < ch; ++c) outBits[c].swap(pipes[c]->bits);
                    writer.write(outBits);

                    doneFrames += batchFrames;
                    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    double rt = (doneFrames / double(fsIn)) / std::max(el, 1e-6);

                    double trackFrac = (estTotalFrames > 0) ? std::min(1.0, double(doneFrames) / double(estTotalFrames)) : 0.5;
                    double overallFrac = (double(idx) + trackFrac) / double(trackCount);
                    p_status.set_progress_float(overallFrac);

                    char statusBuf[512];
                    std::snprintf(statusBuf, sizeof(statusBuf), "[%zu/%zu] %s%s (DSD%d, %.1fx realtime)",
                                  static_cast<size_t>(idx + 1), static_cast<size_t>(trackCount),
                                  outStem.c_str(), ext, settings_.rate, rt);
                    p_status.set_item(statusBuf);
                }

                decoder.release();

                // Build ID3v2 tag with all metadata + front cover art if requested
                std::vector<uint8_t> id3Tag;
                if (settings_.copyTags) {
                    album_art_data_ptr coverArt;
                    try {
                        auto aam = album_art_manager_v2::get();
                        pfc::list_single_ref_t<metadb_handle_ptr> artItems(track);
                        pfc::list_single_ref_t<GUID> artIds(album_art_ids::cover_front);
                        auto extArt = aam->open(artItems, artIds, p_abort);
                        coverArt = extArt->query(album_art_ids::cover_front, p_abort);
                    } catch (...) {}
                    id3Tag = buildId3v2Tag(info, coverArt);
                }

                writer.finish(pipes[0]->mod.stats().samples, id3Tag);

                double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                double rt = (doneFrames / double(fsIn)) / std::max(el, 1e-6);
                FB2K_console_formatter() << "foo_deltarec: wrote " << outPath.c_str()
                                         << " (DSD" << settings_.rate << ", "
                                         << pfc::format_float(el, 0, 1) << " s, "
                                         << pfc::format_float(rt, 0, 1) << "x realtime)";
            } catch (const exception_aborted&) {
                std::wstring wOut = utf8ToWide(outPath);
                DeleteFileW(wOut.c_str());
                throw;
            } catch (const std::exception& e) {
                FB2K_console_formatter() << "foo_deltarec error (" << outPath.c_str() << "): " << e.what();
            }
        }
    }

    void on_done(ctx_t p_wnd, bool p_was_aborted) override {}

private:
    metadb_handle_list items_;
    ConvertSettings settings_;
};

// ---- Modal Configuration Dialog ------------------------------------------
struct DialogContext {
    metadb_handle_list items;
    ConvertSettings settings;
    bool accepted = false;
};

bool browseForFolder(HWND parent, std::wstring& folderInOut) {
    IFileOpenDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd) return false;

    bool ok = false;
    DWORD opts = 0;
    if (SUCCEEDED(pfd->GetOptions(&opts))) {
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    pfd->SetTitle(L"Select DSD Output Folder");

    if (!folderInOut.empty()) {
        IShellItem* psiInit = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(folderInOut.c_str(), nullptr, IID_PPV_ARGS(&psiInit))) && psiInit) {
            pfd->SetFolder(psiInit);
            psiInit->Release();
        }
    }

    if (SUCCEEDED(pfd->Show(parent))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
                folderInOut = pszPath;
                CoTaskMemFree(pszPath);
                ok = true;
            }
            psi->Release();
        }
    }
    pfd->Release();
    return ok;
}

void updateOutDirState(HWND hwnd) {
    BOOL sameFolder = (IsDlgButtonChecked(hwnd, IDC_SAME_FOLDER_CHECK) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_OUTDIR_EDIT), !sameFolder);
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_BTN), !sameFolder);
}

INT_PTR CALLBACK ConvertDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
        auto* ctx = reinterpret_cast<DialogContext*>(lParam);

        // Header text
        wchar_t headerBuf[256];
        std::swprintf(headerBuf, 256, L"Selected: %zu track(s) to convert", static_cast<size_t>(ctx->items.get_count()));
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_STATIC, headerBuf);

        // Rate ComboBox
        HWND hRate = GetDlgItem(hwnd, IDC_RATE_COMBO);
        SendMessageW(hRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DSD64   (64x  -  2.8224 / 3.072 MHz)"));
        SendMessageW(hRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DSD128  (128x -  5.6448 / 6.144 MHz)"));
        SendMessageW(hRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DSD256  (256x - 11.2896 / 12.288 MHz) [Default]"));
        SendMessageW(hRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DSD512  (512x - 22.5792 / 24.576 MHz)"));
        int rateIdx = 2;
        if (ctx->settings.rate == 64) rateIdx = 0;
        else if (ctx->settings.rate == 128) rateIdx = 1;
        else if (ctx->settings.rate == 256) rateIdx = 2;
        else if (ctx->settings.rate == 512) rateIdx = 3;
        SendMessageW(hRate, CB_SETCURSEL, rateIdx, 0);

        // Base Clock Family ComboBox
        HWND hBase = GetDlgItem(hwnd, IDC_BASE_COMBO);
        SendMessageW(hBase, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (match source 44.1k / 48k family)"));
        SendMessageW(hBase, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"44100 Hz family (SACD standard)"));
        SendMessageW(hBase, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"48000 Hz family"));
        SendMessageW(hBase, CB_SETCURSEL, std::clamp(ctx->settings.baseMode, 0, 2), 0);

        // Format ComboBox
        HWND hFmt = GetDlgItem(hwnd, IDC_FORMAT_COMBO);
        SendMessageW(hFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DSF - Sony DSD Stream File (.dsf)"));
        SendMessageW(hFmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DFF - Philips DSDIFF (.dff)"));
        SendMessageW(hFmt, CB_SETCURSEL, std::clamp(ctx->settings.format, 0, 1), 0);

        // Order ComboBox (2..8)
        HWND hOrd = GetDlgItem(hwnd, IDC_ORDER_COMBO);
        for (int ord = 2; ord <= 8; ++ord) {
            wchar_t obuf[32];
            if (ord == 7) std::swprintf(obuf, 32, L"%dth (Default)", ord);
            else std::swprintf(obuf, 32, L"%dth order", ord);
            SendMessageW(hOrd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(obuf));
        }
        SendMessageW(hOrd, CB_SETCURSEL, std::clamp(ctx->settings.order, 2, 8) - 2, 0);

        // Gain
        std::wstring wGain = utf8ToWide(g_cfg_gain_str.get().c_str());
        if (wGain.empty()) wGain = L"0.0";
        SetDlgItemTextW(hwnd, IDC_GAIN_EDIT, wGain.c_str());

        // Checkboxes
        CheckDlgButton(hwnd, IDC_DITHER_CHECK, ctx->settings.dither > 0.0 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_COPY_TAGS_CHECK, ctx->settings.copyTags ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SAME_FOLDER_CHECK, ctx->settings.sameFolder ? BST_CHECKED : BST_UNCHECKED);

        // Output directory default
        std::string defaultOutDir = ctx->settings.outDir;
        if (defaultOutDir.empty() && ctx->items.get_count() > 0) {
            std::string stem;
            getTrackSourceFolderAndStem(ctx->items[0], defaultOutDir, stem);
        }
        SetDlgItemTextW(hwnd, IDC_OUTDIR_EDIT, utf8ToWide(defaultOutDir).c_str());
        updateOutDirState(hwnd);

        // Naming ComboBox
        HWND hNaming = GetDlgItem(hwnd, IDC_NAMING_COMBO);
        SendMessageW(hNaming, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Source filename (e.g. track.flac -> track.dsf)"));
        SendMessageW(hNaming, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tracknumber - Title (%tracknumber% - %title%)"));
        SendMessageW(hNaming, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Artist - Title (%artist% - %title%)"));
        SendMessageW(hNaming, CB_SETCURSEL, std::clamp(ctx->settings.namingMode, 0, 2), 0);

        return TRUE;
    }

    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        if (id == IDC_SAME_FOLDER_CHECK) {
            updateOutDirState(hwnd);
            return TRUE;
        }
        if (id == IDC_BROWSE_BTN) {
            wchar_t curPath[MAX_PATH * 2] = {};
            GetDlgItemTextW(hwnd, IDC_OUTDIR_EDIT, curPath, MAX_PATH * 2);
            std::wstring folder(curPath);
            if (browseForFolder(hwnd, folder)) {
                SetDlgItemTextW(hwnd, IDC_OUTDIR_EDIT, folder.c_str());
            }
            return TRUE;
        }
        if (id == IDOK) {
            auto* ctx = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            static const int kRates[4] = {64, 128, 256, 512};
            int rIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_RATE_COMBO, CB_GETCURSEL, 0, 0));
            ctx->settings.rate = kRates[std::clamp(rIdx, 0, 3)];
            ctx->settings.baseMode = std::clamp(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_BASE_COMBO, CB_GETCURSEL, 0, 0)), 0, 2);
            ctx->settings.format = std::clamp(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_FORMAT_COMBO, CB_GETCURSEL, 0, 0)), 0, 1);
            ctx->settings.order = std::clamp(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_ORDER_COMBO, CB_GETCURSEL, 0, 0)) + 2, 2, 8);

            wchar_t gainBuf[64] = {};
            GetDlgItemTextW(hwnd, IDC_GAIN_EDIT, gainBuf, 64);
            std::string gainUtf8 = wideToUtf8(gainBuf);
            ctx->settings.gainDb = std::atof(gainUtf8.c_str());

            ctx->settings.dither = (IsDlgButtonChecked(hwnd, IDC_DITHER_CHECK) == BST_CHECKED) ? 0.05 : 0.0;
            ctx->settings.copyTags = (IsDlgButtonChecked(hwnd, IDC_COPY_TAGS_CHECK) == BST_CHECKED);
            ctx->settings.sameFolder = (IsDlgButtonChecked(hwnd, IDC_SAME_FOLDER_CHECK) == BST_CHECKED);
            ctx->settings.namingMode = std::clamp(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_NAMING_COMBO, CB_GETCURSEL, 0, 0)), 0, 2);

            wchar_t outDirBuf[MAX_PATH * 2] = {};
            GetDlgItemTextW(hwnd, IDC_OUTDIR_EDIT, outDirBuf, MAX_PATH * 2);
            ctx->settings.outDir = wideToUtf8(outDirBuf);

            if (!ctx->settings.sameFolder && ctx->settings.outDir.empty()) {
                MessageBoxW(hwnd, L"Please specify an output folder or check 'Save in the same folder as each source track'.",
                            L"Convert to DSD (deltarec)", MB_ICONWARNING | MB_OK);
                return TRUE;
            }

            // Persist settings
            g_cfg_rate = ctx->settings.rate;
            g_cfg_base_mode = ctx->settings.baseMode;
            g_cfg_format = ctx->settings.format;
            g_cfg_order = ctx->settings.order;
            g_cfg_gain_str = gainUtf8.c_str();
            g_cfg_dither = (ctx->settings.dither > 0.0) ? 1 : 0;
            g_cfg_copy_tags = ctx->settings.copyTags ? 1 : 0;
            g_cfg_same_folder = ctx->settings.sameFolder ? 1 : 0;
            if (!ctx->settings.outDir.empty()) g_cfg_out_dir = ctx->settings.outDir.c_str();
            g_cfg_naming = ctx->settings.namingMode;

            ctx->accepted = true;
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ---- Context Menu Item ---------------------------------------------------
class DeltarecContextMenu : public contextmenu_item_simple {
public:
    unsigned get_num_items() override { return 1; }

    void get_item_name(unsigned p_index, pfc::string_base& p_out) override {
        if (p_index == 0) p_out = "Convert to DSD (deltarec)...";
    }

    GUID get_item_guid(unsigned p_index) override {
        // {9E73A120-5B4C-4D92-8F10-123456789ABC}
        static const GUID guid_cmd = { 0x9e73a120, 0x5b4c, 0x4d92, { 0x8f, 0x10, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc } };
        return guid_cmd;
    }

    bool get_item_description(unsigned p_index, pfc::string_base& p_out) override {
        if (p_index == 0) {
            p_out = "Converts the selected tracks to 1-bit DSD (DSF / DSDIFF) using deltarec.";
            return true;
        }
        return false;
    }

    void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) override {
        if (p_index != 0 || p_data.get_count() == 0) return;

        DialogContext ctx;
        ctx.items = p_data;
        ctx.settings.rate = static_cast<int>(g_cfg_rate.get_value());
        ctx.settings.baseMode = static_cast<int>(g_cfg_base_mode.get_value());
        ctx.settings.format = static_cast<int>(g_cfg_format.get_value());
        ctx.settings.order = static_cast<int>(g_cfg_order.get_value());
        ctx.settings.gainDb = std::atof(g_cfg_gain_str.get().c_str());
        ctx.settings.dither = g_cfg_dither.get_value() ? 0.05 : 0.0;
        ctx.settings.copyTags = g_cfg_copy_tags.get_value() != 0;
        ctx.settings.sameFolder = g_cfg_same_folder.get_value() != 0;
        ctx.settings.outDir = g_cfg_out_dir.get().c_str();
        ctx.settings.namingMode = static_cast<int>(g_cfg_naming.get_value());

        HWND parent = core_api::get_main_window();
        DialogBoxParamW(
            core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDD_DELTAREC_DIALOG),
            parent,
            ConvertDlgProc,
            reinterpret_cast<LPARAM>(&ctx)
        );

        if (!ctx.accepted) return;

        service_ptr_t<threaded_process_callback> worker =
            new service_impl_t<DsdConvertWorker>(p_data, ctx.settings);

        const unsigned flags =
            threaded_process::flag_show_progress |
            threaded_process::flag_show_abort |
            threaded_process::flag_show_item;

        threaded_process::g_run_modeless(worker, flags, parent, "Converting to DSD (deltarec)...");
    }
};

static contextmenu_item_factory_t<DeltarecContextMenu> g_deltarec_contextmenu_factory;

}  // namespace
