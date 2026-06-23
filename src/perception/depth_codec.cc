#include "perception/depth_codec.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <zlib.h>

#include "core/log.h"

namespace ros2_android {

// ============================================================================
// compressedDepth ConfigHeader (compressed_depth_image_transport format)
// ============================================================================

struct ConfigHeader {
    int32_t format;            // 0 = PNG, 1 = RVL
    float   depth_quantization;
    float   depth_max;
};
static_assert(sizeof(ConfigHeader) == 12, "ConfigHeader must be 12 bytes");

static constexpr int32_t kFormatPNG = 0;

// ============================================================================
// Minimal PNG parser helpers (big-endian reads, chunk iteration)
// ============================================================================

static inline uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
static inline uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static const uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

// ============================================================================
// PNG filter reconstruction (operates on bytes, not pixels)
// Implements all 5 PNG filter types for 16-bit grayscale images.
// ============================================================================

static uint8_t paeth_predictor(int a, int b, int c) {
    int p  = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc)             return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

// Reconstruct one scanline in-place.
// raw_row: pointer to the filter byte + pixel bytes for this row (length 1 + bytes_per_row)
// prev_row: pointer to the pixel bytes of the previous row (no filter byte), or nullptr for row 0
// bytes_per_row: width * 2 (2 bytes per 16-bit grayscale pixel)
// bpp: bytes per pixel = 2
static bool reconstruct_row(uint8_t* raw_row, const uint8_t* prev_row,
                             uint32_t bytes_per_row) {
    const uint8_t filter = raw_row[0];
    uint8_t* row = raw_row + 1;  // pixel data starts after filter byte
    constexpr int bpp = 2;       // 16-bit grayscale → 2 bytes per pixel

    switch (filter) {
        case 0:  // None
            break;

        case 1:  // Sub: raw[i] += raw[i - bpp]
            for (uint32_t i = bpp; i < bytes_per_row; ++i) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - bpp]);
            }
            break;

        case 2:  // Up: raw[i] += prior[i]
            if (!prev_row) break;
            for (uint32_t i = 0; i < bytes_per_row; ++i) {
                row[i] = static_cast<uint8_t>(row[i] + prev_row[i]);
            }
            break;

        case 3:  // Average: raw[i] += floor((raw[i-bpp] + prior[i]) / 2)
            for (uint32_t i = 0; i < bytes_per_row; ++i) {
                int left  = (i >= static_cast<uint32_t>(bpp)) ? row[i - bpp] : 0;
                int above = prev_row ? prev_row[i] : 0;
                row[i] = static_cast<uint8_t>(row[i] + ((left + above) >> 1));
            }
            break;

        case 4:  // Paeth
            for (uint32_t i = 0; i < bytes_per_row; ++i) {
                int left      = (i >= static_cast<uint32_t>(bpp)) ? row[i - bpp]      : 0;
                int above     = prev_row ? prev_row[i]             : 0;
                int upper_left = (prev_row && i >= static_cast<uint32_t>(bpp)) ? prev_row[i - bpp] : 0;
                row[i] = static_cast<uint8_t>(row[i] + paeth_predictor(left, above, upper_left));
            }
            break;

        default:
            LOGW("DecompressDepth: unknown PNG filter type %d", filter);
            return false;
    }
    return true;
}

// ============================================================================
// Public API
// ============================================================================

sensor_msgs::msg::Image::UniquePtr DecompressDepth(
    const sensor_msgs::msg::CompressedImage& msg) {

    const uint8_t* data = msg.data.data();
    size_t         size = msg.data.size();

    // -------------------------------------------------------------------------
    // 1. Parse ConfigHeader
    // -------------------------------------------------------------------------
    if (size < sizeof(ConfigHeader)) {
        LOGW("DecompressDepth: message too small for ConfigHeader (%zu bytes)", size);
        return nullptr;
    }
    ConfigHeader hdr;
    std::memcpy(&hdr, data, sizeof(ConfigHeader));
    data += sizeof(ConfigHeader);
    size -= sizeof(ConfigHeader);

    if (hdr.format != kFormatPNG) {
        LOGW("DecompressDepth: unsupported format %d (only PNG=0 supported)", hdr.format);
        return nullptr;
    }
    if (hdr.depth_quantization <= 0.0f) {
        LOGW("DecompressDepth: invalid depth_quantization %.3f", hdr.depth_quantization);
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // 2. Verify PNG signature
    // -------------------------------------------------------------------------
    if (size < 8 || std::memcmp(data, kPngSignature, 8) != 0) {
        LOGW("DecompressDepth: missing PNG signature");
        return nullptr;
    }
    data += 8;
    size -= 8;

    // -------------------------------------------------------------------------
    // 3. Parse PNG chunks: extract IHDR (width/height) and IDAT (compressed rows)
    // -------------------------------------------------------------------------
    uint32_t img_width  = 0;
    uint32_t img_height = 0;
    uint8_t  bit_depth  = 0;
    std::vector<uint8_t> idat_combined;

    while (size >= 12) {  // min chunk = 4 (len) + 4 (type) + 0 (data) + 4 (crc)
        uint32_t chunk_len  = be32(data);
        uint8_t  chunk_type[4];
        std::memcpy(chunk_type, data + 4, 4);
        const uint8_t* chunk_data = data + 8;

        if (size < 12 + chunk_len) {
            LOGW("DecompressDepth: truncated PNG chunk");
            break;
        }

        if (std::memcmp(chunk_type, "IHDR", 4) == 0 && chunk_len >= 13) {
            img_width  = be32(chunk_data);
            img_height = be32(chunk_data + 4);
            bit_depth  = chunk_data[8];
            uint8_t color_type = chunk_data[9];
            if (bit_depth != 16 || color_type != 0) {
                LOGW("DecompressDepth: unexpected PNG format bit_depth=%d color_type=%d "
                     "(expected 16-bit grayscale)", bit_depth, color_type);
                return nullptr;
            }
        } else if (std::memcmp(chunk_type, "IDAT", 4) == 0) {
            idat_combined.insert(idat_combined.end(), chunk_data, chunk_data + chunk_len);
        } else if (std::memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }

        data += 12 + chunk_len;
        size -= 12 + chunk_len;
    }

    if (img_width == 0 || img_height == 0 || idat_combined.empty()) {
        LOGW("DecompressDepth: IHDR or IDAT missing (w=%u h=%u idat=%zu)",
             img_width, img_height, idat_combined.size());
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // 4. Decompress IDAT with zlib
    //    Filtered output: height * (1 + 2*width) bytes (filter byte + 16-bit pixels)
    // -------------------------------------------------------------------------
    const uint32_t bytes_per_row  = img_width * 2;
    const uint32_t stride         = 1 + bytes_per_row;  // filter byte + pixels
    uLongf         filtered_size  = static_cast<uLongf>(img_height) * stride;

    std::vector<uint8_t> filtered(filtered_size);
    int z_result = uncompress(filtered.data(), &filtered_size,
                              idat_combined.data(), idat_combined.size());
    if (z_result != Z_OK) {
        LOGW("DecompressDepth: zlib uncompress failed: %d", z_result);
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // 5. Apply PNG filter reconstruction row by row
    // -------------------------------------------------------------------------
    for (uint32_t row = 0; row < img_height; ++row) {
        uint8_t* raw_row  = filtered.data() + row * stride;
        const uint8_t* prev = (row > 0) ? (filtered.data() + (row - 1) * stride + 1) : nullptr;
        if (!reconstruct_row(raw_row, prev, bytes_per_row)) {
            return nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // 6. Convert 16UC1 big-endian to 32FC1 (meters)
    //    pixel == 0 → NaN; otherwise depth_m = pixel / depth_quantization
    // -------------------------------------------------------------------------
    auto out = std::make_unique<sensor_msgs::msg::Image>();
    out->header   = msg.header;
    out->width    = img_width;
    out->height   = img_height;
    out->encoding = "32FC1";
    out->step     = img_width * sizeof(float);
    out->is_bigendian = false;
    out->data.resize(img_width * img_height * sizeof(float));

    float* dst = reinterpret_cast<float*>(out->data.data());
    for (uint32_t row = 0; row < img_height; ++row) {
        const uint8_t* src = filtered.data() + row * stride + 1;  // skip filter byte
        for (uint32_t col = 0; col < img_width; ++col) {
            uint16_t pixel = be16(src + col * 2);
            if (pixel == 0) {
                dst[row * img_width + col] = std::numeric_limits<float>::quiet_NaN();
            } else {
                dst[row * img_width + col] = pixel / hdr.depth_quantization;
            }
        }
    }

    LOGD("DecompressDepth: %ux%u depth image (quant=%.1f max=%.1f)",
         img_width, img_height, hdr.depth_quantization, hdr.depth_max);

    return out;
}

}  // namespace ros2_android
