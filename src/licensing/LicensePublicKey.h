#pragma once
#include <array>
#include <cstdint>

// Ed25519 PUBLIC key that whysho.com's license answers are verified against.
// The matching private key exists only in the website's environment
// (LICENSE_SIGNING_KEY). To rotate: run `npm run license:keygen` in the
// whysho.com repo, set the new LICENSE_SIGNING_KEY on the host, paste the
// printed array here, and release a new plugin build. Builds with the old key
// stop accepting answers once the site signs with the new one.
namespace wsho::lic
{
inline constexpr std::array<uint8_t, 32> kLicensePublicKey = {
    0x07, 0x33, 0x4D, 0x67, 0xCB, 0xCD, 0x3A, 0x67,
    0x0A, 0x3F, 0x11, 0xF0, 0x36, 0xF2, 0x04, 0x6B,
    0xB2, 0xFF, 0x3E, 0x19, 0x82, 0x45, 0xF3, 0xC2,
    0x55, 0xD4, 0xD1, 0x3D, 0x2D, 0xDE, 0x01, 0xC1
};
}
