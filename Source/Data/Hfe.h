#pragma once
// HxC floppy emulator (.HFE, format 1, ISO MFM, 80 tracks x 2 sides x 9 sectors of 512 bytes) -> 737280 byte raw disk image.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s5::data
{
bool isHfe (const uint8_t* d, size_t n);
bool hfeToImage (const uint8_t* d, size_t n, std::vector<uint8_t>& out, int* missingSectors = nullptr);
// raw image of 737280 bytes or more (extra bytes are cut off) or HFE -> 737280 byte raw image; false for anything else
bool toRawImage (const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
}
