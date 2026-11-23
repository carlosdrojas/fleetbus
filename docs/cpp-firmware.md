# C++ in the firmware

The application logic is C++17. The vendor layers (FreeRTOS, TI DriverLib), the board support package and the
generated CAN codec are C. That split is the usual one in embedded C++: nobody rewrites the RTOS or the vendor driver
library, and the code you own and test is where C++ pays for itself.

| Layer | Files | Language | Why |
|---|---|---|---|
| Node logic | `firmware/common/*.hpp/.cpp` | C++17 | owned, tested on the host, where types and the compiler catch bugs |
| FreeRTOS application | `firmware/app/app_main.cpp`, `*_hw.cpp` | C++17 | task wiring; RAII around critical sections |
| Host simulator, tests | `firmware/host/`, `firmware/tests/*.cpp` | C++17 | drive the same classes the board runs |
| CAN codec | `firmware/common/can_msgs.{h,c}` | C, generated | produced by cantools from the DBC; never hand-edited |
| Board support | `firmware/bsp/` | C | thin calls into TI DriverLib (C); one `bsp.h` with an `extern "C"` guard |
| Bench images | `firmware/app/bringup/*.c` | C | bring-up tests of the BSP alone, before any of the C++ runs |
| RTOS, vendor SDK | `firmware/third_party/` | C | not ours |

## The subset

Compiled with `-fno-exceptions -fno-rtti -fno-threadsafe-statics` (`firmware/CMakeLists.txt`). In practice:

- **No heap after start-up.** The only allocations are FreeRTOS creating its five tasks from its 12 KB heap at boot.
  Containers are `std::array` and `StaticQueue<T, N>` (`common/static_queue.hpp`), sized at compile time.
- **No exceptions.** Every method is `noexcept`; failure is a return value (`push()` returns false and counts the drop).
- **No runtime constructors.** The node object is a global with a `constexpr` constructor, so it is constant-initialized
  into `.data` and nothing runs before `main()` except the copy. Checked: `.init_array` in both node images holds only
  the toolchain's `crtbegin` entry.
- **One virtual interface, on purpose.** `Canvas::fill()` (`common/gauge_view.hpp`) lets the same renderer draw to the
  ST7735 on the board and to a framebuffer in the tests. One indirect call per rectangle is nothing next to the SPI
  transfer it starts. The destructor is protected and non-virtual, so there is no deleting destructor and no `operator
  delete` in the image. `__cxa_pure_virtual` is defined in `app_main.cpp` so libsupc++'s version (which pulls in
  `abort()`) is not linked.
- **Compile-time roles.** `BatteryNode` and `BodyNode` are separate types sharing `NodeBase`; the image picks one with
  `using Node = ...` (`app/app.hpp`). No vtable, and the battery image contains no body code. Role-specific steps in
  `app_main.cpp` are small templates with `if constexpr`, so the other role's branch is discarded, never compiled in.
- **No atomic read-modify-write.** The Cortex-M0+ has no LDREX/STREX. Watchdog check-ins (`CheckIn` in `app_main.cpp`)
  have one writer each and use relaxed `std::atomic<uint32_t>` loads and stores, which compile to plain `ldr`/`str`.
  `fetch_add` would call into libatomic, which disables interrupts anyway.

## What C++ bought, concretely

| Feature | Where | What it prevents |
|---|---|---|
| RAII `CriticalSection` | `app_main.cpp` | a return path that forgets `taskEXIT_CRITICAL()` and leaves interrupts masked |
| `Latest<T>` snapshot template | `app_main.cpp` | tasks sharing a struct field by field; every read is a whole, consistent copy |
| `StaticQueue<T, N>` with `static_assert` | `static_queue.hpp` | an index type too small for the capacity; full queue is a counted drop, never a block |
| Strong types for inputs/outputs (`BatterySample`, `BodyOutputs`, `GaugeInputs`) | `common/` | passing raw `int`s in the wrong order (the old C API took `mv, ma, dc, ok` positionally) |
| `constexpr` geometry and ids with `static_assert` | `gauge_view.cpp`, `node.hpp` | a heartbeat id clash, a glyph table that doesn't match its character list, gauge maths that points the wrong way |
| `[[nodiscard]]` | queues, monitors | ignoring whether a push or pop succeeded |

## Cost

Measured with `make fw` on 2026-11-23 (arm-none-eabi-gcc 15.3, `-Os`): battery node 12,052 B flash, body node 15,488 B,
SRAM 13.7 KB of 32 KB in both (12 KB is the FreeRTOS heap). There is no like-for-like C build of the trimmed code to
compare against: the earlier C images also carried the OTA and signature code (28.9 KB and 32.6 KB), so the
difference says nothing about C versus C++.

What was checked: the display renderer was converted from C to C++ and the four reference screens it renders
(`make gauge-shots`) are byte-identical PNGs to the C version's.

## The C boundary

- `bsp.h` is C with an `extern "C"` guard. The C++ logic never includes it; only `app/` does.
- `fb_types.h` (enums shared with the BSP and the DBC) is plain C so both sides can include it.
- `can_msgs.h` is generated with its own `extern "C"` guard.
- `firmware/host/fbsim.h` is a C ABI over the C++ node classes, because Python's ctypes can only call C functions.
