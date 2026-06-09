# OmniSwipe - Frei0r Transition Plugin for Kdenlive

An extremely versatile swipe transition plugin for Kdenlive. It's designed to cover pretty much every swipe or slide-related transition.

<p align="center">
<img width="447" height="447" alt="kdenlive_PEOP4WS4SR" src="https://github.com/user-attachments/assets/da127baa-1deb-4b5d-83e6-ce7e905ab78d" />
</p>

---
## Features
- Flexible per-clip directions: Independent axis and angle (0-180°) for incoming and outgoing clips.
- Carefully Designed Steps for Quick Setup: +0/+90+180 Angle steps, 180° max wheel value, etc.
- Background Clip behaviors: Static, Move (slide out), or Fade.
- Speed Curve: Adjustable 'linear to logarithmic' speed acceleration.
- Directional Motion Blur: Realistic blur synced to movement speed and direction.

## Parameters
- Clip 1 Axis / Direction Angle: Controls how the incoming clip enters.
- Clip 2 Axis / Direction Angle: Controls how the outgoing clip disappears (when moving).
- Clip 2 Behavior: Static / Move / Fade.
- Speed Curve**: Higher % = stronger acceleration.
- Motion Blur**: Strength of directional blur.

### Installation (Windows)
1. Download the build from the release. The zip file should have `omni-swipe.dll` and `camerashakeorganic.xml`.
2. Place `omni-swipe.dll` in Kdenlive frei0r plugins folder (e.g., kdenlive-master\lib\frei0r-1)
3. Place `omni-swipe.xml` in Kdenlive effects folder (e.g., kdenlive-master\bin\data\kdenlive\transitions)
4. Restart Kdenlive. The transition should appear under "Transitions".

### Important Warning
**This is NOT an official Kdenlive plugin.**  
This is a modified third-party plugin.  
  - It may stop working after Kdenlive or Frei0r updates.
  - Not tested on all systems or versions.
  - Use at your own risk.

### Disclaimers
- It doesn't support keyframe creation, but I couldn't figure out how to fix it. Since a lot of official Kdenlive plugins are broken in keyframe support, I suppose it's a bug on Kdenlive's side.
- It's just a tiny cosmetic issue, but the thumbnail preview is not generated in Kdenlive's Docker.

## License
- This plugin is made by acc4commissions with assistance from Grok 4.3 (xAI). 
- This project is licensed under the **GNU General Public License v3.0** (GPL-3.0).
- See the LICENSE for full details.
