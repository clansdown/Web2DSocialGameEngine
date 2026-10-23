#include "ImageReader.hpp"
#include <fstream>
#include <cstring>
#include <cstdint>
#include <vector>

static ImageDimensions read_png_dimensions(std::ifstream& file) {
    unsigned char header[24];
    if (!file.read(reinterpret_cast<char*>(header), 24)) return {};

    const unsigned char png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(header, png_sig, 8) != 0) return {};
    if (header[12] != 'I' || header[13] != 'H' || header[14] != 'D' || header[15] != 'R') return {};

    auto read_be32 = [&](int i) -> int {
        return (static_cast<int>(header[i]) << 24)
             | (static_cast<int>(header[i + 1]) << 16)
             | (static_cast<int>(header[i + 2]) << 8)
             | static_cast<int>(header[i + 3]);
    };

    return { read_be32(16), read_be32(20) };
}

static ImageDimensions read_jpeg_dimensions(std::ifstream& file) {
    unsigned char soi[2];
    if (!file.read(reinterpret_cast<char*>(soi), 2)) return {};
    if (soi[0] != 0xFF || soi[1] != 0xD8) return {};

    while (true) {
        unsigned char ff;
        if (!file.read(reinterpret_cast<char*>(&ff), 1)) return {};
        if (ff != 0xFF) return {}; // each marker starts with FF

        unsigned char marker;
        if (!file.read(reinterpret_cast<char*>(&marker), 1)) return {};

        // SOF0, SOF1, SOF2 all carry dimensions at the same offset
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            unsigned char len[2];
            if (!file.read(reinterpret_cast<char*>(len), 2)) return {};
            // skip segment length, then precision(1) + height(2) + width(2)
            unsigned char dims[5];
            if (!file.read(reinterpret_cast<char*>(dims), 5)) return {};

            int h = (static_cast<int>(dims[1]) << 8) | static_cast<int>(dims[2]);
            int w = (static_cast<int>(dims[3]) << 8) | static_cast<int>(dims[4]);
            return { w, h };
        }

        // EOI (end of image) or SOS (start of scan) — no SOF found
        if (marker == 0xD9 || marker == 0xDA) return {};

        // Markers D0–D7 and marker 01 have no segment length
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) continue;

        // All other markers have a 2-byte length (including the 2 bytes)
        unsigned char seg_len[2];
        if (!file.read(reinterpret_cast<char*>(seg_len), 2)) return {};
        int skip = ((static_cast<int>(seg_len[0]) << 8) | static_cast<int>(seg_len[1])) - 2;
        if (skip < 0) return {};
        file.seekg(skip, std::ios::cur);
    }
}

// WebP: RIFF container with a VP8/VP8L/VP8X chunk. Dimensions live in the chunk
// header (VP8X) or the frame header (VP8 lossy / VP8L lossless).
static ImageDimensions read_webp_dimensions(std::ifstream& file) {
    unsigned char header[30];
    if (!file.read(reinterpret_cast<char*>(header), 30)) return {};
    if (std::memcmp(header, "RIFF", 4) != 0) return {};
    if (std::memcmp(header + 8, "WEBP", 4) != 0) return {};

    const unsigned char* fourcc = header + 12; // chunk fourCC (data starts at 20)
    if (std::memcmp(fourcc, "VP8X", 4) == 0) {
        // Extended format: 24-bit LE canvas width-1 / height-1 at data offset 4.
        int w = 1 + (header[24] | (header[25] << 8) | (header[26] << 16));
        int h = 1 + (header[27] | (header[28] << 8) | (header[29] << 16));
        return { w, h };
    }
    if (std::memcmp(fourcc, "VP8 ", 4) == 0) {
        // Lossy: 3-byte frame tag, 3-byte start code, then 14-bit LE dims.
        if (header[23] != 0x9D || header[24] != 0x01 || header[25] != 0x2A) return {};
        int w = (header[26] | (header[27] << 8)) & 0x3FFF;
        int h = (header[28] | (header[29] << 8)) & 0x3FFF;
        return { w, h };
    }
    if (std::memcmp(fourcc, "VP8L", 4) == 0) {
        // Lossless: 0x2F signature then a packed 32-bit LE bitfield.
        if (header[20] != 0x2F) return {};
        const uint32_t bits = static_cast<uint32_t>(header[21])
                            | (static_cast<uint32_t>(header[22]) << 8)
                            | (static_cast<uint32_t>(header[23]) << 16)
                            | (static_cast<uint32_t>(header[24]) << 24);
        int w = static_cast<int>(bits & 0x3FFF) + 1;
        int h = static_cast<int>((bits >> 14) & 0x3FFF) + 1;
        return { w, h };
    }
    return {};
}

static uint32_t read_be32_at(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// AVIF/HEIF: ISOBMFF boxes. The primary image size lives in an `ispe` box under
// meta > iprp > ipco. Collect the largest ispe (thumbnails may also declare
// one) so a >256 guard uses the real dimensions.
static void collect_ispe(const unsigned char* data, size_t size, ImageDimensions& best) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        uint64_t box_size = read_be32_at(data + pos);
        const unsigned char* type = data + pos + 4;
        size_t header = 8;
        if (box_size == 1) {
            if (pos + 16 > size) return;
            box_size = (static_cast<uint64_t>(read_be32_at(data + pos + 8)) << 32)
                     | static_cast<uint64_t>(read_be32_at(data + pos + 12));
            header = 16;
        } else if (box_size == 0) {
            box_size = size - pos;
        }
        if (box_size < header || pos + box_size > size) return;

        if (std::memcmp(type, "ispe", 4) == 0) {
            if (header + 12 <= box_size) {
                int w = static_cast<int>(read_be32_at(data + pos + header + 4));
                int h = static_cast<int>(read_be32_at(data + pos + header + 8));
                if (w > 0 && h > 0 &&
                    static_cast<int64_t>(w) * h >
                        static_cast<int64_t>(best.width) * best.height) {
                    best.width = w;
                    best.height = h;
                }
            }
        } else if (std::memcmp(type, "meta", 4) == 0) {
            // `meta` is a FullBox: skip 4 bytes of version/flags before children.
            if (header + 4 <= box_size) {
                collect_ispe(data + pos + header + 4, box_size - header - 4, best);
            }
        } else if (std::memcmp(type, "iprp", 4) == 0 ||
                   std::memcmp(type, "ipco", 4) == 0) {
            collect_ispe(data + pos + header, box_size - header, best);
        }
        pos += box_size;
    }
}

static ImageDimensions read_avif_dimensions(std::ifstream& file) {
    file.clear();
    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (file_size <= 0 || file_size > 64 * 1024 * 1024) return {};
    std::vector<unsigned char> buf(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(file_size))) return {};
    ImageDimensions best;
    collect_ispe(buf.data(), buf.size(), best);
    return best;
}

ImageDimensions get_image_dimensions(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return {};

    unsigned char magic[12];
    if (!file.read(reinterpret_cast<char*>(magic), 12)) return {};

    file.clear();
    file.seekg(0);

    if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N')
        return read_png_dimensions(file);

    if (magic[0] == 0xFF && magic[1] == 0xD8)
        return read_jpeg_dimensions(file);

    if (std::memcmp(magic, "RIFF", 4) == 0 && std::memcmp(magic + 8, "WEBP", 4) == 0)
        return read_webp_dimensions(file);

    if (std::memcmp(magic + 4, "ftyp", 4) == 0)
        return read_avif_dimensions(file);

    return {};
}
