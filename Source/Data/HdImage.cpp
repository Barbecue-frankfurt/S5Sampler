#include "HdImage.h"
#include <cstring>

namespace s5::data
{
static uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

bool HdImage::open (const uint8_t* img, size_t size)
{
    if (size < 0x1400 || std::memcmp (img, "* ROLAND S-550 *", 16) != 0) return false;
    bytes.assign (img, img + size);
    areas = 0;
    for (int i = 0; i < kHdSlots; ++i)
    {
        const uint8_t* e = img + 0x400 + i * 64;
        HdSlot& s = slots[i];
        s = HdSlot();
        s.sector = be32 (e + 0x30); s.sectors = be32 (e + 0x34);
        s.valid = s.sectors == (uint32_t) kHdBlockSectors && ((size_t) s.sector + s.sectors) * 512 <= size;
        if (s.valid) ++areas;
        bool anyInk = false;
        for (int k = 0; k < 12 && s.valid; ++k) { const uint8_t c = img[(size_t) s.sector * 512 + k]; if (c != 0 && (c < 32 || c > 126)) s.valid = false; if (c != 0) anyInk = true; }
        if (s.valid && ! anyInk) s.valid = false;               // freshly formatted, nothing saved there yet
        auto txt = [] (const uint8_t* p, int n) { std::string r; for (int k = 0; k < n; ++k) r += (p[k] >= 32 && p[k] < 127) ? (char) p[k] : ' '; return r; };
        s.name = txt (e, 8);
        s.label = txt (e + 8, 0x28);
        if (s.valid) s.blockLabel = txt (img + ((size_t) s.sector + 8) * 512 + 0x33, 12);
    }
    return true;
}

bool HdImage::loadBlock (int slot, Block& b) const
{
    if (slot < 0 || slot >= kHdSlots || ! slots[slot].valid) return false;
    const uint8_t* s = bytes.data() + (size_t) slots[slot].sector * 512;
    std::vector<uint8_t> f ((size_t) kImageSize, 0);
    std::memcpy (&f[4], "S550", 4);
    std::memcpy (&f[kOffPatch], s, 8 * 512);                 // patches
    std::memcpy (&f[kOffLabel - 0x33], s + 8 * 512, 512);    // misc sector (label at +0x33)
    std::memcpy (&f[kOffTone], s + 9 * 512, 8 * 512);        // tones
    std::memcpy (&f[kOffTone + 8 * 512], s + 17 * 512, 512);
    for (int bank = 0; bank < 2; ++bank)
    {
        const uint8_t* w = s + (18 + bank * 864) * 512;
        std::vector<int16_t> v ((size_t) kWaveWords);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (int16_t) (((int16_t) (uint16_t) (w[i * 2] | (w[i * 2 + 1] << 8))) >> 4);
        packWave (v, &f[(size_t) (kOffWaveA + bank * kSegments * kSegBytes)]);
    }
    return b.parse (f.data(), f.size()) == Block::Result::Ok;
}

bool HdImage::saveBlock (int slot, const Block& b, std::vector<std::pair<size_t, size_t>>& dirty, std::string& err, bool withParams)
{
    err.clear();
    if (slot < 0 || slot >= kHdSlots) { err = "HD Area Error"; return false; }
    HdSlot& sl = slots[slot];
    if (sl.sectors != (uint32_t) kHdBlockSectors || ((size_t) sl.sector + sl.sectors) * 512 > bytes.size()) { err = "HD Area not formatted"; return false; }
    const std::vector<uint8_t> f = b.serialize();
    if (f.size() < (size_t) kImageSize) { err = "HD Write Error"; return false; }
    uint8_t* s = bytes.data() + (size_t) sl.sector * 512;
    std::memcpy (s, &f[kOffPatch], 8 * 512);
    if (withParams || ! sl.valid) std::memcpy (s + 8 * 512, &f[kOffLabel - 0x33], 512);                // Save Set: the parameters (Play / FUNC / MIDI) go with it
    else std::memcpy (s + 8 * 512 + 0x33, &f[kOffLabel], kLabelSize);                                  // Save Block: sound data only, the area keeps its parameters
    std::memcpy (s + 9 * 512, &f[kOffTone], 8 * 512);
    std::memcpy (s + 17 * 512, &f[kOffTone + 8 * 512], 512);
    for (int bank = 0; bank < 2; ++bank)
    {
        uint8_t* w = s + (18 + bank * 864) * 512;
        for (size_t i = 0; i < (size_t) kWaveWords; ++i)
        {
            const uint16_t v = (uint16_t) ((int) b.wave[bank][i] << 4);
            w[i * 2] = (uint8_t) (v & 0xff); w[i * 2 + 1] = (uint8_t) (v >> 8);
        }
    }
    uint8_t* e = bytes.data() + 0x400 + (size_t) slot * 64;                 // directory name = block label
    std::string nm = b.label.substr (0, 8); nm.resize (8, ' ');
    std::memcpy (e, nm.data(), 8);
    sl.name = nm; sl.valid = true;
    std::memset (e + 8, 0, 0x20);                                            // short text of the area (HD index): the block label
    std::string lab = b.label; lab.resize (12, ' ');
    std::memcpy (e + 0x18, lab.data(), 12);
    sl.blockLabel.clear(); for (int k = 0; k < 12; ++k) { const uint8_t c = s[8 * 512 + 0x33 + k]; sl.blockLabel += (c >= 32 && c < 127) ? (char) c : ' '; }
    dirty.push_back ({ (size_t) (0x400 + slot * 64), 64 });
    dirty.push_back ({ (size_t) sl.sector * 512, (size_t) kHdBlockSectors * 512 });
    return true;
}

std::vector<uint8_t> HdImage::makeBlank (int mb)
{
    const int nAreas = mb == 20 ? 16 : mb == 40 ? 32 : mb == 60 ? 48 : 64;
    std::vector<uint8_t> v ((size_t) mb * 1024 * 1024, 0);
    static const uint8_t hdr[64] = { '*',' ','R','O','L','A','N','D',' ','S','-','5','5','0',' ','*', 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x03,0xff,0xff,0xff,0xff,0xff,0xff,
                                     'S','y','s','t','e','m',' ','D','i','r','e','c','t','o','r','y', 0,0,0,0x01,0,0,0,0x01,' ',0xff,0xff,0xff,0xff,0xff,0xff,0xff };
    std::memcpy (v.data(), hdr, 64);
    std::memset (&v[0x1400], 0xff, 0x200);                                   // as on a formatted disk: unused directory space
    const uint32_t first = 5368;                                              // first area (the system area lies in front of it)
    for (int i = 0; i < nAreas; ++i)
    {
        uint8_t* e = &v[0x400 + (size_t) i * 64];
        std::memset (e, ' ', 8);
        const uint32_t sec = first + (uint32_t) i * kHdBlockSectors, cnt = kHdBlockSectors;
        for (int k = 0; k < 4; ++k) { e[0x30 + k] = (uint8_t) (sec >> (24 - 8 * k)); e[0x34 + k] = (uint8_t) (cnt >> (24 - 8 * k)); }
        e[0x38] = 0x41; std::memset (e + 0x39, 0xff, 7);
    }
    return v;
}
}
