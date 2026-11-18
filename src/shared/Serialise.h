#pragma once
// Hand-rolled binary serialisation (section 11): little-endian fields, variable-length integers for counts.
// Written once here and used for both network messages and (later) region files.
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vrtc {
    class ByteWriter {
        std::vector<uint8_t> buffer;

    public:
        void Clear() { buffer.clear(); }

        [[nodiscard]] std::span<const uint8_t> Bytes() const { return buffer; }

        [[nodiscard]] size_t Size() const { return buffer.size(); }

        std::vector<uint8_t> &Buffer() { return buffer; }

        void U8(uint8_t v) { buffer.push_back(v); }

        void U16(uint16_t v) {
            buffer.push_back(static_cast<uint8_t>(v));
            buffer.push_back(static_cast<uint8_t>(v >> 8));
        }

        void U32(uint32_t v) {
            for (int i = 0; i < 4; ++i) buffer.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }

        void U64(uint64_t v) {
            for (int i = 0; i < 8; ++i) buffer.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }

        void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }

        void F32(float v) { U32(std::bit_cast<uint32_t>(v)); }

        void F64(double v) { U64(std::bit_cast<uint64_t>(v)); }

        void VarU32(uint32_t v) {
            while (v >= 0x80) {
                buffer.push_back(static_cast<uint8_t>(v | 0x80));
                v >>= 7;
            }
            buffer.push_back(static_cast<uint8_t>(v));
        }

        void Raw(std::span<const uint8_t> bytes) { buffer.insert(buffer.end(), bytes.begin(), bytes.end()); }

        // Length-prefixed.
        void Str(std::string_view s) {
            VarU32(static_cast<uint32_t>(s.size()));
            buffer.insert(buffer.end(), s.begin(), s.end());
        }

        // Count is not written; the reader must know it.
        void U16Array(std::span<const uint16_t> values) {
            const size_t at = buffer.size();
            buffer.resize(at + values.size_bytes());
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(buffer.data() + at, values.data(), values.size_bytes());
            } else {
                uint8_t *out = buffer.data() + at;
                for (const uint16_t v: values) {
                    *out++ = static_cast<uint8_t>(v);
                    *out++ = static_cast<uint8_t>(v >> 8);
                }
            }
        }
    };

    // Reads never throw: a short or malformed buffer sets Ok() false and returns zeros.
    class ByteReader {
        std::span<const uint8_t> data;
        size_t pos = 0;
        bool ok = true;

        bool Need(size_t n) {
            if (pos + n > data.size()) {
                ok = false;
                return false;
            }
            return true;
        }

    public:
        explicit ByteReader(std::span<const uint8_t> bytes) : data(bytes) {
        }

        [[nodiscard]] bool Ok() const { return ok; }

        [[nodiscard]] size_t Remaining() const { return data.size() - pos; }

        uint8_t U8() {
            if (!Need(1)) return 0;
            return data[pos++];
        }

        uint16_t U16() {
            if (!Need(2)) return 0;
            const uint16_t v = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
            pos += 2;
            return v;
        }

        uint32_t U32() {
            if (!Need(4)) return 0;
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
            pos += 4;
            return v;
        }

        uint64_t U64() {
            if (!Need(8)) return 0;
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
            pos += 8;
            return v;
        }

        int32_t I32() { return static_cast<int32_t>(U32()); }

        float F32() { return std::bit_cast<float>(U32()); }

        double F64() { return std::bit_cast<double>(U64()); }

        uint32_t VarU32() {
            uint32_t v = 0;
            for (int shift = 0; shift < 35; shift += 7) {
                if (!Need(1)) return 0;
                const uint8_t b = data[pos++];
                v |= static_cast<uint32_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) return v;
            }
            ok = false;
            return 0;
        }

        std::span<const uint8_t> Take(size_t n) {
            if (!Need(n)) return {};
            const auto out = data.subspan(pos, n);
            pos += n;
            return out;
        }

        std::string Str() {
            const uint32_t n = VarU32();
            if (!Need(n)) return {};
            std::string s(reinterpret_cast<const char *>(data.data() + pos), n);
            pos += n;
            return s;
        }

        bool U16Array(std::span<uint16_t> out) {
            if (!Need(out.size_bytes())) return false;
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(out.data(), data.data() + pos, out.size_bytes());
            } else {
                for (uint16_t &v: out) {
                    v = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
                    pos += 2;
                }
                return true;
            }
            pos += out.size_bytes();
            return true;
        }
    };
}
