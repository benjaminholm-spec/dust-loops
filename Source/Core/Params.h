#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs. These are saved in Logic projects and presets: never rename or reuse one.
// Values are stored in real units (Hz, seconds, dB, semitones) so preset JSON stays readable.
namespace ID
{
namespace global
{
inline constexpr auto master    = "global.master";
inline constexpr auto inputGain = "global.inputGain"; // FX main input, or the instrument's sidechain
} // namespace global

namespace synth
{
inline constexpr auto model     = "synth.model";
inline constexpr auto voiceMode = "synth.voiceMode";
inline constexpr auto glide     = "synth.glide";
inline constexpr auto bendRange = "synth.bendRange";
inline constexpr auto level     = "synth.level";
inline constexpr auto slop      = "synth.slop";
inline constexpr const char* macro[4] { "synth.macro1", "synth.macro2", "synth.macro3", "synth.macro4" };

// DCO-Poly
inline constexpr auto saw   = "synth.saw";
inline constexpr auto pulse = "synth.pulse";
inline constexpr auto pw    = "synth.pw";
inline constexpr auto pwm   = "synth.pwm";
inline constexpr auto sub   = "synth.sub";
inline constexpr auto noise = "synth.noise";

// VCO-Poly
inline constexpr auto osc1Wave     = "synth.osc1Wave";
inline constexpr auto osc2Wave     = "synth.osc2Wave";
inline constexpr auto osc2Semi     = "synth.osc2Semi";
inline constexpr auto osc2Detune   = "synth.osc2Detune";
inline constexpr auto osc2Level    = "synth.osc2Level";
inline constexpr auto sync         = "synth.sync";
inline constexpr auto polyModPitch = "synth.polyModPitch";
inline constexpr auto polyModPw    = "synth.polyModPw";
inline constexpr auto drive        = "synth.drive";

// Ensemble
inline constexpr auto ensFeet     = "synth.ensFeet";
inline constexpr auto ensBright   = "synth.ensBright";
inline constexpr auto ensEnsemble = "synth.ensEnsemble";

// Filter
inline constexpr auto cutoff       = "synth.cutoff";
inline constexpr auto resonance    = "synth.resonance";
inline constexpr auto hpf          = "synth.hpf";
inline constexpr auto filterEnv    = "synth.filterEnv";
inline constexpr auto keyTrack     = "synth.keyTrack";
inline constexpr auto velToFilter  = "synth.velToFilter";
inline constexpr auto atToFilter   = "synth.atToFilter";
inline constexpr auto fAttack      = "synth.fAttack";
inline constexpr auto fDecay       = "synth.fDecay";
inline constexpr auto fSustain     = "synth.fSustain";
inline constexpr auto fRelease     = "synth.fRelease";

// Amp
inline constexpr auto attack   = "synth.attack";
inline constexpr auto decay    = "synth.decay";
inline constexpr auto sustain  = "synth.sustain";
inline constexpr auto release  = "synth.release";
inline constexpr auto velToAmp = "synth.velToAmp";

// LFO
inline constexpr auto lfoRate   = "synth.lfoRate";
inline constexpr auto lfoDelay  = "synth.lfoDelay";
inline constexpr auto lfoShape  = "synth.lfoShape";
inline constexpr auto lfoPitch  = "synth.lfoPitch";
inline constexpr auto lfoFilter = "synth.lfoFilter";
inline constexpr auto lfoPwm    = "synth.lfoPwm";

// Chorus
inline constexpr auto chorus    = "synth.chorus";
inline constexpr auto chorusMix = "synth.chorusMix";
} // namespace synth

namespace atmos
{
inline constexpr auto level   = "atmos.level";
inline constexpr auto crackle = "atmos.crackle";
inline constexpr auto rain    = "atmos.rain";
inline constexpr auto hum     = "atmos.hum";
inline constexpr auto humFreq = "atmos.humFreq";
inline constexpr auto tone    = "atmos.tone";
inline constexpr auto always  = "atmos.always";
} // namespace atmos

namespace tape
{
inline constexpr auto mode       = "tape.mode";
inline constexpr auto time       = "tape.time";
inline constexpr auto feed       = "tape.feed";
inline constexpr auto mix        = "tape.mix";
inline constexpr auto volume     = "tape.volume";
inline constexpr auto speed      = "tape.speed";
inline constexpr auto inertia    = "tape.inertia";
inline constexpr auto freeze     = "tape.freeze";
inline constexpr auto sync       = "tape.sync";
inline constexpr auto select     = "tape.select";
inline constexpr auto brake      = "tape.brake";
inline constexpr auto type       = "tape.type";
inline constexpr auto wow        = "tape.wow";
inline constexpr auto flutter    = "tape.flutter";
inline constexpr auto hiss       = "tape.hiss";
inline constexpr auto saturation = "tape.saturation";
inline constexpr auto age        = "tape.age";
inline constexpr auto dropouts   = "tape.dropouts";
} // namespace tape

// Cassette piano keys. Momentary: an action fires on the rising edge.
namespace xport
{
inline constexpr auto rec   = "xport.rec";
inline constexpr auto play  = "xport.play";
inline constexpr auto rew   = "xport.rew";
inline constexpr auto ffwd  = "xport.ffwd";
inline constexpr auto stop  = "xport.stop";
inline constexpr auto pause = "xport.pause";
} // namespace xport

namespace loop
{
inline constexpr int count = 3;
inline constexpr const char* beats[count]   { "loopA.beats",   "loopB.beats",   "loopC.beats" };
inline constexpr const char* seconds[count] { "loopA.seconds", "loopB.seconds", "loopC.seconds" };
inline constexpr const char* level[count]   { "loopA.level",   "loopB.level",   "loopC.level" };
inline constexpr const char* pan[count]     { "loopA.pan",     "loopB.pan",     "loopC.pan" };
inline constexpr const char* speed[count]   { "loopA.speed",   "loopB.speed",   "loopC.speed" };
inline constexpr const char* mute[count]    { "loopA.mute",    "loopB.mute",    "loopC.mute" };
inline constexpr const char* rec[count]     { "loopA.rec",     "loopB.rec",     "loopC.rec" };
inline constexpr const char* clear[count]   { "loopA.clear",   "loopB.clear",   "loopC.clear" };
} // namespace loop

namespace pedal
{
// Every pedal has these three, as "pedal.<name>.on" etc.
inline constexpr auto on = "on", loop = "loop", loopAmt = "loopAmt";

inline constexpr int count = 5;
inline constexpr const char* names[count] { "dirt", "warble", "drift", "cloud", "hall" };

namespace dirt   { inline constexpr auto drive = "pedal.dirt.drive", blend = "pedal.dirt.blend", tone = "pedal.dirt.tone", mix = "pedal.dirt.mix"; }
namespace warble { inline constexpr auto rate = "pedal.warble.rate", depth = "pedal.warble.depth", mode = "pedal.warble.mode", mix = "pedal.warble.mix"; }
namespace drift  { inline constexpr auto freq = "pedal.drift.freq", reso = "pedal.drift.reso", mode = "pedal.drift.mode",
                   lfoRate = "pedal.drift.lfoRate", lfoDepth = "pedal.drift.lfoDepth", envAmt = "pedal.drift.envAmt"; }
namespace cloud  { inline constexpr auto pitch = "pedal.cloud.pitch", smear = "pedal.cloud.smear", density = "pedal.cloud.density", mix = "pedal.cloud.mix"; }
namespace hall   { inline constexpr auto decay = "pedal.hall.decay", tone = "pedal.hall.tone", mix = "pedal.hall.mix", freeze = "pedal.hall.freeze"; }

inline juce::String id (int pedalIndex, const char* suffix) { return juce::String ("pedal.") + names[pedalIndex] + "." + suffix; }
} // namespace pedal
} // namespace ID

namespace Params
{
// The FX build leaves out the synth section so Logic's automation list only shows what exists.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool includeSynth);
} // namespace Params
