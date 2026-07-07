#include "ImageReader.hpp"
#include <fstream>
#include <cstring>

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

ImageDimensions get_image_dimensions(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return {};

    unsigned char magic[3];
    if (!file.read(reinterpret_cast<char*>(magic), 3)) return {};

    file.clear();
    file.seekg(0);

    if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N')
        return read_png_dimensions(file);

    if (magic[0] == 0xFF && magic[1] == 0xD8)
        return read_jpeg_dimensions(file);

    return {};
}
