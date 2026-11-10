// Writes every page image to <dir>/<id>.ppm so they can be diffed against the layout boards.
#include "../Source/Screen/UiState.h"
#include <cstdio>
#include <vector>
int main (int, char** argv)
{
    // live pages in their default state
    for (const char* id : { "ToneEdit", "Lfo", "Loop", "Tvf", "Tva", "Split", "Master", "Message", "ProgNo", "Truncate", "Combine", "DFilter", "Delete", "CopyMove", "DispWave", "Standard", "EightKey", "Main", "Mix", "WaveDraw", "Sampling", "ToneMap", "PatchMap", "Disk", "LoadPRM", "SavePRM", "LoadTone", "LoadAll", "SaveAll", "Format", "Backup", "SaveSys", "ChangeSys", "DirPatch", "DirTone" })
    {
        s5::Screen s; auto p = s5::makeLivePage (id); p->draw (s);
        std::vector<uint32_t> rgb (s5::kW * s5::kH); s.toRgb (rgb.data());
        std::string n = std::string (argv[1]) + "/live_" + id + ".ppm";
        FILE* f = std::fopen (n.c_str(), "wb"); std::fprintf (f, "P6\n%d %d\n255\n", s5::kW, s5::kH);
        for (uint32_t q : rgb) { unsigned char c[3] = { (unsigned char) (q >> 16), (unsigned char) (q >> 8), (unsigned char) q }; std::fwrite (c, 1, 3, f); }
        std::fclose (f);
    }
    for (int i = 0; i < s5::pages::kNumPageImages; ++i)
    {
        s5::Screen s;
        s5::pages::drawImage (s, s5::pages::kPageImages[i]);
        std::vector<uint32_t> rgb (s5::kW * s5::kH);
        s.toRgb (rgb.data());
        std::string n = std::string (argv[1]) + "/" + s5::pages::kPageImages[i].id + ".ppm";
        FILE* f = std::fopen (n.c_str(), "wb");
        std::fprintf (f, "P6\n%d %d\n255\n", s5::kW, s5::kH);
        for (uint32_t p : rgb) { unsigned char c[3] = { (unsigned char) (p >> 16), (unsigned char) (p >> 8), (unsigned char) p }; std::fwrite (c, 1, 3, f); }
        std::fclose (f);
    }
}
