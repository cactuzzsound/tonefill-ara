#include "dsp/VoiceDetect.h"

#if TONEFILL_USE_FVAD

#include <fvad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tonefill::dsp
{
std::vector<char> detectVoice (const float* mono, int n, double sampleRate, int aggressiveness)
{
    std::vector<char> mask ((std::size_t) (n > 0 ? n : 0), 0);
    if (n <= 0 || sampleRate <= 0.0 || mono == nullptr) return mask;

    // libfvad supports 8/16/32/48 kHz only -> resample (nearest) to 16 kHz for detection.
    const int vsr = 16000;
    const double ratio = sampleRate / (double) vsr; // input samples per VAD sample
    const int vn = (int) ((double) n / ratio);
    if (vn < 480) return mask;

    std::vector<std::int16_t> buf ((std::size_t) vn);
    for (int i = 0; i < vn; ++i)
    {
        const int idx = (int) ((double) i * ratio);
        const float s = idx < n ? mono[idx] : 0.0f;
        const int v = (int) std::lround (s * 32767.0f);
        buf[(std::size_t) i] = (std::int16_t) std::max (-32768, std::min (32767, v));
    }

    Fvad* vad = fvad_new();
    if (vad == nullptr) return mask;
    fvad_set_mode (vad, std::max (0, std::min (3, aggressiveness)));
    fvad_set_sample_rate (vad, vsr);

    const int frame = 480; // 30 ms @ 16 kHz
    for (int f = 0; f + frame <= vn; f += frame)
    {
        if (fvad_process (vad, &buf[(std::size_t) f], (std::size_t) frame) == 1)
        {
            const int os = (int) ((double) f * ratio);
            const int oe = std::min (n, (int) ((double) (f + frame) * ratio));
            for (int o = os; o < oe; ++o) mask[(std::size_t) o] = 1;
        }
    }

    fvad_free (vad);
    return mask;
}
} // namespace tonefill::dsp

#else

namespace tonefill::dsp
{
std::vector<char> detectVoice (const float*, int, double, int) { return {}; }
} // namespace tonefill::dsp

#endif
