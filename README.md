# OmniSwipe - Frei0r Transition Plugin for Kdenlive

An extremely versatile swipe transition plugin for Kdenlive. It's designed to cover pretty much every swipe or slide-related transition.

<p align="center">

<img width="447" height="415" alt="kdenlive_MiB34PuMYO" src="https://github.com/user-attachments/assets/2d89b354-bc0d-4dd7-a2dd-b1509b8fdff5" />

</p>

---
## Features

- **Clip A Axis / Direction Angle**: Controls how the incoming clip enters.
- **Clip B Axis / Direction Angle**: Controls how the outgoing clip disappears (when moving).
- **Clip B Behavior**: Static / Move / Fade.
- **Speed Curve**: Higher % = stronger acceleration.
- **Motion Blur**: Strength of directional blur.
- **Edge Smoothing**: Smoothing out the edge pixels. On by default. (Note that it introduces dark edges on blurred clips due to Kdenlive's limitation.)
- **Invert**: Invert the effect.

### Installation (Windows)

1. Download the build from the release. The zip file should have `omni-swipe.dll` and `camerashakeorganic.xml`.
2. Place `omni-swipe.dll` in Kdenlive frei0r plugins folder (e.g., kdenlive-master\lib\frei0r-1)
3. Place `omni-swipe.xml` in Kdenlive effects folder (e.g., kdenlive-master\bin\data\kdenlive\transitions)
4. Restart Kdenlive. The transition should appear under "Transitions".

## Usage

- Drag the **OmniSwipe** transition onto the timeline between two clips. (A very short duration is recommended.)
- Adjust parameters:
  - **Clip 1 Axis / Wheel**: Controls the angle for the incoming clip.
  - **Clip 2 Axis / Wheel**: Controls the angle for the outgoing clip (when moving).
  - **Clip 2 Behavior**: Controls how the outgoing clip disappears -> Static / Move / Fade.
  - **Speed Curve**: Higher % = stronger acceleration.
  - **Gentle Arrival**: Higher % = stronger deceleration.
  - **Motion Blur**: Strength of directional blur.

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).
See the LICENSE for full details.

## Credits

Developed for the open-source video editing community.  
Copyright (C) 2026 acc4commissions

