// id3v2_builder.hpp - Builds an ID3v2.3 tag (UTF-16LE + BOM) from foobar2000
// file_info and optional album art for embedding into DSF and DFF files.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <SDK/foobar2000.h>

namespace deltarec {

namespace id3detail {

inline std::string toUpperAscii(const char* s) {
    std::string out;
    if (!s) return out;
    for (; *s; ++s) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(*s))));
    return out;
}

inline void appendU32BE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline void appendSynchsafe32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 21) & 0x7F));
    buf.push_back(static_cast<uint8_t>((v >> 14) & 0x7F));
    buf.push_back(static_cast<uint8_t>((v >> 7) & 0x7F));
    buf.push_back(static_cast<uint8_t>(v & 0x7F));
}

// Appends UTF-16LE with BOM (0xFF 0xFE) for a UTF-8 string (without trailing null).
inline void appendUtf16WithBom(std::vector<uint8_t>& buf, const std::string& utf8) {
    buf.push_back(0xFF);
    buf.push_back(0xFE);
    if (utf8.empty()) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return;
    std::vector<wchar_t> wbuf(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wbuf.data(), wlen);
    for (wchar_t wc : wbuf) {
        uint16_t u = static_cast<uint16_t>(wc);
        buf.push_back(static_cast<uint8_t>(u & 0xFF));
        buf.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    }
}

inline void appendFrame(std::vector<uint8_t>& frames, const char id[4], const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;
    frames.insert(frames.end(), id, id + 4);
    appendU32BE(frames, static_cast<uint32_t>(payload.size()));
    frames.push_back(0x00);
    frames.push_back(0x00);
    frames.insert(frames.end(), payload.begin(), payload.end());
}

inline void addTextFrame(std::vector<uint8_t>& frames, const char id[4], const std::string& text) {
    if (text.empty()) return;
    std::vector<uint8_t> payload;
    payload.push_back(0x01);  // UTF-16 with BOM
    appendUtf16WithBom(payload, text);
    appendFrame(frames, id, payload);
}

inline void addTxxxFrame(std::vector<uint8_t>& frames, const std::string& desc, const std::string& val) {
    if (desc.empty() || val.empty()) return;
    std::vector<uint8_t> payload;
    payload.push_back(0x01);  // UTF-16 with BOM
    appendUtf16WithBom(payload, desc);
    payload.push_back(0x00);
    payload.push_back(0x00);
    appendUtf16WithBom(payload, val);
    appendFrame(frames, "TXXX", payload);
}

inline void addCommOrUsltFrame(std::vector<uint8_t>& frames, const char id[4], const std::string& text) {
    if (text.empty()) return;
    std::vector<uint8_t> payload;
    payload.push_back(0x01);  // UTF-16 with BOM
    payload.push_back('e');
    payload.push_back('n');
    payload.push_back('g');
    // Empty content descriptor with BOM + UTF-16 null terminator
    payload.push_back(0xFF);
    payload.push_back(0xFE);
    payload.push_back(0x00);
    payload.push_back(0x00);
    appendUtf16WithBom(payload, text);
    appendFrame(frames, id, payload);
}

inline void addApicFrame(std::vector<uint8_t>& frames, const void* imgData, size_t imgSize) {
    if (!imgData || imgSize < 4) return;
    const auto* b = static_cast<const uint8_t*>(imgData);
    const char* mime = "image/jpeg";
    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') mime = "image/png";
    else if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F') mime = "image/gif";
    else if (b[0] == 'B' && b[1] == 'M') mime = "image/bmp";

    std::vector<uint8_t> payload;
    payload.reserve(32 + imgSize);
    payload.push_back(0x00);  // ISO-8859-1 description encoding
    payload.insert(payload.end(), mime, mime + std::strlen(mime) + 1);  // null-terminated MIME
    payload.push_back(0x03);  // Picture type: Cover (front)
    payload.push_back(0x00);  // Empty description (null-terminated)
    payload.insert(payload.end(), b, b + imgSize);
    appendFrame(frames, "APIC", payload);
}

inline std::string joinMetaValues(const file_info& info, t_size idx) {
    t_size count = info.meta_enum_value_count(idx);
    std::string out;
    for (t_size j = 0; j < count; ++j) {
        const char* v = info.meta_enum_value(idx, j);
        if (!v || !*v) continue;
        if (!out.empty()) out += "; ";
        out += v;
    }
    return out;
}

inline std::string getMetaByName(const file_info& info, const char* name) {
    t_size idx = info.meta_find(name);
    if (idx == pfc::infinite_size) return {};
    return joinMetaValues(info, idx);
}

}  // namespace id3detail

// Builds an ID3v2.3 byte array from `info` and optional front cover art `coverArt`.
inline std::vector<uint8_t> buildId3v2Tag(const file_info& info, album_art_data_ptr coverArt = nullptr) {
    using namespace id3detail;
    std::vector<uint8_t> frames;

    // Combined TRACKNUMBER/TOTALTRACKS and DISCNUMBER/TOTALDISCS
    std::string trk = getMetaByName(info, "TRACKNUMBER");
    std::string totTrk = getMetaByName(info, "TOTALTRACKS");
    if (!trk.empty()) {
        addTextFrame(frames, "TRCK", !totTrk.empty() ? (trk + "/" + totTrk) : trk);
    }

    std::string dsc = getMetaByName(info, "DISCNUMBER");
    std::string totDsc = getMetaByName(info, "TOTALDISCS");
    if (!dsc.empty()) {
        addTextFrame(frames, "TPOS", !totDsc.empty() ? (dsc + "/" + totDsc) : dsc);
    }

    const t_size metaCount = info.meta_get_count();
    for (t_size i = 0; i < metaCount; ++i) {
        std::string name = toUpperAscii(info.meta_enum_name(i));
        std::string val = joinMetaValues(info, i);
        if (val.empty()) continue;

        if (name == "TRACKNUMBER" || name == "TOTALTRACKS" ||
            name == "DISCNUMBER" || name == "TOTALDISCS") {
            continue;  // already handled above
        } else if (name == "TITLE") {
            addTextFrame(frames, "TIT2", val);
        } else if (name == "ARTIST") {
            addTextFrame(frames, "TPE1", val);
        } else if (name == "ALBUM") {
            addTextFrame(frames, "TALB", val);
        } else if (name == "ALBUM ARTIST" || name == "ALBUMARTIST") {
            addTextFrame(frames, "TPE2", val);
        } else if (name == "DATE" || name == "YEAR") {
            addTextFrame(frames, "TYER", val);
        } else if (name == "GENRE") {
            addTextFrame(frames, "TCON", val);
        } else if (name == "COMPOSER") {
            addTextFrame(frames, "TCOM", val);
        } else if (name == "CONDUCTOR") {
            addTextFrame(frames, "TPE3", val);
        } else if (name == "PERFORMER" || name == "REMIXER") {
            addTextFrame(frames, "TPE4", val);
        } else if (name == "COPYRIGHT") {
            addTextFrame(frames, "TCOP", val);
        } else if (name == "PUBLISHER" || name == "LABEL" || name == "ORGANIZATION") {
            addTextFrame(frames, "TPUB", val);
        } else if (name == "ISRC") {
            addTextFrame(frames, "TSRC", val);
        } else if (name == "BPM" || name == "TEMPO") {
            addTextFrame(frames, "TBPM", val);
        } else if (name == "COMMENT") {
            addCommOrUsltFrame(frames, "COMM", val);
        } else if (name == "LYRICS" || name == "UNSYNCED LYRICS" || name == "UNSYNCEDLYRICS") {
            addCommOrUsltFrame(frames, "USLT", val);
        } else {
            addTxxxFrame(frames, info.meta_enum_name(i), val);
        }
    }

    if (coverArt.is_valid() && coverArt->get_size() > 0) {
        addApicFrame(frames, coverArt->get_ptr(), coverArt->get_size());
    }

    if (frames.empty()) return {};

    std::vector<uint8_t> tag;
    tag.reserve(10 + frames.size());
    tag.push_back('I');
    tag.push_back('D');
    tag.push_back('3');
    tag.push_back(0x03);  // ID3v2.3
    tag.push_back(0x00);  // revision 0
    tag.push_back(0x00);  // flags
    appendSynchsafe32(tag, static_cast<uint32_t>(frames.size()));
    tag.insert(tag.end(), frames.begin(), frames.end());
    return tag;
}

}  // namespace deltarec
