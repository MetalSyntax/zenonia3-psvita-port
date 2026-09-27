# `loader/audio.h` — Design Architecture & Notes

Explanatory and architectural design notes extracted from source code and replaced with concise technical Doxygen blocks. This document preserves the reasoning ('why') separated from technical API documentation.

## `audio_init` (line ~4)

**Source File:** `loader/audio.h`

> Port audio player, adapted from Zenonia 2 (same engine) with
> two real differences from Zenonia 3:
>
> 1. The APK .oggs are NOT all 22050 Hz: there are 44100 Hz mono (60) and
> 16000 Hz mono (17). Mixer runs at 44100 and resamples (linear)
> the voices that need it.
> 2. The dispatch replicates ZenoniaUIControllerView.OnSoundPlay (jadx):
> - vol == 0 && isLoop -> stopBGMSound() ("stop music" command)
> - sndID 1..15 -> SFX (SoundPool, overlap)
> - rest -> BGM (isLoop) or stream one-shot (!isLoop)
> and NexusSound.setVolume(vol/10): mVolume = (vol/10)/10.0f.
>
> The engine requests R.raw.s000 + sndID: Android resource IDs are consecutive
> in alphabetical order of the .ogg files that exist in res/raw, but the
> original filenames have gaps (s010, s019, ... are missing) and irregular
> suffixes (s116xx.ogg), so sndID is NOT the number embedded in the filename.
> Manually renaming files before install used to be required; audio_init()
> now scans ux0:data/zenonia3/sound/ once, sorts the *.ogg entries
> alphabetically, and reconstructs that same ID assignment directly from the
> APK's original res/raw filenames (verified: ordinal index 10 == s011.ogg).
> This means players can extract res/raw/ straight into
> ux0:data/zenonia3/sound/ with no renaming step -- a stale copy of an
> older or newer APK's audio in that folder (mismatched ordinal order) is
> the classic cause of missing BGM / wrong SFX playing (see
> port_progress.md Phase 5).

---