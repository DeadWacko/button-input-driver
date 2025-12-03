# Button library for Raspberry Pi Pico

Small helper library that wraps basic GPIO setup and debounced button reads for Raspberry Pi Pico projects using the Pico SDK.

## Layout
- `include/buttonlib.h` — public API.
- `src/buttonlib.c` — implementation.
- `examples/main.c` — minimal usage example that mirrors button state to the onboard LED.
- `examples/CMakeLists.txt` — builds the example target and links it against the library.

## Building
```bash
mkdir -p build
cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build .
```

This produces a UF2 for the example target (`buttons.uf2`) in `build/`.

## Using in your own code
1. Link against the `buttonlib` target in your CMake project.
2. Include the header:
   ```c
   #include "buttonlib.h"
   ```
3. Initialize and poll:
   ```c
   button_t btn;
   button_init(&btn, 14, true, 20); // pin, active-low, 20 ms debounce
   bool pressed = button_read(&btn);
   ```
