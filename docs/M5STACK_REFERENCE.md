# M5Stack Reference Boundary

This project borrows interaction ideas from M5Stack avatar projects, not full source trees.

## Use

- `stack-chan/m5stack-avatar`: MIT license. Use as visual/state-machine reference. Source: https://github.com/stack-chan/m5stack-avatar
- `m5stack/StackChan-BSP`: MIT license. Use as hardware/project reference for StackChan-style behavior. Source: https://github.com/m5stack/StackChan-BSP

## Do Not Vendor

- Arduino/M5GFX/M5Unified stacks.
- M5Stack board-specific CoreS3 BSP code.
- GPLv3 RoboEyes source code.

RoboEyes remains an effect reference only. Source: https://github.com/Harrison-Xu/M5Stack_RoboEyes

## Migrated Into This Project

- small avatar states: idle, listening, thinking, speaking, error
- eye geometry parameters
- blink openness
- gaze offset
- expression intensity

The implementation is original ESP-IDF C code under `firmware/esp32c3`.
