#pragma once
#include <string>
#include <vector>

namespace s5
{
// One editable value: integer range, optionally shown through a list of names.
struct Param
{
    std::string label;
    int value = 0, min = 0, max = 0;
    int step = 1;                        // size of one Dec/Inc push
    int fast = 1;                        // step multiplier while Shift is held (address fields: 10)
    int pad = 0;                         // zero padded width (addresses)
    std::vector<std::string> names;      // if not empty: value (minus min) indexes into names

    std::string text() const
    {
        if (! names.empty()) return names[(size_t) (value - min)];
        std::string t = std::to_string (value < 0 ? -value : value);
        while ((int) t.size() < pad) t = "0" + t;
        return value < 0 ? "-" + t : t;
    }
    bool change (int dir)
    {
        long nv = (long) value + (long) dir * step;
        if (nv < min) nv = min;
        if (nv > max) nv = max;
        if (nv == value) return false;
        value = (int) nv;
        return true;
    }
};
}
