#include "Sha256.hpp"

#include <algorithm>
#include <cstring>

namespace digitalforge::core {

namespace {


constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

[[nodiscard]] constexpr uint32_t rotateRight(uint32_t value, uint32_t count) noexcept {
    return (value >> count) | (value << (32 - count));
}

void compressBlock(const uint8_t* block, std::array<uint32_t, 8>& state) {
    std::array<uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

std::string Hash256::toHex() const {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 0x0F]);
    }
    return hex;
}

bool Hash256::fromHex(std::string_view hex, Hash256& out) {
    if (hex.size() != 64) {
        return false;
    }
    const auto nibble = [](char c, uint8_t& value) {
        if (c >= '0' && c <= '9') {
            value = static_cast<uint8_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value = static_cast<uint8_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value = static_cast<uint8_t>(c - 'A' + 10);
        } else {
            return false;
        }
        return true;
    };

    Hash256 parsed;
    for (std::size_t i = 0; i < parsed.bytes.size(); ++i) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!nibble(hex[i * 2], high) || !nibble(hex[i * 2 + 1], low)) {
            return false;
        }
        parsed.bytes[i] = static_cast<uint8_t>((high << 4) | low);
    }
    out = parsed;
    return true;
}

bool Hash256::isNull() const noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte == 0; });
}

Hash256 sha256(std::string_view data) {
    std::array<uint32_t, 8> state = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
    const std::size_t length = data.size();

    std::size_t offset = 0;
    for (; offset + 64 <= length; offset += 64) {
        compressBlock(bytes + offset, state);
    }

    // Relleno: 0x80, ceros hasta dejar 8 bytes libres, y la longitud original
    // en bits como big-endian de 64 bits.
    std::array<uint8_t, 128> tail{};
    const std::size_t remaining = length - offset;
    if (remaining > 0) {
        std::memcpy(tail.data(), bytes + offset, remaining);
    }
    tail[remaining] = 0x80;
    const std::size_t tailBlocks = remaining + 1 + 8 > 64 ? 2 : 1;
    const std::size_t tailSize = tailBlocks * 64;
    const uint64_t bitLength = static_cast<uint64_t>(length) * 8;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[tailSize - 1 - i] = static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF);
    }
    for (std::size_t block = 0; block < tailBlocks; ++block) {
        compressBlock(tail.data() + block * 64, state);
    }

    Hash256 result;
    for (std::size_t i = 0; i < state.size(); ++i) {
        result.bytes[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
        result.bytes[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
        result.bytes[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
        result.bytes[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
    }
    return result;
}

Sha256Builder& Sha256Builder::field(std::string_view value) {
    // Prefijo de longitud + separador: sin esto, hashear {"ab","c"} y
    // {"a","bc"} daria el mismo digesto y dos interfaces distintas podrian
    // parecer iguales.
    canonical_ += std::to_string(value.size());
    canonical_ += ':';
    canonical_.append(value);
    canonical_ += '\x1F';
    return *this;
}

Sha256Builder& Sha256Builder::field(uint64_t value) { return field(std::to_string(value)); }

Sha256Builder& Sha256Builder::field(bool value) { return field(std::string_view(value ? "true" : "false")); }

Hash256 Sha256Builder::finish() const { return sha256(canonical_); }

} // namespace digitalforge::core
