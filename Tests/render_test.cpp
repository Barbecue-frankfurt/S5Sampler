// Renders pages with the C++ engine and writes raw RGB (P6 PPM) for comparison with the layout boards.
#include "../Source/Screen/Pages.h"
#include <cstdio>
#include <cstring>
#include <vector>

int main (int argc, char** argv)
{
    const std::string page = argc > 1 ? argv[1] : "patch";
    const char* out = argc > 2 ? argv[2] : "out.ppm";
    s5::Screen s;
    if (page == "patch") s5::pages::patchPrm (s, s5::pages::defaultPatchPrm());
    else { std::fprintf (stderr, "unknown page\n"); return 1; }
    std::vector<uint32_t> rgb (s5::kW * s5::kH);
    s.toRgb (rgb.data());
    FILE* f = std::fopen (out, "wb");
    std::fprintf (f, "P6\n%d %d\n255\n", s5::kW, s5::kH);
    for (uint32_t p : rgb) { unsigned char c[3] = { (unsigned char) (p >> 16), (unsigned char) (p >> 8), (unsigned char) p }; std::fwrite (c, 1, 3, f); }
    std::fclose (f);
    return 0;
}
