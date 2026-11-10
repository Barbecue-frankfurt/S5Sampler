#pragma once
// Time axis of the S-550 envelopes, measured on device photos (TVA and TVF screens, Disp.Zoom 1-4, rates 5 to 100).
// Shared by the display (EDIT-TVA / EDIT-TVF graph) and the audio engine.
namespace s5::env
{
// Screen pixels per level step at Disp.Zoom 3 for a segment with this Rate (0 for Rate 127: the level jumps).
inline double pxPerLevel (int rate)
{
    if (rate >= 127) return 0.0;
    auto f = [] (int i) { return i <= 20 ? 0.8997 : i <= 29 ? 0.9531 : i <= 57 ? 0.9662 : i <= 68 ? 0.9535 : 0.9642; };     // k (i) = k (i - 1) * f (i)
    double k = 0.65;                                     // rate 57
    for (int i = 58; i <= rate; ++i) k *= f (i);
    for (int i = 57; i > rate; --i) k /= f (i);
    return k;
}
// One Disp.Zoom step is about 2.17 times wider.
inline double zoomFactor (int zoom)
{
    double k = 1.0;
    for (int z = 3; z < zoom; ++z) k *= 2.17;
    for (int z = 3; z > zoom; --z) k /= 2.17;
    return k;
}
// Seconds one screen pixel (Disp.Zoom 3) stands for.  Not measured: the photos only show the axis, so this is an assumption
// (a full sweep at Rate 0 takes about 30 s, at Rate 57 about 0.4 s, at Rate 100 under 0.1 s).
constexpr double kSecondsPerPixel = 0.005;
inline double secondsPerLevel (int rate) { return pxPerLevel (rate) * kSecondsPerPixel; }
}
