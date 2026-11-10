#pragma once
// S-550 hard disk image (ZIP / ZuluSCSI): directory of 64 slots, each slot one block of
// 1746 sectors (512 bytes): 8 patch sectors, 1 label sector, 8 tone sectors, 1 list sector,
// 2 x 864 sectors of 16 bit wave words (12 bit sample << 4, little endian).
#include "Block.h"
#include <string>
#include <vector>
#include <utility>

namespace s5::data
{
constexpr int kHdSlots = 64, kHdBlockSectors = 1746;

struct HdSlot { std::string name, label, blockLabel; uint32_t sector = 0, sectors = 0; bool valid = false; };

struct HdImage
{
    std::vector<uint8_t> bytes;
    HdSlot slots[kHdSlots];
    int areas = 0;                                         // formatted areas (directory entries with a whole block)
    static std::vector<uint8_t> makeBlank (int megabytes);   // freshly formatted image: 20 / 40 / 60 / 80 MB = 16 / 32 / 48 / 64 areas
    bool open (const uint8_t* img, size_t size);          // false if not an S-550 HD image
    bool saveBlock (int slot, const Block& b, std::vector<std::pair<size_t, size_t>>& dirty, std::string& err, bool withParams = true);   // writes a block into the area (bytes + changed ranges)
    bool loadBlock (int slot, Block& out) const;           // convert slot to a Block
    static std::string slotId (int slot) { return std::string ("H") + char ('1' + slot / 8) + char ('1' + slot % 8); }
};
}
