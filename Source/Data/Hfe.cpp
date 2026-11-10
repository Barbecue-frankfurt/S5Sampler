#include "Hfe.h"
#include <cstring>

namespace s5::data
{
bool isHfe (const uint8_t* d, size_t n) { return n > 512 && std::memcmp (d, "HXCPICFE", 8) == 0; }

namespace
{
// bit stream of one side (HFE stores bits LSB first)
std::vector<uint8_t> sideBits (const std::vector<uint8_t>& bytes)
{
    std::vector<uint8_t> b; b.reserve (bytes.size() * 8);
    for (uint8_t x : bytes) for (int k = 0; k < 8; ++k) b.push_back ((x >> k) & 1);
    return b;
}

int byteAt (const std::vector<uint8_t>& bs, size_t j)       // MFM: every second bit is data
{
    int v = 0;
    for (int k = 0; k < 8; ++k) v = (v << 1) | bs[j + 2 * (size_t) k + 1];
    return v;
}
}

bool hfeToImage (const uint8_t* d, size_t n, std::vector<uint8_t>& out, int* missing)
{
    if (! isHfe (d, n) || n < 1024) return false;
    const int tracks = d[9];
    const size_t lut = (size_t) d[18] | ((size_t) d[19] << 8);
    if (tracks < 80) return false;
    out.assign (80 * 2 * 9 * 512, 0);
    int miss = 0;
    static const uint8_t sync[16] = { 0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1 };
    for (int c = 0; c < 80; ++c)
    {
        const size_t e = lut * 512 + 4 * (size_t) c;
        if (e + 4 > n) return false;
        const size_t off = ((size_t) d[e] | ((size_t) d[e + 1] << 8)) * 512, len = (size_t) d[e + 2] | ((size_t) d[e + 3] << 8);
        std::vector<uint8_t> side[2];
        for (size_t i = 0; i < len; i += 512)
        {
            if (off + i + 512 > n) break;
            side[0].insert (side[0].end(), d + off + i, d + off + i + 256);
            side[1].insert (side[1].end(), d + off + i + 256, d + off + i + 512);
        }
        for (int h = 0; h < 2; ++h)
        {
            const auto bs = sideBits (side[h]);
            bool got[9] = {};
            int cr = -1;
            size_t i = 0;
            while (i + 60 < bs.size())
            {
                bool hit = true;
                for (int k = 0; k < 48 && hit; ++k) hit = bs[i + (size_t) k] == sync[k % 16];
                if (! hit) { ++i; continue; }
                size_t j = i + 48;
                if (j + 16 > bs.size()) break;
                const int mark = byteAt (bs, j); j += 16;
                if (mark == 0xFE)
                {
                    if (j + 64 > bs.size()) break;
                    const int cc = byteAt (bs, j), hh = byteAt (bs, j + 16), r = byteAt (bs, j + 32);
                    cr = (cc == c && hh == h && r >= 1 && r <= 9) ? r : -1;
                    i = j + 16 * 6; continue;
                }
                if ((mark == 0xFB || mark == 0xF8) && cr > 0)
                {
                    if (j + 16 * 514 > bs.size()) break;
                    uint8_t* dst = out.data() + (((size_t) c * 2 + (size_t) h) * 9 + (size_t) (cr - 1)) * 512;
                    for (int q = 0; q < 512; ++q) dst[q] = (uint8_t) byteAt (bs, j + 16 * (size_t) q);
                    got[cr - 1] = true; cr = -1; i = j + 16 * 514; continue;
                }
                i = j;
            }
            for (int r = 0; r < 9; ++r) if (! got[r]) ++miss;
        }
    }
    if (missing) *missing = miss;
    return true;
}

bool toRawImage (const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    if (isHfe (in.data(), in.size())) return hfeToImage (in.data(), in.size(), out);
    if (in.size() >= 0x1400 && std::memcmp (in.data(), "* ROLAND S-550 *", 16) == 0) { out = in; return true; }   // hard disk image: kept whole
    if (in.size() < 737280) return false;
    out.assign (in.begin(), in.begin() + 737280);
    return true;
}
}
