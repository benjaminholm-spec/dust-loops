#!/usr/bin/env python3
"""Writes the factory presets to presets/<Bank>/NN Name.json.

The JSON files are what the plugin ships (they are built into it). This script only produced the
starting set: edit the JSON directly, or edit here and re-run. Anything a preset leaves out loads at its
default. Values are in real units (Hz, seconds, dB); choices are given by name.

    python3 tools/make_presets.py
"""
import json
import os
import shutil

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'presets')
HM, MD, NB, UT = 'Hazy Memories', 'Machine Drones', 'Night Bus', 'Utility'
PRESETS = []

# Loudness trims in dB from the render tool (tools/Render.cpp), applied to synth.level.
TRIMS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preset_trims.json')
TRIMS = json.load(open(TRIMS_FILE)) if os.path.exists(TRIMS_FILE) else {}


# ---- helpers -------------------------------------------------------------------------------------
def section(prefix, kw):
    return {prefix + k: v for k, v in kw.items()}


def syn(**kw): return section('synth.', kw)
def atm(**kw): return section('atmos.', kw)
def tape(**kw): return section('tape.', kw)


def mac(bloom=0.0, motion=0.0, warp=0.0, haze=0.0):
    return {'synth.macro1': bloom, 'synth.macro2': motion, 'synth.macro3': warp, 'synth.macro4': haze}


def loops(**kw):
    """loops(beats=(16, 13, 11), level=(-3, -3, -6), mute=(False, False, True), ...). REC arms are never stored."""
    out = {}
    for key, values in kw.items():
        for i, v in enumerate(values):
            if v is not None:
                out['loop%s.%s' % ('ABC'[i], key)] = v
    return out


def pedal(name, **kw):
    d = {'pedal.%s.on' % name: True}
    d.update({'pedal.%s.%s' % (name, k): v for k, v in kw.items()})
    return d


def tape_type(kind):
    wear = {'Ferric': (0.2, 0.15, 0.12, 0.3, 0.25, 0.05),
            'Chrome': (0.08, 0.06, 0.05, 0.15, 0.12, 0.02),
            'Worn Dictaphone': (0.55, 0.4, 0.35, 0.55, 0.55, 0.3)}[kind]
    keys = ('wow', 'flutter', 'hiss', 'saturation', 'age', 'dropouts')
    d = {'tape.type': kind}
    d.update({'tape.' + k: v for k, v in zip(keys, wear)})
    return d


ALL_OFF = {}
for _p in ('dirt', 'warble', 'drift', 'cloud', 'hall'):
    ALL_OFF['pedal.%s.on' % _p] = False
    ALL_OFF['pedal.%s.loop' % _p] = False

DEFAULT_ORDER = 'dirt,warble,drift,tape,cloud,hall'


def P(bank, name, description, *parts, order=DEFAULT_ORDER):
    params = dict(ALL_OFF)
    for part in parts:
        params.update(part)
    # Loudness trim from the render tool, measured as the plugin normally loads presets: sound only.
    level = params.get('synth.level', -6.0) + TRIMS.get(name, 0.0)
    params['synth.level'] = round(max(-30.0, min(6.0, level)), 1)
    PRESETS.append({'name': name, 'bank': bank, 'description': description, 'pedalOrder': order, 'params': params})


# ---- synth archetypes ----------------------------------------------------------------------------
def dco_pad(**kw):
    d = syn(model='DCO-Poly', saw=0.8, pulse=0.5, pw=0.5, pwm=0.45, sub=0.3, noise=0.0, cutoff=1500, resonance=0.2,
            filterEnv=0.15, keyTrack=0.4, fAttack=1.5, fDecay=3.0, fSustain=0.6, fRelease=3.0,
            attack=1.2, decay=2.0, sustain=0.9, release=3.5, velToAmp=0.2, velToFilter=0.2,
            lfoRate=0.45, lfoShape='Triangle', chorus='II', chorusMix=1.0, slop=0.3)
    d.update(syn(**kw))
    return d


def dco_keys(**kw):
    d = syn(model='DCO-Poly', saw=0.6, pulse=0.7, pw=0.35, pwm=0.1, sub=0.25, cutoff=1100, resonance=0.15,
            filterEnv=0.45, keyTrack=0.6, fAttack=0.002, fDecay=1.4, fSustain=0.2, fRelease=1.2,
            attack=0.004, decay=2.8, sustain=0.3, release=1.4, velToAmp=0.55, velToFilter=0.45,
            lfoRate=0.5, chorus='I', chorusMix=0.9, slop=0.25)
    d.update(syn(**kw))
    return d


def dco_bell(**kw):
    d = syn(model='DCO-Poly', saw=0.0, pulse=1.0, pw=0.12, pwm=0.0, sub=0.0, cutoff=700, resonance=0.35,
            filterEnv=0.6, keyTrack=1.0, fAttack=0.001, fDecay=0.9, fSustain=0.0, fRelease=1.5,
            attack=0.002, decay=3.0, sustain=0.0, release=2.8, velToAmp=0.6, velToFilter=0.5,
            chorus='I', chorusMix=0.7, slop=0.3)
    d.update(syn(**kw))
    return d


def vco_pad(**kw):
    d = syn(model='VCO-Poly', osc1Wave=0.0, osc2Wave=0.4, osc2Semi=0, osc2Detune=9.0, osc2Level=0.8, drive=0.3,
            pw=0.4, cutoff=1000, resonance=0.3, filterEnv=0.25, keyTrack=0.4, fAttack=2.0, fDecay=3.0, fSustain=0.5,
            fRelease=4.0, attack=1.5, decay=2.0, sustain=0.9, release=4.0, velToAmp=0.2, velToFilter=0.2,
            lfoRate=0.3, chorus='Off', slop=0.45)
    d.update(syn(**kw))
    return d


def vco_drone(**kw):
    d = syn(model='VCO-Poly', osc1Wave=0.0, osc2Wave=0.0, osc2Semi=-12, osc2Detune=4.0, osc2Level=0.9, drive=0.55,
            cutoff=520, resonance=0.5, filterEnv=0.3, keyTrack=0.2, fAttack=4.0, fDecay=6.0, fSustain=0.5, fRelease=6.0,
            attack=3.0, decay=3.0, sustain=1.0, release=6.0, velToAmp=0.1, velToFilter=0.1,
            lfoRate=0.08, lfoShape='Sine', lfoFilter=0.25, chorus='Off', slop=0.55)
    d.update(syn(**kw))
    return d


def vco_lead(**kw):
    d = syn(model='VCO-Poly', voiceMode='Legato', glide=0.12, osc1Wave=0.0, osc2Wave=1.0, osc2Semi=0, osc2Detune=6.0,
            osc2Level=0.6, pw=0.3, drive=0.4, cutoff=1300, resonance=0.3, filterEnv=0.35, keyTrack=0.6,
            fAttack=0.01, fDecay=1.0, fSustain=0.4, fRelease=0.8, attack=0.02, decay=1.0, sustain=0.85, release=0.8,
            velToAmp=0.3, lfoRate=5.2, lfoShape='Sine', lfoDelay=0.6, lfoPitch=0.25, chorus='Off', slop=0.4)
    d.update(syn(**kw))
    return d


def ens(**kw):
    d = syn(model='Ensemble', ensFeet="8' + 4'", ensBright=0.5, ensEnsemble=0.9, cutoff=6000, resonance=0.1,
            filterEnv=0.0, keyTrack=0.3, attack=0.8, decay=1.0, sustain=1.0, release=2.5, velToAmp=0.15,
            velToFilter=0.1, chorus='Off', slop=0.3)
    d.update(syn(**kw))
    return d


ORDER_DRONE = 'dirt,drift,tape,warble,cloud,hall'
ORDER_POST_DIRT = 'warble,drift,tape,dirt,cloud,hall'
ORDER_CLOUD_FIRST = 'dirt,warble,cloud,tape,drift,hall'

# ==================================================================================================
# Hazy Memories: warm, detuned, nostalgic. Juno pads, wobbly tape, home-movie colours.
# ==================================================================================================
P(HM, 'Cassette Afternoon', 'Warm Juno pad on a worn cassette. Hold slow chords; the mod wheel adds a gentle wobble.',
  dco_pad(), mac(warp=0.3), tape_type('Ferric'), tape(feed=0.65, mix=0.45),
  pedal('warble', rate=0.4, depth=0.25, mix=0.4), pedal('hall', decay=9.0, tone=0.3, mix=0.3, loop=True, loopAmt=0.15))

P(HM, 'Faded Polaroid', 'Soft electric keys that drift out of tune on every pass. Play short phrases and let the loops answer.',
  dco_keys(), mac(warp=0.35, haze=0.2), tape_type('Ferric'), tape(feed=0.7, mix=0.5, wow=0.45),
  pedal('warble', rate=0.3, depth=0.35, mix=0.5, loop=True, loopAmt=0.2), pedal('hall', decay=6.0, tone=0.35, mix=0.25))

P(HM, 'Summer Camp Film', 'Two detuned VCOs, gently seasick. Great for long chord progressions.',
  vco_pad(cutoff=1200, osc2Detune=14.0), mac(warp=0.45, bloom=0.2), tape_type('Ferric'), tape(feed=0.6, mix=0.45),
  pedal('dirt', drive=0.25, blend=0.0, tone=0.4, mix=0.6), pedal('hall', decay=8.0, tone=0.4, mix=0.3))

P(HM, 'Geography Lesson', 'Glassy pulse bells through a tape echo. Play single notes slowly.',
  dco_bell(cutoff=900), mac(warp=0.2), tape_type('Ferric'), tape(mode='Echo', time=0.72, feed=0.55, mix=0.4),
  pedal('warble', rate=0.5, depth=0.2, mix=0.35), pedal('hall', decay=5.0, tone=0.4, mix=0.25))

P(HM, 'Kodachrome Sky', 'String machine through an old ensemble. Big, warm and a little out of focus.',
  ens(ensBright=0.45, cutoff=4500), mac(warp=0.2, bloom=0.2), tape_type('Ferric'), tape(feed=0.65, mix=0.45),
  pedal('hall', decay=14.0, tone=0.3, mix=0.35, loop=True, loopAmt=0.15))

P(HM, 'Slow Dissolve', 'Everything swells in slowly and takes ages to fade. Hold one chord, then another.',
  dco_pad(cutoff=1100), mac(bloom=0.65, warp=0.25), tape_type('Ferric'), tape(feed=0.82, mix=0.55, age=0.4),
  pedal('hall', decay=16.0, tone=0.3, mix=0.35, loop=True, loopAmt=0.2))

P(HM, 'Rust Belt Lullaby', 'A gliding mono lead with a slow vibrato. Play a melody legato; overlap notes to slide.',
  vco_lead(cutoff=1100, osc2Wave=0.3), mac(warp=0.3), tape_type('Ferric'), tape(feed=0.6, mix=0.45),
  pedal('warble', rate=0.35, depth=0.3, mix=0.5, mode='Vibrato'), pedal('hall', decay=7.0, tone=0.35, mix=0.3))

P(HM, 'Daylight Bells', 'Bright music-box bells with a faint octave shimmer on the loops.',
  dco_bell(cutoff=1100, pw=0.2), mac(warp=0.15), tape_type('Chrome'), tape(mode='Echo', time=0.64, feed=0.5, mix=0.4),
  pedal('cloud', pitch='Octave Up', smear=0.6, density=0.4, mix=0.25), pedal('hall', decay=6.0, tone=0.45, mix=0.3))

P(HM, 'Half Speed Holiday', 'Keys on a tape running at half speed: the loops come back dull and a little blurred.',
  dco_keys(cutoff=1300), mac(warp=0.3), tape_type('Ferric'), tape(speed=0.5, feed=0.65, mix=0.5),
  pedal('warble', rate=0.4, depth=0.25, mix=0.4), pedal('hall', decay=7.0, tone=0.35, mix=0.25))

P(HM, 'Old Home Movie', 'A pad on a cheap dictaphone tape: hiss, dropouts and a faint projector crackle.',
  dco_pad(cutoff=1300), mac(warp=0.4, haze=0.25), tape_type('Worn Dictaphone'), tape(feed=0.6, mix=0.5),
  atm(level=0.45, crackle=0.5, rain=0.0, hum=0.15), pedal('hall', decay=6.0, tone=0.3, mix=0.25))

P(HM, 'Wobbly Choir', 'Vowel-like strings with a heavy warble. Hold chords in the middle of the keyboard.',
  ens(ensFeet="8'", ensBright=0.35, cutoff=1500, resonance=0.45, attack=1.2, release=3.0), mac(warp=0.35),
  tape_type('Ferric'), tape(feed=0.65, mix=0.45),
  pedal('warble', rate=0.25, depth=0.45, mix=0.55, loop=True, loopAmt=0.2), pedal('hall', decay=10.0, tone=0.3, mix=0.3))

P(HM, 'Tidepool', 'VCO pad behind a filter that slowly opens and closes by itself.',
  vco_pad(cutoff=1400), mac(warp=0.25), tape_type('Ferric'), tape(feed=0.7, mix=0.5),
  pedal('drift', freq=1500.0, reso=0.35, lfoRate=0.03, lfoDepth=0.6), pedal('hall', decay=10.0, tone=0.35, mix=0.3))

P(HM, 'Paper Lanterns', 'All three string registers, blooming slowly. Big held chords.',
  ens(ensFeet="16' + 8' + 4'", ensBright=0.4, cutoff=3500, attack=1.0, release=4.0), mac(bloom=0.45, warp=0.2),
  tape_type('Ferric'), tape(feed=0.7, mix=0.5), pedal('hall', decay=18.0, tone=0.3, mix=0.35, loop=True, loopAmt=0.15))

P(HM, 'Nostalgia Machine', 'A Juno pad with movement built in: vibrato and a breathing pulse width.',
  dco_pad(cutoff=1700, pwm=0.6), mac(motion=0.35, warp=0.3), tape_type('Ferric'), tape(feed=0.65, mix=0.45),
  pedal('hall', decay=9.0, tone=0.35, mix=0.3, loop=True, loopAmt=0.12))

P(HM, 'Broken Music Box', 'Thin, plinky bells on a badly stretched tape. Short melodies.',
  dco_bell(cutoff=600, pw=0.08, resonance=0.45), mac(warp=0.5), tape_type('Worn Dictaphone'),
  tape(mode='Echo', time=0.6, feed=0.6, mix=0.45, wow=0.7),
  pedal('warble', rate=0.6, depth=0.35, mix=0.5, mode='Vibrato'), pedal('hall', decay=5.0, tone=0.35, mix=0.3))

P(HM, 'Bicycle Spokes', 'Short plucks for arpeggios. Loops lock to the tempo in 6, 5 and 4 beats.',
  dco_keys(decay=0.9, sustain=0.0, release=0.6, fDecay=0.5, cutoff=1400), mac(warp=0.2),
  tape_type('Ferric'), tape(feed=0.6, mix=0.5), loops(beats=(6, 5, 4)),
  pedal('warble', rate=0.5, depth=0.2, mix=0.35), pedal('hall', decay=4.0, tone=0.4, mix=0.25))

P(HM, 'Sunbleached', 'A soft, hazy VCO pad, like a photo left on the dashboard.',
  vco_pad(cutoff=1600, osc2Wave=0.6), mac(haze=0.4, warp=0.3, bloom=0.2), tape_type('Ferric'), tape(feed=0.7, mix=0.5),
  pedal('hall', decay=12.0, tone=0.3, mix=0.35, loop=True, loopAmt=0.15))

P(HM, 'Long Drive North', 'Long free-running loops of 9, 14 and 21 seconds that slowly drift apart.',
  vco_pad(cutoff=1100, lfoFilter=0.2, lfoRate=0.06, lfoShape='Sine'), mac(warp=0.25),
  tape_type('Ferric'), tape(sync='Free', feed=0.75, mix=0.5), loops(seconds=(9.0, 14.0, 21.0)),
  pedal('hall', decay=10.0, tone=0.3, mix=0.3))

P(HM, "Children's TV", 'A square-wave mono melody with glide. Play a tune; the mod wheel adds vibrato.',
  dco_keys(voiceMode='Mono', glide=0.08, saw=0.0, pulse=1.0, pw=0.5, sub=0.4, attack=0.01, decay=1.0, sustain=0.8,
           release=0.5, cutoff=1500, filterEnv=0.3),
  mac(warp=0.3), tape_type('Ferric'), tape(feed=0.55, mix=0.45),
  pedal('warble', rate=0.45, depth=0.3, mix=0.5), pedal('hall', decay=4.0, tone=0.4, mix=0.25))

P(HM, 'Afterglow Reel', 'A wide Juno pad with a faint octave shimmer growing on the loops.',
  dco_pad(cutoff=1800), mac(warp=0.3, bloom=0.3), tape_type('Ferric'), tape(feed=0.75, mix=0.5),
  pedal('cloud', pitch='Octave Up', smear=0.7, density=0.5, mix=0.2, loop=True, loopAmt=0.2),
  pedal('hall', decay=12.0, tone=0.35, mix=0.3))

# ==================================================================================================
# Machine Drones: Cortini/NIN. Resonant VCOs, slow filters, grit, long feedback.
# ==================================================================================================
P(MD, 'Iron Lung Hum', 'A heavy two-oscillator drone with an octave below. Hold one low note.',
  vco_drone(), mac(warp=0.2), tape_type('Ferric'), tape(feed=0.88, mix=0.55),
  pedal('dirt', drive=0.5, blend=0.35, tone=0.35, mix=0.7), pedal('hall', decay=14.0, tone=0.25, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Servo Choir', 'Low string registers through a band-pass filter that sweeps on its own.',
  ens(ensFeet="16' + 8'", ensBright=0.4, cutoff=5000, attack=2.0, release=5.0), mac(warp=0.15),
  tape_type('Ferric'), tape(feed=0.8, mix=0.5),
  pedal('drift', freq=900.0, reso=0.55, mode='Bandpass', lfoRate=0.04, lfoDepth=0.7),
  pedal('cloud', pitch='Octave Down', smear=0.7, density=0.5, mix=0.25), pedal('hall', decay=16.0, tone=0.25, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Cold Circuit', 'A hard-synced VCO sweeping on its filter envelope. Slow attack, cold and metallic.',
  vco_drone(sync=True, polyModPitch=0.35, osc2Semi=0, osc2Detune=0.0, osc2Level=0.3, filterEnv=0.4, fAttack=3.0,
            fDecay=6.0, fSustain=0.3, cutoff=900, resonance=0.4),
  mac(warp=0.1), tape_type('Chrome'), tape(feed=0.85, mix=0.5), pedal('hall', decay=12.0, tone=0.3, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Voltage Garden', 'Random stepped filter movement, like a sample-and-hold on a modular.',
  vco_pad(cutoff=900, resonance=0.55, lfoShape='Random', lfoRate=3.0, lfoFilter=0.35), mac(warp=0.15),
  tape_type('Ferric'), tape(feed=0.8, mix=0.5),
  pedal('drift', freq=1800.0, reso=0.3, lfoRate=0.02, lfoDepth=0.5), pedal('hall', decay=10.0, tone=0.3, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Grey Static Field', 'Noise and resonance: wind through a broken machine. Hold chords low.',
  dco_pad(saw=0.5, pulse=0.0, sub=0.2, noise=0.7, cutoff=700, resonance=0.65, filterEnv=0.2, chorus='I'),
  mac(haze=0.2), tape_type('Worn Dictaphone'), tape(feed=0.85, mix=0.55),
  pedal('hall', decay=25.0, tone=0.25, mix=0.4, loop=True, loopAmt=0.2), order=ORDER_DRONE)

P(MD, 'Sub Station', 'A legato bass drone with glide and fuzz. Play slow bass lines.',
  vco_lead(osc2Semi=-12, osc2Wave=0.0, osc2Level=0.8, cutoff=500, resonance=0.45, drive=0.7, glide=0.25,
           lfoPitch=0.0, filterEnv=0.25),
  mac(warp=0.1), tape_type('Ferric'), tape(feed=0.75, mix=0.45),
  pedal('dirt', drive=0.6, blend=0.45, tone=0.3, mix=0.7), pedal('hall', decay=8.0, tone=0.2, mix=0.2),
  order=ORDER_DRONE)

P(MD, 'Rust Engine', 'Fifths, heavy drive and a crushed loop. Industrial and loud.',
  vco_drone(osc2Semi=7, osc2Detune=3.0, drive=0.8, cutoff=800), mac(warp=0.15), tape_type('Ferric'),
  tape(feed=0.85, mix=0.55, saturation=0.5),
  pedal('dirt', drive=0.65, blend=0.8, tone=0.45, mix=0.6, loop=True, loopAmt=0.15),
  pedal('hall', decay=10.0, tone=0.25, mix=0.25), order=ORDER_DRONE)

P(MD, 'Signal Decay', 'Loops that fall apart: heavy wear and dropouts on every pass.',
  vco_pad(cutoff=1300, resonance=0.35), mac(warp=0.2), tape_type('Worn Dictaphone'),
  tape(feed=0.9, mix=0.55, age=0.75, dropouts=0.6), pedal('hall', decay=12.0, tone=0.3, mix=0.3), order=ORDER_DRONE)

P(MD, 'Reverse Cathedral', 'The tape runs backwards into a huge hall. Play a chord and listen to it bloom in reverse.',
  ens(ensFeet="16' + 8' + 4'", ensBright=0.35, cutoff=4000, attack=1.5, release=4.0), mac(bloom=0.3),
  tape_type('Ferric'), tape(speed=-1.0, feed=0.8, mix=0.6),
  pedal('cloud', pitch='Octave Up + Down', smear=0.8, density=0.5, mix=0.25),
  pedal('hall', decay=30.0, tone=0.25, mix=0.45), order=ORDER_DRONE)

P(MD, 'Half-Speed Machine', 'Resonant pad on a tape at half speed, slowly grinding down.',
  vco_pad(cutoff=800, resonance=0.6, drive=0.5), mac(warp=0.2), tape_type('Ferric'),
  tape(speed=0.5, feed=0.85, mix=0.55),
  pedal('dirt', drive=0.4, blend=0.3, tone=0.35, mix=0.5), pedal('hall', decay=14.0, tone=0.25, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Modular Weather', 'Stepped random filter and a slow drifting band-pass. Never the same twice.',
  vco_drone(osc2Semi=12, osc2Level=0.4, lfoShape='Random', lfoRate=1.2, lfoFilter=0.5, cutoff=700, resonance=0.6),
  mac(warp=0.15), tape_type('Chrome'), tape(feed=0.85, mix=0.5),
  pedal('drift', freq=1200.0, reso=0.45, mode='Bandpass', lfoRate=0.015, lfoDepth=0.8),
  pedal('hall', decay=14.0, tone=0.3, mix=0.3), order=ORDER_DRONE)

P(MD, 'Black Mirror Pad', 'Dark pulse-width pad with a high resonance and a slow sweep.',
  vco_pad(osc1Wave=1.0, osc2Wave=1.0, pw=0.3, lfoPwm=0.5, lfoRate=0.2, cutoff=650, resonance=0.6, attack=2.5,
          release=5.0), mac(haze=0.15), tape_type('Ferric'), tape(feed=0.85, mix=0.55),
  pedal('hall', decay=18.0, tone=0.2, mix=0.35, loop=True, loopAmt=0.2), order=ORDER_DRONE)

P(MD, 'Coolant Leak', 'Synced and cross-modulated VCOs into a long tape echo. Metallic and dripping.',
  vco_pad(sync=True, polyModPitch=0.5, osc2Semi=-5, osc2Level=0.25, filterEnv=0.5, fAttack=0.01, fDecay=2.0,
          fSustain=0.2, attack=0.01, decay=2.5, sustain=0.5, release=3.0, cutoff=1100, resonance=0.35),
  mac(warp=0.1), tape_type('Chrome'), tape(mode='Echo', time=0.78, feed=0.75, mix=0.45),
  pedal('dirt', drive=0.4, blend=0.0, tone=0.4, mix=0.5), pedal('hall', decay=10.0, tone=0.3, mix=0.3),
  order=ORDER_DRONE)

P(MD, 'Tension Wire', 'A filter on the edge of whistling, swept by a slow band-pass. Hold single high-ish notes.',
  vco_drone(osc2Semi=0, osc2Level=0.5, resonance=0.78, cutoff=900, drive=0.3),
  mac(warp=0.1), tape_type('Chrome'), tape(feed=0.85, mix=0.5),
  pedal('drift', freq=2500.0, reso=0.5, mode='Bandpass', lfoRate=0.05, lfoDepth=0.6),
  pedal('hall', decay=16.0, tone=0.3, mix=0.35), order=ORDER_DRONE)

P(MD, 'Ghost in the Rack', 'Dark strings with shimmer above and below. Very long hall.',
  ens(ensFeet="16' + 8'", ensBright=0.25, cutoff=2500, attack=2.0, release=6.0), mac(haze=0.2, bloom=0.3),
  tape_type('Ferric'), tape(feed=0.8, mix=0.55),
  pedal('cloud', pitch='Octave Up + Down', smear=0.75, density=0.55, mix=0.3, loop=True, loopAmt=0.2),
  pedal('hall', decay=35.0, tone=0.2, mix=0.4), order=ORDER_DRONE)

P(MD, 'Feedback Hymn', 'Frippertronics: long free loops at very high feedback. Layer notes slowly, they stay.',
  dco_pad(cutoff=1600, attack=0.8), mac(warp=0.15), tape_type('Chrome'),
  tape(sync='Free', feed=0.95, mix=0.6), loops(seconds=(8.3, 12.7, 19.1)),
  pedal('hall', decay=10.0, tone=0.35, mix=0.3), order=ORDER_DRONE)

P(MD, 'Dead Channel', 'Noise through a crushing pedal and a resonant filter. Texture, not notes.',
  dco_pad(saw=0.3, pulse=0.0, sub=0.0, noise=1.0, cutoff=1200, resonance=0.7, chorus='Off', lfoFilter=0.3,
          lfoRate=0.1, lfoShape='Sine'), mac(),
  tape_type('Worn Dictaphone'), tape(feed=0.8, mix=0.5),
  pedal('dirt', drive=0.5, blend=1.0, tone=0.4, mix=0.6), pedal('hall', decay=12.0, tone=0.3, mix=0.35),
  order=ORDER_DRONE)

P(MD, 'Pressure Drop', 'An inverted filter envelope: notes start dark and open up as they fade. Press harder on the keys to open it.',
  vco_pad(filterEnv=-0.45, cutoff=2600, resonance=0.45, fAttack=0.5, fDecay=4.0, fSustain=0.3, atToFilter=0.7),
  mac(warp=0.1), tape_type('Ferric'), tape(feed=0.8, mix=0.5),
  pedal('hall', decay=12.0, tone=0.3, mix=0.3, loop=True, loopAmt=0.15), order=ORDER_DRONE)

P(MD, 'Slow Machine Breathing', 'The filter rises and falls every twelve seconds, like breathing.',
  vco_drone(osc2Semi=0, osc2Detune=8.0, lfoRate=0.08, lfoFilter=0.55, cutoff=600, resonance=0.5),
  mac(warp=0.2), tape_type('Ferric'), tape(feed=0.85, mix=0.5),
  pedal('warble', rate=0.15, depth=0.3, mix=0.4), pedal('hall', decay=14.0, tone=0.25, mix=0.3), order=ORDER_DRONE)

P(MD, 'Industrial Lullaby', 'Fuzzy keys with a warbling vibrato and a big hall. Simple melodies.',
  dco_keys(cutoff=1300), mac(warp=0.2), tape_type('Ferric'), tape(feed=0.75, mix=0.5),
  pedal('dirt', drive=0.55, blend=0.5, tone=0.4, mix=0.65),
  pedal('warble', rate=4.5, depth=0.2, mix=0.5, mode='Vibrato'), pedal('hall', decay=12.0, tone=0.3, mix=0.35),
  order=ORDER_POST_DIRT)

# ==================================================================================================
# Night Bus: Burial. Rain, crackle, ghostly voices, short skippy loops, worn tape.
# ==================================================================================================
P(NB, 'Night Bus Home', 'Dark string machine in the rain. Hold minor chords.',
  ens(ensFeet="8'", ensBright=0.3, cutoff=2200, resonance=0.25, attack=1.0, release=3.0), mac(haze=0.3, warp=0.2),
  tape_type('Ferric'), tape(feed=0.7, mix=0.5),
  atm(level=0.5, crackle=0.45, rain=0.6, hum=0.0, tone=0.45),
  pedal('warble', rate=0.3, depth=0.3, mix=0.4), pedal('hall', decay=12.0, tone=0.25, mix=0.35))

P(NB, 'Rain on the Top Deck', 'A soft pad with heavy rain on the window.',
  dco_pad(cutoff=1100), mac(haze=0.35, warp=0.25), tape_type('Ferric'), tape(feed=0.7, mix=0.45),
  atm(level=0.6, crackle=0.2, rain=0.85, hum=0.0, tone=0.5, always=True),
  pedal('hall', decay=10.0, tone=0.25, mix=0.35))

P(NB, 'Sodium Lights', 'Orange-lit VCO pad through a tape echo. Dark and hazy.',
  vco_pad(cutoff=900), mac(haze=0.5, warp=0.2), tape_type('Ferric'),
  tape(mode='Echo', time=0.74, feed=0.65, mix=0.45), atm(level=0.3, crackle=0.5, rain=0.2),
  pedal('hall', decay=10.0, tone=0.25, mix=0.35))

P(NB, 'Last Train Vox', 'A vowel-like string voice with an octave shimmer: like a pitched-up sample. Hold notes high.',
  ens(ensFeet="8'", ensBright=0.3, cutoff=1100, resonance=0.6, attack=0.4, release=2.0), mac(warp=0.3),
  tape_type('Ferric'), tape(feed=0.65, mix=0.5), atm(level=0.35, crackle=0.6, rain=0.2),
  pedal('cloud', pitch='Octave Up', smear=0.5, density=0.6, mix=0.35),
  pedal('hall', decay=9.0, tone=0.3, mix=0.35), order=ORDER_CLOUD_FIRST)

P(NB, 'Garage Ghosts', 'Short chord stabs on skippy loops of 6, 5 and 3 beats. Play off-beat stabs.',
  dco_keys(decay=0.5, sustain=0.0, release=0.4, fDecay=0.35, cutoff=1500, chorus='II'), mac(haze=0.2),
  tape_type('Ferric'), tape(feed=0.6, mix=0.55), loops(beats=(6, 5, 3)),
  atm(level=0.35, crackle=0.6, rain=0.3), pedal('hall', decay=6.0, tone=0.3, mix=0.3))

P(NB, 'Pirate Radio Fade', 'A pad on a dictaphone with mains hum and crackle. Fading broadcast.',
  dco_pad(cutoff=1200), mac(haze=0.35, warp=0.35), tape_type('Worn Dictaphone'), tape(feed=0.65, mix=0.5),
  atm(level=0.45, crackle=0.6, rain=0.0, hum=0.5, humFreq='50 Hz'),
  pedal('drift', freq=1800.0, reso=0.25, lfoRate=0.05, lfoDepth=0.35), pedal('hall', decay=7.0, tone=0.3, mix=0.25))

P(NB, 'Wet Pavement', 'Low strings behind a slow drifting filter and rain.',
  ens(ensFeet="16' + 8'", ensBright=0.35, cutoff=3000, attack=1.2, release=4.0), mac(haze=0.25, warp=0.2),
  tape_type('Ferric'), tape(feed=0.75, mix=0.5), atm(level=0.45, crackle=0.3, rain=0.7),
  pedal('drift', freq=1000.0, reso=0.35, lfoRate=0.025, lfoDepth=0.6), pedal('hall', decay=12.0, tone=0.25, mix=0.3))

P(NB, 'Estate Afterhours', 'A deep mono sub bass with a little hum underneath. Slow bass lines.',
  vco_lead(osc2Semi=-12, osc2Wave=0.0, osc2Level=0.7, cutoff=380, resonance=0.25, drive=0.3, glide=0.08, lfoPitch=0.0,
           filterEnv=0.2, decay=1.5, sustain=0.9, release=1.0),
  mac(haze=0.2), tape_type('Ferric'), tape(feed=0.55, mix=0.4), atm(level=0.3, crackle=0.3, hum=0.4),
  pedal('hall', decay=5.0, tone=0.2, mix=0.2))

P(NB, 'Crackle and Hiss', 'Keys on a well-worn record: heavy crackle and tape hiss.',
  dco_keys(cutoff=1100), mac(haze=0.3, warp=0.25), tape_type('Ferric'), tape(feed=0.65, mix=0.5, hiss=0.45),
  atm(level=0.6, crackle=0.85, rain=0.0, hum=0.1, always=True), pedal('hall', decay=8.0, tone=0.3, mix=0.3))

P(NB, '2AM Minicab', 'Chorused pad through a tape echo, running slightly slow.',
  dco_pad(cutoff=1300), mac(haze=0.3, warp=0.3), tape_type('Ferric'),
  tape(mode='Echo', time=0.68, feed=0.6, mix=0.45, speed=0.97),
  pedal('warble', rate=0.4, depth=0.3, mix=0.4), pedal('hall', decay=9.0, tone=0.3, mix=0.3))

P(NB, 'Underpass Choir', 'A choir of strings an octave down, echoing in a concrete hall.',
  ens(ensFeet="8' + 4'", ensBright=0.3, cutoff=1800, resonance=0.4, attack=1.5, release=4.0), mac(haze=0.3, bloom=0.3),
  tape_type('Ferric'), tape(feed=0.75, mix=0.5), atm(level=0.3, crackle=0.4, rain=0.4),
  pedal('cloud', pitch='Octave Down', smear=0.7, density=0.5, mix=0.3),
  pedal('hall', decay=20.0, tone=0.25, mix=0.4, loop=True, loopAmt=0.15))

P(NB, 'Fog Over the Estuary', 'A very hazy VCO pad in soft rain. Slow chords.',
  vco_pad(cutoff=1400), mac(haze=0.7, bloom=0.3, warp=0.2), tape_type('Ferric'), tape(feed=0.75, mix=0.5),
  atm(level=0.4, crackle=0.2, rain=0.6), pedal('hall', decay=16.0, tone=0.25, mix=0.35))

P(NB, 'Lost Text Message', 'Small pulse bells with a wobbly vibrato and a long echo.',
  dco_bell(cutoff=1000, pw=0.25), mac(haze=0.2, warp=0.3), tape_type('Ferric'),
  tape(mode='Echo', time=0.7, feed=0.65, mix=0.45), atm(level=0.3, crackle=0.5, rain=0.3),
  pedal('warble', rate=5.0, depth=0.15, mix=0.5, mode='Vibrato'), pedal('hall', decay=8.0, tone=0.3, mix=0.3))

P(NB, 'Siren Song in Rain', 'A gliding lead with a delayed vibrato. Sing a melody over the rain.',
  vco_lead(cutoff=1200, resonance=0.35, glide=0.2, lfoDelay=0.8, lfoPitch=0.3), mac(haze=0.3),
  tape_type('Ferric'), tape(feed=0.65, mix=0.45), atm(level=0.4, crackle=0.3, rain=0.6),
  pedal('hall', decay=12.0, tone=0.3, mix=0.35))

P(NB, 'Grey Morning', 'A gentle string pad that blooms slowly, with a little crackle.',
  ens(ensFeet="8' + 4'", ensBright=0.3, cutoff=3000), mac(bloom=0.5, haze=0.3, warp=0.2),
  tape_type('Ferric'), tape(feed=0.7, mix=0.5), atm(level=0.35, crackle=0.55, rain=0.15),
  pedal('hall', decay=12.0, tone=0.3, mix=0.3, loop=True, loopAmt=0.15))

# ==================================================================================================
# Utility: clean starting points.
# ==================================================================================================
P(UT, 'Init DCO', 'A plain DCO-Poly patch with a clean tape. Start here to build your own.',
  syn(model='DCO-Poly', chorus='I'), tape_type('Chrome'), tape(feed=0.6, mix=0.4),
  pedal('hall', decay=6.0, tone=0.35, mix=0.2))

P(UT, 'Init VCO', 'A plain VCO-Poly patch with a clean tape. Start here to build your own.',
  syn(model='VCO-Poly', chorus='Off'), tape_type('Chrome'), tape(feed=0.6, mix=0.4),
  pedal('hall', decay=6.0, tone=0.35, mix=0.2))

P(UT, 'Init Ensemble', 'A plain Ensemble patch with a clean tape. Start here to build your own.',
  ens(), tape_type('Chrome'), tape(feed=0.6, mix=0.4), pedal('hall', decay=6.0, tone=0.35, mix=0.2))

P(UT, 'Tape Echo', 'A plain tape echo with three heads. Good for the FX version on any track.',
  dco_keys(), tape_type('Ferric'), tape(mode='Echo', time=0.62, feed=0.45, mix=0.35))

P(UT, 'Frippertronics', 'Two long free loops at high feedback on clean tape. Layer slowly; it builds forever.',
  dco_pad(cutoff=2000, chorus='I'), tape_type('Chrome'), tape(sync='Free', feed=0.92, mix=0.55),
  loops(seconds=(11.0, 17.0, 17.0), mute=(False, False, True)))

P(UT, 'Dry Synth', 'The synth on its own: the tape is mixed out. Turn MIX up to bring the loops in.',
  dco_pad(), tape_type('Chrome'), tape(mix=0.0), pedal('hall', decay=6.0, tone=0.35, mix=0.25))

P(UT, 'Pedal Playground', 'Every pedal on at a moderate setting. Stomp them on and off to hear each one.',
  dco_pad(cutoff=1800), tape_type('Ferric'), tape(feed=0.65, mix=0.45),
  pedal('dirt', drive=0.35, blend=0.3, tone=0.45, mix=0.6), pedal('warble', rate=0.5, depth=0.3, mix=0.45),
  pedal('drift', freq=1500.0, reso=0.3, lfoRate=0.05, lfoDepth=0.5), pedal('cloud', pitch='Octave Up', mix=0.25),
  pedal('hall', decay=8.0, tone=0.35, mix=0.3))


# ---- write ---------------------------------------------------------------------------------------
def main():
    if os.path.isdir(ROOT):
        shutil.rmtree(ROOT)
    counts = {}
    names = set()
    for p in PRESETS:
        assert p['name'] not in names, 'duplicate name ' + p['name']
        names.add(p['name'])
        counts[p['bank']] = counts.get(p['bank'], 0) + 1
        folder = os.path.join(ROOT, p['bank'])
        os.makedirs(folder, exist_ok=True)
        safe = ''.join(c for c in p['name'] if c.isalnum() or c in ' -')
        path = os.path.join(folder, '%02d %s.json' % (counts[p['bank']], safe))
        with open(path, 'w') as f:
            json.dump(p, f, indent=2, sort_keys=False)
            f.write('\n')
    print('%d presets: %s' % (len(PRESETS), ', '.join('%s %d' % kv for kv in counts.items())))


if __name__ == '__main__':
    main()
