# ROOM: marketing renders and videos

All of ROOM's product imagery: photoreal "hardware" renders of the plugin, cinematic close-ups, abstract card/box art, and 5-second product video clips. Everything was made in Higgsfield, with local Python/ffmpeg post-processing.

The final video download is in [`../ROOM_Videos_Final/`](../ROOM_Videos_Final/) (13 approved clips plus 2 flagged for retake).

## Folder map

| Folder | What's in it |
|---|---|
| `00_Source/` | `room_ui_reference.png`, the plugin UI screenshot every render is built from. Also the room-screen crop and the wordmark used as prompt references. `feedback/` holds the review notes and screenshots from the approval rounds. |
| `01_Studio/` | Four hero stills on pure black: three-quarter hero (**the master reference**), hero angle 2, front (4:3), top-down. |
| `02_Cinematic/` | 11 shallow-depth-of-field close-ups, in `full_res/` and `1920x1080/`. |
| `03_Abstract/` | Room cards and wireframe box as physical objects: A hero card, B ring, D card edge macro, I box alone. |
| `04_Video/` | Working video files: `*_final.mp4` deliverables, `raw/` Kling downloads, `SHOT_LIST.md`, and earlier attempts at clip 01 (v1–v7). |
| `05_Sharp_Plates/` | All-in-focus plates and built end frames used for the video tests. |
| `tools/` | Scripts (see below). |

## Brand and look (keep consistent)

- **Device:** matte near-black powder-coat slab with softly rounded edges and no screws. Raised black control cards, one raised warm off-white L-shaped cream plate, and black knurled knobs with a white dot and red value arc. Flush glass screens.
- **Room screen:** a crimson-to-black gradient with a thin white wireframe box and a red hatched floor. The six cards are BASEMENT SHOW (red), BELL LOFT (salmon), CLAP HALL (magenta with a red outline, the selected card), AFTER HOURS (hot pink), BACK CHAMBER and BACK ALLEY (dusty rose). Labels sit below the cards.
- **Type:** thin, wide-tracked geometric caps (Satoshi). The logo is the chunky lowercase "room" wordmark from `plugin/Design/brand/wordmark/`.
- **Light:** one key light from the left, shadows falling right, deep blacks.

## Stills pipeline

1. **Model:** Nano Banana Pro at 2K, 2 credits per image. Prompts reference `00_Source/room_ui_reference.png` plus the Hero 1 master.
2. **New camera angles:** GPT Image 2.6 at high quality (2.75 credits). It holds a requested camera while copying the layout. Nano Banana tends to copy the reference's framing.
3. **Text fixes:** the AI often misspells small labels (2.5.5, SYVC, DELL LOFT and so on). These are fixed by hand with the scripts in `tools/still_retouch/`, which redraw glyphs in Satoshi and repaint knob dots. That costs nothing, unlike a re-render.

**Lessons:**
- Attaching the Hero 1 image makes the model copy its camera. Leave it out when you need a new angle, or use GPT Image 2.6.
- The "only references" approach (a screen crop with no module) works for the abstract card/box shots.

## Video pipeline (final recipe)

1. **First frame:** the approved still, by its Higgsfield job ID.
2. **Model:** Kling 3.0 Pro, `sound: off`, 5 s, 8.75 credits.
3. **Prompt:** keep the product static and describe one slow, eased, motion-control move: a push past a foreground element, a pedestal rise over an edge, a truck along a bar, or a small orbit. The moves are modelled on slow-glide product reference films. The prompt also says "Real camera move, not a zoom" and asks for very shallow depth of field.
4. **`tools/dof_pass.py`:** adds a time-varying depth-of-field focus pull on top of Kling's output, with one config per clip in `tools/configs/`.
5. **Conform:** 1920x1080, 24 fps, 120 frames, no audio track. `tools/process_clip.sh` does download, conform, blur pass and check strip in one step.

**Lessons from the video tests (clip 01, v1–v7):**
- **Drifting text:** starting from a blurred still makes the model invent text when the focus lands on blurred areas.
- **No blur:** starting from an all-sharp clean plate loses the depth-of-field look.
- **No camera movement:** a last frame with the same framing as the first frame kills camera motion.
- **A zoom instead of a move:** a cropped last frame produces a zoom, not a camera move.
- **Invented hardware:** large orbits make the model invent hardware (shot 12) or change screen content (shot 11).
- **What worked:** the first frame plus a gentle prompted move plus the post blur pass. That's what shots 02–I use.
- **`tools/animate_plate.py`:** a fully local 2.5D animator. It's stable but looks flat; kept for reference.

## Shot list

See [`04_Video/SHOT_LIST.md`](04_Video/SHOT_LIST.md).

## Higgsfield job IDs (for re-rendering)

| Asset | Job |
|---|---|
| UI reference (uploaded media) | `dee339cd-62c7-443c-a7ce-5fdbfe6a7129` |
| Hero 1 master (uploaded, final) | `08cf9bbb-5d71-46c9-baa5-51c4fdd9fb05` |
| Cinematic 01 knob row | `f534936f-9323-4b65-8beb-b9891f181208` |
| Cinematic 02 TONE knob | `4f9954b4-e10e-4919-9998-8b2ff2ba58f1` |
| Cinematic 04 wireframe box | `59fd5ef7-d400-4662-b062-446c10f9d44e` |
| Cinematic 05 cream panel edge | `7b18872e-0994-404d-9b4f-1f0e36c2353b` |
| Cinematic 06 D R N LEDs | `a2be881b-87cc-4db5-9674-6d479ef4e558` |
| Cinematic 07 NOISE screen | `99b2b72f-36e6-4c56-b017-66f952b6e6a3` |
| Cinematic 08 top bar | `aa5741e0-215a-4693-946e-17fb9a320ce4` |
| Cinematic 09 small controls | `97472113-6e83-46a5-8d35-8727e72ad5b2` |
| Cinematic 10 STOP button | `de4120f4-4833-42a7-a1ef-311d3bb14c9b` |
| Cinematic 11 side profile | `b99edf7b-5a9e-4e23-b592-1af452bfe9cf` |
| Cinematic 12 wide hero (GPT 2.6) | `6d396208-42ec-485c-899b-1b3089238458` |
| Abstract A / B / D / I | `326acbb9-1ffe-4c45-9ae9-fccfd6ea70e5` / `4b495103-c6a8-49d3-9c12-a6a2db3b9916` / `c619f0a8-0b70-4fbc-9936-d07c6d1a90f3` / `4497c316-6e34-428c-b91f-61c0806901af` |

Video job IDs for the batch are in `04_Video/raw/batch_jobs.json`.

## Open items

- **Video 11 (side profile):** retake, because the NOISE waveform changes mid-clip.
- **Video 12 (wide hero):** retake, because the orbit invents cables and ports. Use a smaller move, or a last frame locked to a warped copy of the render.
- **Cinematic still 12:** add the red outline on the CLAP HALL card.

**Not in the repo:**
- Draft and rejected renders (`plugin/Design/renders/`, gitignored, about 220 MB).
- The stills download zip (over GitHub's 100 MB limit; the folder itself is committed).
- Third-party reference videos.
