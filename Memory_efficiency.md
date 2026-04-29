Good question, and one that actually matters for CrossLuaReader and any embedded work you do. The microcontroller mindset is fundamentally different from desktop C — every byte is visible, every allocation is a decision.

Here's the field manual.

## Memory layout — know where everything lives

Microcontroller memory is split into regions with very different costs:

- **Flash (.text, .rodata)** — large, slow, write-limited. Code lives here. Const data lives here. *Use this aggressively.*

- **SRAM (.data, .bss, stack, heap)** — small, fast, precious. Everything mutable lives here.

- **Sometimes external PSRAM** — slower than SRAM, larger, accessed over SPI/QSPI on ESP32-S3 etc.

First skill: **always know which region every variable lives in.** Run `arm-none-eabi-size` or equivalent on your binary and look at `.text`, `.data`, `.bss` sizes. Add `-Wl,-Map=output.map` to your linker flags and read the map file. The map file tells you exactly which symbol consumed which bytes. Most embedded developers never look at this. Looking at it weekly is one of the highest-leverage habits.

## Put const data in flash

The single biggest win, and the most commonly missed:

```
// BAD - copies the string into precious SRAM at boot
char* msg = "Battery low";

// GOOD - lives in flash, zero SRAM cost
const char* msg = "Battery low";

// BEST on AVR/some MCUs - explicitly placed
const char msg[] PROGMEM = "Battery low";
```

For lookup tables, font data, configuration defaults, anything you don't write to — `const` it and place it in flash. On ESP32 use `static const` (often goes to flash automatically with `-fdata-sections`). On AVR you need `PROGMEM` and `pgm_read_*` accessors. On ARM Cortex-M, `const` usually does it but check your linker script.

For CrossLuaReader specifically: your fonts, language packs, default UI strings, .cfont tables — all flash. Never copy to RAM unless you're actively rendering.

## No malloc, or malloc carefully

`malloc()` on a microcontroller is a trap. Heap fragmentation will kill long-running systems. Three strategies, in order of preference:

**1. No dynamic allocation at all.** Pre-allocate everything at compile time. Static arrays, fixed-size buffers. Your maximum memory usage is provable from the binary.

**2. Allocate once at boot, never free.** If you need dynamic sizing but the size is known after boot, allocate once during init. No fragmentation because you never free.

**3. Pool / arena allocators.** If you genuinely need dynamic allocation during runtime, use a pool (fixed-size blocks) or an arena (bump allocator that frees in bulk). Never `malloc/free` individual variable-size objects.

```
// Arena: linear allocator, free everything at once
typedef struct {
    uint8_t* base;
    size_t size;
    size_t used;
} arena_t;

void* arena_alloc(arena_t* a, size_t n) {
    if (a->used + n > a->size) return NULL;
    void* p = a->base + a->used;
    a->used += n;
    return p;
}

void arena_reset(arena_t* a) { a->used = 0; }
```

This pattern alone solves 80% of dynamic-memory needs in embedded C. Allocate the arena once (statically), bump-allocate from it, reset between major operations.

## Pick the right integer types

Default `int` is 32-bit on ARM Cortex-M, often 16-bit on AVR. Don't use `int` casually:

```
// BAD
int temperature_celsius;       // 4 bytes for a value that fits in 1
int day_of_week;               // 4 bytes for 0-6

// GOOD
int8_t temperature_celsius;    // 1 byte
uint8_t day_of_week;           // 1 byte
```

Use `<stdint.h>` everywhere. `uint8_t`, `uint16_t`, `int32_t`. Be explicit about size. On a struct holding a few thousand records, swapping `int` for `uint8_t` on the right field can save kilobytes.

## Pack structs (carefully)

Compiler aligns struct members to natural boundaries by default. This wastes memory:

```
struct unaligned {
    uint8_t  a;      // offset 0
    uint32_t b;      // offset 4 (3 bytes padding!)
    uint8_t  c;      // offset 8
};                   // sizeof = 12, with 6 bytes of padding

struct aligned_well {
    uint32_t b;      // offset 0
    uint8_t  a;      // offset 4
    uint8_t  c;      // offset 5
};                   // sizeof = 8, with 2 bytes of trailing padding
```

**First, just reorder fields largest-to-smallest.** Free win, no downsides.

**Second, only if needed**, use `__attribute__((packed))`:

```
struct __attribute__((packed)) tight {
    uint8_t  a;
    uint32_t b;
    uint8_t  c;
};                   // sizeof = 6
```

Packed structs save space but unaligned access on some MCUs (Cortex-M0, AVR) is slow or illegal. Don't pack everything reflexively — pack data that gets stored/transmitted, not data accessed in tight loops.

## Bitfields and bit packing

For flags and small enums, pack them:

```
// 4 bytes
struct {
    bool is_dirty;
    bool is_valid;
    bool needs_redraw;
    uint8_t mode;       // 4 possible values
} state;

// 1 byte (or 4 bits if you're aggressive)
struct {
    uint8_t is_dirty     : 1;
    uint8_t is_valid     : 1;
    uint8_t needs_redraw : 1;
    uint8_t mode         : 2;
    uint8_t reserved     : 3;
} state;
```

Bitfield ordering and ABI vary across compilers, so don't use them for wire/file formats. Use them for in-memory state.

For wire formats use explicit bitwise ops (`x |= (1 << 3)`) — portable, predictable.

## Stack discipline

Stack overflows are silent killers on microcontrollers. There's usually no MMU to catch them. Two practices:

**Watermark the stack.** Fill it with a known pattern at boot (`0xDEADBEEF`), then check the highest used address periodically:

```
extern uint8_t _stack_start, _stack_end;

void stack_paint(void) {
    for (uint8_t* p = &_stack_start; p < &_stack_end; p++) *p = 0xA5;
}

size_t stack_high_water(void) {
    uint8_t* p = &_stack_start;
    while (*p == 0xA5 && p < &_stack_end) p++;
    return &_stack_end - p;
}
```

Print the high-water mark from a debug command. Lets you size stack correctly instead of guessing.

**Avoid large local variables.**

```
void render(void) {
    uint8_t framebuffer[800 * 480 / 8];   // 48KB on the stack — disaster
}
```

Move it to a static, a heap allocation at init, or a pool. Stack is for small, short-lived things.

**Avoid recursion.** Or bound it explicitly. Recursive parsers in microcontroller-land are a frequent crash source.

## Avoid the standard library when you can

`printf` alone can drag in 10-30KB of code and use kilobytes of stack. On a microcontroller, that's catastrophic.

- Use `snprintf` over `printf` (no stdout buffer).

- Use a tiny printf alternative like `mpaland/printf` or `eyalroz/printf` — drop-in replacement, \~3KB code, no malloc.

- Avoid `stdio` file operations.

- Avoid `<math.h>` doubles; use floats or fixed-point.

- Avoid `<string.h>` if it's pulling in a heavyweight memcpy — write your own for small known sizes.

The compiler's `--specs=nano.specs` (newlib-nano) on ARM is mandatory for size-constrained builds.

## Floats vs fixed-point

If your MCU has no FPU (Cortex-M0, M3, AVR, ESP32-C3 in some configs), every floating-point operation gets emulated in software. Slow and brings in big libraries.

Use **fixed-point arithmetic** instead:

```
// Q16.16 fixed point: upper 16 bits integer, lower 16 bits fraction
typedef int32_t fix16_t;
#define FIX16_ONE 65536

fix16_t fix_mul(fix16_t a, fix16_t b) {
    return (fix16_t)(((int64_t)a * b) >> 16);
}
```

Or use `libfixmath`. For UI positioning, sensor scaling, audio — fixed-point is plenty. Save the FPU for things that genuinely need it.

## Power-of-two sizes

Modulo and division are expensive on MCUs without hardware division (AVR, Cortex-M0). Power-of-two sizes let the compiler use shift/mask instead:

```
#define BUF_SIZE 256                    // power of 2
uint8_t buf[BUF_SIZE];
size_t i = 0;
buf[i++ & (BUF_SIZE - 1)] = byte;       // shift-mask, fast
                                        // vs: i++ % BUF_SIZE -- slow division
```

Ring buffers, hash tables, lookup tables — pick power-of-two sizes when you can.

## Read the datasheet's memory map

Many MCUs have multiple RAM banks with different speeds and access patterns. ESP32 has DRAM, IRAM, RTC RAM. STM32 has main SRAM, CCM RAM (faster, no DMA). Cortex-M7 has TCM (tightly-coupled memory, single-cycle access).

Linker attributes let you place specific data in specific banks:

```
// ESP32: force into IRAM (executable RAM, faster code)
void IRAM_ATTR fast_isr(void) { /* ... */ }

// STM32 with CCM
__attribute__((section(".ccmram"))) static uint8_t fast_buffer[1024];
```

This isn't optimization theater — it's often the difference between code fitting and not fitting, or between an interrupt handler running fast enough or not.

## Toolchain habits

- **`-Os`** (optimize for size) over `-O2` for size-constrained builds. Often runs faster too because more code fits in cache.

- **`-ffunction-sections -fdata-sections`** + linker `-Wl,--gc-sections`. Strips unused code/data per-function instead of per-translation-unit. Can save 20-40% binary size with zero code changes.

- **`-flto`** (link-time optimization). Sometimes shrinks code, sometimes confuses debuggers. Try it.

- **`-Werror=stack-usage=512`** — fails build if any function uses more than 512 bytes of stack. Forces discipline.

- Run `nm --size-sort -t d binary.elf | tail -50` to see your largest symbols. Often surprising. Often actionable.

- `arm-none-eabi-objdump -h binary.elf` shows section sizes.

- `-fstack-usage` generates a `.su` file per source file with stack usage per function. Read these.

## A few specific C patterns

**Use `restrict` on hot loop pointers** — tells the compiler the pointers don't alias, lets it keep values in registers:

```
void blit(uint8_t* restrict dst, const uint8_t* restrict src, size_t n);
```

**Use `static` aggressively** — file-scope static functions are eligible for inlining, dead-code removal, and tighter call conventions.

**Prefer `const` everywhere** — gives the compiler optimization opportunities and lets it place data in flash.

**Avoid VLAs** (variable-length arrays). They live on the stack and you can't bound them at compile time.

**Don't use `enum` for flags you'll OR together** — `enum` is `int`-sized by default. Use `#define` or `static const uint8_t`.

## Practices to actually implement

If you wanted a checklist:

- Read the linker map file after every meaningful change

- Run `arm-none-eabi-size` in your CI so size regressions are visible

- Stack-paint at boot, print high-water marks in debug

- Set a stack-usage warning threshold (`-Werror=stack-usage=N`)

- Audit every variable for: does it need to be `int`? Does it need to be in RAM? Does it need to be mutable?

- Replace `printf` with a tiny printf or `snprintf` to a static buffer

- Reorder struct members largest-to-smallest by default

- Build with `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`

- Statically allocate. If you need dynamic, use an arena. If you need real malloc, you probably don't.

## Books worth reading

- **"Making Embedded Systems"** by Elecia White — the practical embedded C book most embedded engineers recommend

- **"Patterns for Time-Triggered Embedded Systems"** by Michael Pont — free PDF, 1000 pages, deep on the discipline aspects

- **"Hacker's Delight"** by Henry Warren — bit manipulation tricks. Useful when every cycle and byte counts.

- The Zephyr RTOS source code is a good read for "how do real embedded engineers structure C in 2026"

For your CrossLuaReader specifically, the highest-leverage things to audit are: the framebuffer (where does it live? flash vs RAM cost?), the font tables (in flash via `const`?), the Lua VM's heap (bounded? arena-based?), and the EPUB/text rendering pipeline (any per-page mallocs?). Walk through each with the question "what's the worst-case memory footprint" and you'll find the bottlenecks fast.