#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// ===========================================================================
//  Compile-time string obfuscation. Sensitive literals (server URLs, product
//  ids, user-facing security strings) are XOR-encrypted in the binary and
//  only decrypted on demand, so a plain `strings` dump reveals nothing useful.
//  This is a deterrent layer, not unbreakable crypto.
// ===========================================================================
namespace wsho::obf
{
constexpr uint8_t keyAt (std::size_t i) { return (uint8_t) (0x5Au ^ (uint8_t) (i * 131u + 17u)); }

template <std::size_t N>
struct Obfuscated
{
    std::array<char, N> data {};

    constexpr Obfuscated (const char (&in)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
            data[i] = (char) ((uint8_t) in[i] ^ keyAt (i));
    }

    std::string reveal() const
    {
        std::string s;
        s.resize (N - 1);
        for (std::size_t i = 0; i < N - 1; ++i)
            s[i] = (char) ((uint8_t) data[i] ^ keyAt (i));
        return s;
    }
};

} // namespace wsho::obf

// Usage: WSHO_OBF("secret")  -> std::string at runtime, obfuscated at rest.
#define WSHO_OBF(str) ([]{ constexpr wsho::obf::Obfuscated<sizeof(str)> o(str); return o; }().reveal())
