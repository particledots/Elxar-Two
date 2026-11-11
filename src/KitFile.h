// ELXAR-Two: reading LXR-02 kit files (.SND). No JUCE in this file.
//
// The file layout comes from the firmware's own preset manager (Core/Src/Preset/presetManager.c in
// github.com/erica-synths/lxr-desktop-app, commit a43cd4f; that file is hardware code and is not part of this repo):
//   - preset_saveDrumsetSD writes 8 name bytes, then one byte for each sound parameter 0 .. END_OF_SOUND_PARAMETERS-1.
//     END_OF_SOUND_PARAMETERS is 247 (the same count as ELXAR_NUM_KIT_PARAMS, checked at compile time in elxar_engine.c),
//     so a kit file is 8 + 247 = 255 bytes. There is no header, no checksum, no version byte.
//   - preset_loadDrumsetSD reads the 8 name bytes, then up to 247 bytes. If the file is shorter, the missing values are
//     set to 0 (this is how kits from older firmware, which had fewer parameters, still load). Extra bytes are ignored.
//   - After reading, the firmware sets every velocity target and LFO target that is not in its list of modulation
//     targets to 0 (preset_readDrumsetData). The same is done here.
//   - The file name on the SD card is "NN-NAME.snd" (slot number, first 5 letters of the name). The name that counts is
//     the one INSIDE the file; the file name is only a label.
// Changes ELXAR-Two makes on top of the firmware's rules (the hardware has none of these checks):
//   - every byte is limited to 0..127 (a parameter is a 7-bit value), EXCEPT the 12 modulation target values (velocity
//     target and LFO target of each voice): these are positions in the firmware's list of 229 targets, so they run up to
//     228 and must keep values above 127;
//   - a list value (waveform, filter type, ...) past the end of its list is set to the last entry, a yes/no value to 0 or 1,
//     and an audio routing above 6 (the effect) to 0. The engine survives any value (tested with thousands of random kits
//     under the memory checker), but the window could not show a value that is not in its list.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "SoundParams.h"
extern "C" {
#include "elxar_engine.h"
}

namespace elxar
{
namespace kitfile
{
constexpr int kNameBytes = 8;
constexpr int kFileBytes = kNameBytes + ELXAR_NUM_KIT_PARAMS;   // 255

using Params = std::array<uint8_t, (size_t) ELXAR_NUM_KIT_PARAMS>;

// The firmware's routing value that sends a voice through the effect (MIXER_ROUTING_FX_BUS in mixer.h).
constexpr int kRoutingFx = 6;

// Puts a kit into the range the window and the engine are happy with (see the top of this file).
// numModTargets = elxar_num_mod_targets(), passed in so that this can also be tested without the engine.
// Returns how many values had to be changed.
inline bool isTargetParam (int par)
{
    return (par >= PAR_VEL_DEST_1 && par <= PAR_VEL_DEST_6) || (par >= PAR_TARGET_LFO1 && par <= PAR_TARGET_LFO6);
}

inline int sanitise (Params& k, int numModTargets)
{
    int changed = 0;
    auto limit = [&changed] (uint8_t& b, int most) { if ((int) b > most) { b = (uint8_t) most; ++changed; } };
    for (int p = 0; p < ELXAR_NUM_KIT_PARAMS; ++p)
    {
        auto& b = k[(size_t) p];
        if (isTargetParam (p)) { if ((int) b >= numModTargets) { b = 0; ++changed; } }   // the firmware's own rule
        else limit (b, 127);
    }
    for (int v = 0; v < ELXAR_NUM_VOICES; ++v)
    {
        for (const auto& i : voiceParams (v))
        {
            if (i.par < 0 || i.par >= ELXAR_NUM_KIT_PARAMS) continue;
            auto& b = k[(size_t) i.par];
            switch (i.kind)
            {
                case ParamKind::choice:         limit (b, i.numChoices - 1); break;
                case ParamKind::onOff:          limit (b, 1); break;
                case ParamKind::velocityTarget:
                case ParamKind::lfoTarget:      break;   // done above
                case ParamKind::value:          break;
            }
        }
        auto& route = k[(size_t) (PAR_AUDIO_OUT1 + v)];
        if (route > kRoutingFx) { route = 0; ++changed; }
    }
    limit (k[PAR_FX_TYPE], 5);   // Off, Drive, Ring mod, Compressor, Delay, Reverb
    return changed;
}

// The 8 name bytes as text: stops at the first zero byte, anything that is not a plain letter, digit or punctuation mark
// becomes "_", spaces at the end are removed. Can be empty.
inline std::string nameFromBytes (const uint8_t* raw8)
{
    std::string s;
    for (int i = 0; i < kNameBytes; ++i)
    {
        const uint8_t c = raw8[i];
        if (c == 0) break;
        s.push_back (c >= 32 && c <= 126 ? (char) c : '_');
    }
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

struct Result
{
    bool ok = false;
    std::string error;      // when ok is false: what is wrong, in plain words
    std::string note;       // when ok is true: something worth telling (for example a short file)
    std::string name;       // from inside the file; may be empty
    int adjusted = 0;       // how many values were outside what the window shows and were changed (see the top of this file)
    Params params {};
};

inline Result parse (const uint8_t* data, size_t size, int numModTargets)
{
    Result r;
    if (size < (size_t) kNameBytes + 1)
    {
        r.error = "This file is too small to be a kit (" + std::to_string (size) + " bytes; a kit file has " + std::to_string (kFileBytes) + ").";
        return r;
    }
    if (size > (size_t) kFileBytes)
    {
        r.error = "This file is too big to be a kit (" + std::to_string (size) + " bytes; a kit file has " + std::to_string (kFileBytes) + "). Pattern, song and project files are not kits.";
        return r;
    }
    r.name = nameFromBytes (data);
    const size_t have = size - (size_t) kNameBytes;
    for (size_t i = 0; i < r.params.size(); ++i)
        r.params[i] = i < have ? data[(size_t) kNameBytes + i] : (uint8_t) 0;
    if (have < r.params.size())
        r.note = "short file (" + std::to_string (size) + " bytes): the last " + std::to_string (r.params.size() - have) + " values were set to 0, as the hardware does";
    r.adjusted = sanitise (r.params, numModTargets);
    r.ok = true;
    return r;
}
} // namespace kitfile
} // namespace elxar
