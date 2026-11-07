# 1.5.0-perf-test2: separate presentation pacing from GPU submits

## Evidence from perf-test1

Source: the user's test_out.zip (leistung.csv, leistung.txt, spiel.log), logged on
2026-11-07 at 16:19. GTX 1070, driver 32.0.15.6603; i7-6700K; 1920x1080 output.
29,553 CSV intervals, 582.143 seconds summed interval duration. All rows identify
1.5.0-perf-test1 and GPU rendering. Effective scale is 4 for 29,523 rows (3 for 30),
smooth frames enabled, saved VSync enabled, FPS cap 120, sharpening 25.

| CPU wall-time category | Total |
|---|---:|
| SDL_SubmitGPUCommandBufferAndAcquireFence (game thread) | 195,057.721 ms |
| Explicit game-thread SDL_WaitForGPUFences | 1,936.932 ms |
| Command-buffer acquire | 366.958 ms |
| Vertex/command preparation | 746.504 ms |
| Upload/draw recording within hardware flush | 4,212.244 ms |
| Runtime glyph rasterization | 564.514 ms |
| Palette conversion | 224.771 ms |
| Modern UI tile styling | 1,254.177 ms |

These are nested CPU timings, not independent GPU-execution durations: do not sum them
into a purported breakdown of GPU use. The submit API is the principal measured stall
location. 958 intervals exceed 50 ms; their mean submit time is 39.967 ms. Explicit fence
wait time averages only 0.208 ms in that group.

The overload pause reduces package generation: only 9 packages are counted among 5,692
rows whose ending late flag is set (flag transitions can fall inside a measured interval).
It does not remove the presentation/submission coupling. A different workload and longer
recording prevent a controlled claim of improvement from the old P95 to the new P95.

This longer recording also reveals secondary cold-cache problems: CPU tiles peak at
255.5 MiB, one full/drop event, 188 deferred/refused GPU-pool requests and two GPU-pool
restart messages. UI styling peaks at 90.304 ms; rasterization at 24.250 ms. Those can
explain individual new-text spikes, not the full 195-second submit total. They remain a
separate follow-up after presentation timing is measured again.

## Source mechanism and change

The pinned SDL 3.4.8 source has a shared submitLock in
src/gpu/vulkan/SDL_gpu_vulkan.c, VULKAN_Submit(): it holds the lock across vkQueueSubmit,
vkQueuePresentKHR and subsequent cleanup. A blocking window present can therefore make
an independent game-thread submit wait for the same lock. The current measurements locate
the stall in the submit API; they do not distinguish mutex acquisition from the driver
call or cleanup inside SDL. VSync-sized stalls and the source mechanism justify this
next experiment, not an assertion that a specific internal instruction has been profiled.

SDL 3.4.8 src/render/gpu/SDL_render_gpu.c, ChoosePresentMode(), chooses MAILBOX before
IMMEDIATE for SDL_SetRenderVSync(renderer, 0). perf-test2 checks mailbox support first.
When synchronization is requested and mailbox is available, it uses mailbox presentation
and paces the window thread to the refresh rate (or a lower user FPS limit) with a sleep
outside GPU API calls. Mailbox preserves complete-frame presentation; it can discard a
superseded queued picture. The saved VSync setting and 59.94-Hz game clock are unchanged.
Unsupported devices keep the existing VSync path; this patch does not force a tearing mode
when synchronization is requested. Existing fences and resource lifetimes stay intact.

New CSV fields: mailbox_pacing, window_present_ms, window_presents. Window times/counts
are atomic approximate sampling-window totals, independent of the game thread; they are
not GPU timestamps. spiel.log explicitly records mailbox/window-thread pacing when selected.

Primary SDL sources (release-3.4.8):
- https://github.com/libsdl-org/SDL/blob/release-3.4.8/src/gpu/vulkan/SDL_gpu_vulkan.c
- https://github.com/libsdl-org/SDL/blob/release-3.4.8/src/render/gpu/SDL_render_gpu.c

No SDL source, shader, renderer DLL or font asset is changed or copied into this patch.
German translations, Modern UI, Classic and user preferences remain present.

## Report-script correction

The red PowerShell message is a report-formatting bug, not lost CSV data. Division by
1000 outside the format operand let PowerShell apply arithmetic to the formatted string.
Compute durationSeconds first, then format it. Add -ReportOnly and -OutputDirectory for
reprocessing existing logs without launching the game. A real-script fixture regression
runs under Windows PowerShell 5.1 in both de-DE and en-US cultures in CI.

## Player test

Extract this test ZIP separately and run DW2-Leistung.bat, repeat the same battle and
attacks with the same settings, then quit. Return test_out/leistung.txt, leistung.csv and
spiel.log. Check that the header identifies 1.5.0-perf-test2. The next comparison should
show whether submit_ms and long warm-cache intervals drop; no FPS improvement is asserted
before that measurement. Persistent cold-font/style spikes will be evaluated separately.
