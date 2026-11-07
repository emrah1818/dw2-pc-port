# 1.5.0-perf-test1: battle slowdown test build

This build continues from main at 82b4526. It preserves the current German translations,
Modern UI and Classic mode. No new font engine, third-party renderer or shader changes.

## Fix

The overload guard previously reduced the window loop's presentation frequency, but
Host_Present could still replay an interpolated frame. The game thread also kept preparing
its base-picture and VRAM copies. While the guard is active, the window now presents the
real game picture, replay logging/package preparation pauses, and an old package is
withdrawn. Recovery waits for a fresh package. The speed-up hold remains independent.
GPU fences and resource-retirement rules are unchanged.

## Why this first

The supplied leistung (1).csv contains 5,838 publish intervals. Of 593 intervals over 50 ms,
579 (97.64%) have neither new runtime tiles nor replacement uploads. Cache-full, cache-drop
and deferred-upload counters are zero. Total runtime glyph rasterization is 57.942 ms.
555 of those slow intervals have publish_ms over 15 ms. This justifies investigating the
submission/presentation path before replacing the text cache. It does not prove which
SDL call stalls, nor identify the exact measured build or its settings.

## Windows test

1. Extract the Windows test ZIP into its own folder. The program still uses the player's
   own game image/data and existing user settings. The ZIP contains no game data.
2. Run DW2-Leistung.bat and repeat the battle/attack that slowed down. Keep the current
   settings for this first comparison, then quit normally.
3. Return test_out/leistung.txt, test_out/leistung.csv and test_out/spiel.log.
4. If needed, repeat the same scene with only smooth frames disabled (F9). Each measurement
   is also saved in a dated test_out/leistung-* folder for comparison.

The new CSV retains the previous columns and appends game-thread CPU wall times for
submit_ms, fence_wait_ms, acquire_ms, build_ms, record_ms and package_ms, their counts,
and effective GPU/scale/smooth/late plus saved VSync/FPS/language/UI/sharpen/smooth2d
settings and version. vsync_setting is the saved preference, not the effective swapchain
mode: smooth presentation can enable VSync as well. Timings overlap with containing
flush/publish/package times; do not add them. They are not GPU execution timestamps.
The rows measure publish intervals, not necessarily distinct displayed game frames.

## Validation

The synthetic hd_test additionally checks actual GPU package publication, pause fallback,
absence of paused package copies, recovery without stale packages, and independence from
the speed-up hold. It runs on Windows and Linux CI with software Vulkan. This does not
replace a battle test on the affected GTX 1070. No FPS improvement is claimed before that
comparison. Existing font, script-layout and renderer checks remain enabled.
