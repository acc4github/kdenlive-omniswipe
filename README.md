# OmniSwipe - Frei0r Transition Plugin for Kdenlive

An extremely versatile swipe/slide transition plugin for Kdenlive. It's designed to cover pretty much every swipe or slide-related transition.

## Features

- **Flexible per-clip directions**: Independent horizontal/vertical axis and angle (0-180°) for incoming and outgoing clips.
- **Carefully Designed Steps for Quick Setup**: Horizontal/Vertical options, 180° max value, etc.
- **Background Clip behaviors**: Static, Move (slide out), or Fade.
- **Speed Curve**: Adjustable 'linear to logarithmic' speed acceleration.
- **Directional Motion Blur**: Realistic blur synced to movement speed and direction.

### Installation (Windows)
1. Download the build from the release. The zip file should have `omni-swipe.dll` and `camerashakeorganic.xml`.
2. Place `omni-swipe.dll` in Kdenlive frei0r plugins folder (e.g., kdenlive-master\lib\frei0r-1)
3. Place `omni-swipe.xml` in Kdenlive effects folder (e.g., kdenlive-master\bin\data\kdenlive\transitions)
4. Restart Kdenlive. The transition should appear under "Transitions".

## Usage

- Drag the **OmniSwipe** transition onto the timeline between two clips. (A very short duration is recommended.)

<img width="447" height="447" alt="kdenlive_PEOP4WS4SR" src="https://github.com/user-attachments/assets/da127baa-1deb-4b5d-83e6-ce7e905ab78d" />


- Adjust parameters:
  - **Clip 1 Axis / Direction Angle**: Controls how the incoming clip enters.
  - **Clip 2 Axis / Direction Angle**: Controls how the outgoing clip disappears (when moving).
  - **Clip 2 Behavior**: Static / Move / Fade.
  - **Speed Curve**: Higher % = stronger acceleration.
  - **Motion Blur**: Strength of directional blur.

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).
See the LICENSE for full details.

## Credits

Developed for the open-source video editing community.  
Copyright (C) 2026 acc4commissions
