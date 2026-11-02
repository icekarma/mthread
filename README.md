# MTHREAD: Preemptive Multithreading for MS-DOS (8086 / MASM 6.11)

`MTHREAD` is a compact, priority-based preemptive multithreading kernel, synchronization suite, interrupt-driven input library, and C/C++ framework for MS-DOS. Written in 8086/80186 assembly language for Microsoft Macro Assembler (MASM) 6.11 and Microsoft C/C++ 7.00 (with build support for Borland C++ 3.1 / TASM 4.1 and OpenWatcom), it supports all six 16-bit x86 memory models (`TINY`, `SMALL`, `MEDIUM`, `COMPACT`, `LARGE`, and `HUGE`).

---

## Project Layout & Build System

The NMAKE `Makefile` places intermediate object files, static libraries, and linker map files in `obj\`, and links executable demos (`.exe` or `.com` in `TINY` model) into `bin\`:

### Static Libraries (`obj\`)
* **`mthread.lib` (`mthread.asm`, `mthrwrap.cpp`):** Core preemptive scheduler, `INT 08h` timer and `INT 21h` DOS gatekeeper/swapper, BIOS reentrancy wrappers, termination/break/critical-error traps, synchronization primitives, and C++ lifecycle/thread/synchronization wrapper classes.
* **`vid.lib` (`video.c`, `videohlp.asm`):** Direct `B800h` color text-mode video output (`video_putc`, `video_puts`, `video_putu8`, `video_putu16`, `video_putu32`, `video_puthex8`, `video_ruler`) and BIOS cursor positioning (`video_gotoxy`), with automatic `OFFSET_DISPLAY` centering on >80x25 text modes (such as 80x43, 80x50, and 132x43).
* **`input.lib` (`kbd.asm`, `mouse.asm`):** Interrupt-driven keyboard (`INT 09h`) and mouse (`INT 33h`) handlers that wake threads via `event_set_isr()` with zero polling overhead.

### Demo Executables (`bin\`)
* **`demo` / `cppdemo`:** Preemptive compute workers, low-priority background spinner on a caller-supplied static stack, per-thread `STKHQQ` stack probe verification, concurrent `INT 21h` `DiskStack` file logging (`MTTEST.LOG`), and `IOStack` console output.
* **`inpdemo` / `cppinp`:** Zero-polling high-priority input worker woken via `event_set_isr()` from `INT 09h` keyboard and `INT 33h` mouse interrupts, featuring live BIOS modifier inspection, a 32-character typeahead buffer, and clickable mouse UI controls alongside background compute threads.
* **`syncdemo` / `cppsync`:** Showcase of manual-reset and auto-reset Win32 events, recursive mutexes with Priority Inheritance (`LOW` boosted to `HIGH` to defeat a `NORMAL`-priority CPU hog), bounded producer-consumer ring buffers using counting semaphores, and batch notifications via condition variables.

### Building with NMAKE
```dos
nmake                                  (Defaults: COMPILER=MSC, MODEL=SMALL, CPU=8086)
nmake MODEL=TINY                       (MSC 7.00, Tiny model .COM executables in bin\)
nmake MODEL=LARGE CPU=186              (MSC 7.00, Large model, 80186+ instructions)
nmake MODEL=HUGE CPU=186               (MSC 7.00, Huge model with pointer normalization)
nmake COMPILER=BCC MODEL=COMPACT       (Borland C++ 3.1 + TASM 4.1)
nmake COMPILER=WATCOM MODEL=SMALL      (OpenWatcom C/C++ + MASM 6.11)
nmake clean                            (Removes obj\, bin\, and temporary files)
```

---

## How the Implementation Works

### 1. 64-Byte TCB-Resident Context & Zero-Residue User Stacks
Unlike traditional real-mode context switchers that push all general-purpose registers onto the interrupted thread's stack, `MTHREAD` stores all 14 architectural 8086 registers (`AX` through `ES`, plus `SS`, `SP`, `IP`, `CS`, and `FLAGS`) inside a 64-byte **Thread Control Block (`TCB`)**:
* When `INT 08h` or a voluntary context switch (`soft_context_switch`) fires, the entry stub immediately pops the 3-word interrupt frame (`IP`, `CS`, `FLAGS`) off the interrupted thread's stack directly into its `TCB`, records the clean `SS:SP`, and switches `SS:SP` onto a dedicated 512-byte internal scheduler stack (`isr_stack`).
* **Benefit:** User thread stacks never have to absorb the stack footprint of the ROM BIOS timer ISR, chained `INT 1Ch` TSRs, or the scheduler itself.
* **Fast Indexing & Pointer Caching:** The active TCB pointer is cached in `current_tcb` for single-instruction lookup on interrupt entry, and indexing `tcb_table[tid]` requires only a 6-bit left shift (`SHL BX, 6` on 80186+ or `MOV CL, 6 / SHL BX, CL` on strict 8086) with no `MUL` instruction.

### 2. Hardware Timer (`INT 08h`), PIT Scaling, Specific EOI & Instant `HLT` Wakeup
`MTHREAD` hooks hardware `INT 08h` (IRQ0) directly rather than the BIOS user tick `INT 1Ch`:
* **Avoiding the `INT 1Ch` Double-EOI Hazard:** The IBM PC/XT/AT ROM BIOS `INT 08h` handler issues a Non-Specific EOI (`OUT 20h, 20h`) *after* `INT 1Ch` returns. Switching stacks inside `INT 1Ch` either starves the 8259 PIC of an EOI or causes a delayed Non-Specific EOI to clear an unrelated IRQ when the original thread eventually resumes.
* **Power-of-2 PIT Scaling:** `thread_init(pit_shift, main_prio)` reprograms Channel 0 of the 8253/8254 PIT to run at $1\times$ (`MT_TICK_18HZ`, 18.2 Hz), $2\times$ (`MT_TICK_36HZ`, 36.4 Hz), $4\times$ (`MT_TICK_73HZ`, 72.8 Hz), or $8\times$ (`MT_TICK_146HZ`, 145.6 Hz) the stock tick rate. On sub-ticks, `isr_timer` sends a **Specific EOI for IRQ0 (`OUT 20h, 60h`)** to the 8259 PIC; on every $2^{\text{pit\_shift}}$-th tick, it chains to the original ROM BIOS `INT 08h` handler on `isr_stack` so the BIOS time-of-day clock and floppy motor shutoff timer remain accurate.
* **Built-in `HLT` Idle Loop with Instant IRQ Dispatch:** When no thread is `STATE_READY`, the scheduler sets `current_tid = 0xFF` (`NO_THREAD`) and executes `STI / HLT` on `isr_stack`. As soon as *any* hardware interrupt (such as `INT 09h` keyboard or a mouse IRQ calling `event_set_isr()`) wakes the CPU from `HLT` and executes `IRET`, `idle_hlt_spin` immediately executes `CLI / JMP schedule_next_thread`, dispatching the newly woken thread within microseconds without waiting for the next `INT 08h` timer tick.

### 3. Priority-Based Round-Robin Scheduling
Each thread is assigned a priority from `0` (`MT_PRIO_IDLE`) to `7` (`MT_PRIO_HIGHEST`). During every scheduling pass:
1. The dispatcher begins scanning `tcb_table` at `(current_tid + 1) & 7`.
2. It selects the first `STATE_READY` thread whose effective priority (`tcb_prio`) is **strictly greater (`>`)** than the best priority seen so far during the scan.
3. Using strict `>` combined with a rotating start index guarantees fair round-robin time-slicing among threads of equal priority while always preempting in favor of higher-priority runnable threads.

### 4. Hybrid MS-DOS (`INT 21h`) Reentrancy & Stack-Tier Splitting
MS-DOS is non-reentrant because `INT 21h` switches `SS:SP` onto one of three internal 384-byte stacks inside the Swappable Data Area (SDA). `MTHREAD` hooks `INT 21h` and classifies every DOS call into four tiers:
* **Tier 0 (Stackless Calls: `AH = 33h, 50h, 51h, 62h, 64h`):** Passed straight through to DOS immediately with zero locking or swapping.
* **Disjoint Stack Tiers (`IOStack` vs. `DiskStack` vs. `AuxStack`):** Because DOS uses separate 384-byte stacks for character I/O (`AH = 01h..0Ch` on `IOStack`), file/memory calls (`AH >= 0Dh` on `DiskStack`), and error inquiries (`AH = 59h` or `ErrorMode != 0` on `AuxStack`), two threads in *different* tiers only collide on the **24-byte (`0018h`) SDA header** at `SDA+00h..17h` (which holds `user_SP`, `user_SS`, `InDOS`, `CurrentDTA`, and `CurrentPSP`). Each `TCB` embeds a 24-byte `sda_hdr` snapshot, allowing `swap_sda_headers` to swap that header in 12 `MOVSW` instructions when switching between threads in different DOS stacks.
* **Same-Tier `IOStack` Collision (`AH = 01h..0Ch`):** During `thread_init()`, `MTHREAD` discovers the SDA base via `INT 21h, AX=5D06h` and calibrates the exact `IOStack` offset via a one-time `AH=0Bh` probe intercepted by `isr_int16`. If Thread A is inside `IOStack` and Thread B enters `AH = 01h..0Ch`, `MTHREAD` exchanges Thread A's 16-byte console state (`SDA+18h..27h`) and 384-byte `IOStack` into a 400-byte `io_swap_buf` so Thread B can perform console I/O concurrently.
* **Same-Tier `DiskStack` / `AuxStack` Collision:** Because disk calls mutate global non-SDA kernel structures (SFTs, FAT cache, `BUFFERS=` LRU chain), a second thread requesting `DiskStack` or `AuxStack` blocks in `STATE_WAIT_DISK` / `STATE_WAIT_AUX` and applies **Priority Inheritance** to boost the lock owner's effective priority until it exits `INT 21h`.

### 5. ROM BIOS & System Interrupt Reentrancy Guards (`bios_lock`)
* Wrappers around `INT 10h` (Video), `INT 13h` (Disk), `INT 15h` (System Services), `INT 16h` (Keyboard), `INT 17h` (Printer), `INT 25h`/`26h` (Absolute Disk Read/Write, preserving the left-on-stack FLAGS convention), `INT 29h` (Fast Console Output), `INT 2Ah` (DOS Critical Sections `AH=80h/81h/82h` & Network), `INT 2Eh` (`COMMAND.COM` Execute), `INT 2Fh` (Multiplex), `INT 33h` (Mouse Driver, if present), and `INT 67h` (LIM EMS Driver, if present) increment `cs:[bios_lock]` on entry and decrement it on exit while preserving output flags via `RETF 2`.
* External hardware ISRs and driver callbacks (`kbd.asm` and `mouse.asm`) bracket their execution with `mt_enter_isr()` and `mt_leave_isr()`.
* If `INT 08h` fires while `bios_lock > 0`, `isr_timer` takes the non-preemptive `timer_locked_tick` fast path on the current stack—advancing PIT sub-ticks, chaining to BIOS `INT 08h` when due, and decrementing `sleep_ticks` without switching stacks or threads.

### 6. Multi-Layer Termination, Break & Critical Error Defense
Because MS-DOS only restores `INT 22h`, `INT 23h`, and `INT 24h` when a program exits, terminating without unhooking `INT 08h` or `INT 21h` would leave timer interrupts firing into freed memory. `MTHREAD` prevents this with four layers of defense:
1. **Idempotent `unhook_hardware` & `thread_abort()`:** Restores the 18.2 Hz PIT divisor, resets `STKHQQ` to the main thread's stack limit, clears `ErrorMode`, and unhooks all 20 interrupt vectors via `old_int21` (`AH=25h`). `thread_init()` automatically registers `thread_abort()` with the C runtime's `atexit()`.
2. **DOS Termination Traps:** `isr_int21` intercepts `AH=00h`, `AH=31h` (TSR), and `AH=4Ch`, and dedicated traps on `INT 20h` (which also catches CP/M-style `CALL 0005h` terminations) and `INT 27h` invoke `unhook_hardware` before chaining to DOS.
3. **Cooperative & Emergency `Ctrl+C` / `Ctrl+Break` (`INT 1Bh` & `INT 23h`):** `isr_int1b` sets `break_flag = 1`, increments `break_count`, and clears the BIOS break bit at `0040:0071h` to suppress `^C` screen corruption. `isr_int23` allows the first two `Ctrl+C` presses to be handled cooperatively via `thread_is_break_requested()`; on the 3rd press (`break_count >= 3`), it calls `unhook_hardware` and returns with `STC / RETF` to force DOS to abort the program cleanly.
4. **Critical Error Handler (`INT 24h`):** `isr_int24` suppresses the non-reentrant DOS `"Abort, Retry, Ignore, Fail?"` prompt, records the error code (`AH` and `DI`) for `thread_get_critical_error()`, and returns `AL = 03h` (Fail system call).

### 7. C Runtime Stack Probes (`STKHQQ`) & Lazy Heap Reaping
* **`STKHQQ` Synchronization:** Microsoft C 7.00 inserts calls to `__chkstk` at function prologues, comparing `SP` against `DGROUP:STKHQQ`. Every `TCB` records its stack's lower guard address (`stk_limit`) and updates `STKHQQ` on every context switch, providing per-thread stack overflow checking without requiring `/Gs`.
* **Lazy Zombie Stack Reaping:** When `thread_create()` allocates a stack via `malloc()` (`stack_ptr = NULL`), the exiting thread cannot call `free()` while still running on that stack. Instead, `thread_exit()` marks the slot `STATE_ZOMBIE` on `isr_stack`, and `reap_zombie_stacks` frees the block safely during the next `thread_create()`, `thread_join()`, `thread_join_all()`, or `thread_shutdown()`.

### 8. Memory Model Portability (`TINY` through `HUGE`)
`mthread.inc` inspects MASM's built-in `@Model`, `@CodeSize`, `@DataSize`, and `@Cpu` symbols to adapt code and data pointers automatically:
* **`TINY` Model (`.COM` Executables, `@Model == 1`):** Because `.COM` binaries cannot contain segment relocation fixups, `LOAD_DS_DGROUP` loads `DS` from `CS` (`mov ax, cs / mov ds, ax`) instead of `@data`, and the `Makefile` disables CodeView `/Zi` records and outputs `.com` targets.
* **`HUGE` Model (`@Model == 6`, `@DataSize == 2`):** `LOAD_DATA_PTR`, `thread_create`, `kbd_init`, and `mouse_init` invoke `NORMALIZE_FAR_PTR` (using unrolled 4-bit shifts on 8086) so that thread stacks and multi-byte synchronization/event structures never wrap a 64 KB segment boundary, while preserving the raw `malloc()` pointer in `stk_alloc_seg:stk_alloc_off` for `free()`.

### 9. Synchronization Primitives, Thread Joining & C++ Wrappers
All synchronization primitives use 1-byte bitmask wait queues (`1 << tid`), priority-ordered wakeups (`wake_highest_waiter`), direct ownership/token handoff, and immediate preemption (`preempt_if_higher`) when a woken thread outranks the caller:
* **Events (`mt_event_t` / `MEvent`):** Win32-style manual-reset and auto-reset events (`event_init`, `event_set`, `event_set_isr`, `event_reset`, `event_pulse`, `event_wait`).
* **Mutexes (`mt_mutex_t` / `MMutex` / `MLockGuard`):** Recursive mutexes with Priority Inheritance (`mutex_init`, `mutex_lock`, `mutex_trylock`, `mutex_unlock`) and scoped RAII lock guards.
* **Semaphores (`mt_sem_t` / `MSemaphore`):** 16-bit counting semaphores (`sem_init`, `sem_wait`, `sem_trywait`, `sem_post`).
* **Condition Variables (`mt_cond_t` / `MCondVar`):** Atomic recursive mutex release and re-acquisition (`cond_init`, `cond_wait`, `cond_signal`, `cond_broadcast`).
* **Thread Joining & Atomics:** `thread_join(tid)` and `thread_join_all()` block until target thread(s) exit and immediately reap zombie stacks; `cmp_xchg(ptr, old_val, new_val)` provides an interrupt-atomic compare-and-exchange primitive.

### 10. Interrupt-Driven Input (`input.lib`)
* **Keyboard (`kbd.asm` / `MKeyboard`):** Hooks `INT 09h` (bracketed by `mt_enter_isr` / `mt_leave_isr`), chains to the ROM BIOS to translate scan codes, and inspects the BIOS keyboard ring-buffer pointers at `0040:001Ah` (head) and `0040:001Ch` (tail) plus `thread_is_break_requested()`. When a keystroke or break is present, it wakes the registered `mt_event_t` via `event_set_isr()`.
* **Mouse (`mouse.asm` / `MMouse`):** Verifies that `INT 33h` is installed and not a bare `IRET` (`0CFh`), resets the driver, and registers a far callback via `INT 33h, AX=000Ch`. At IRQ time, `mouse_callback` updates a live state snapshot, enqueues events into a 16-slot ring buffer that **coalesces consecutive pure move events** (`MOUSE_EVT_MOVE`) so fast mouse motion never drops button clicks, and signals the registered `mt_event_t` via `event_set_isr()`.

---

## Limitations & Architectural Trade-Offs

1. **Maximum of 8 Concurrent Threads (`MAX_THREADS = 8`):**
   Wait queues (`wait_mask`) and active thread masks are represented as 8-bit bitmasks (`1 << tid`) so scheduler scans and wakeups execute in constant time without pointer manipulation. Supporting more than 8 threads requires widening `wait_mask` to 16 bits (`DW`) or switching to intrusive doubly-linked TCB queues.
2. **C Runtime Library (CRT) Non-Reentrancy:**
   While `MTHREAD` serializes and swaps MS-DOS (`INT 21h`) and guards ROM BIOS interrupts, standard single-threaded DOS C runtime functions that mutate static internal buffers or global heap structures (`malloc`/`free`, `printf`/`fopen` sharing the same `FILE` stream, `strtok`, `rand`, `localtime`) are not automatically thread-safe. Concurrent C heap or shared-stream calls from worker threads should be serialized with a `mt_mutex_t` / `MMutex`.
3. **8087 / 80287 FPU State Not Saved:**
   To keep the `TCB` at 64 bytes and context switches under ~1,000 cycles on a 4.77 MHz 8088, the 94-byte x87 math coprocessor state (`FSAVE`/`FRSTOR`) is not saved on context switches. Floating-point operations should either be confined to a single thread or guarded with a mutex.
4. **Two-Way `IOStack` Swap Buffer:**
   A single 400-byte `io_swap_buf` is statically allocated to resolve `IOStack` collisions between two concurrent console threads. If a *third* thread simultaneously enters `INT 21h` (`AH = 01h..0Ch`), it blocks in `STATE_WAIT_IO` (with priority inheritance) until one of the first two exits.
5. **Blocking Raw BIOS Calls (`INT 16h, AH=00h`):**
   Because `bios_lock` defers preemption while executing inside a raw BIOS interrupt, calling a blocking BIOS routine like `INT 16h, AH=00h` directly from a thread holds `bios_lock > 0` until a key is pressed. Applications should use `input.lib` (`kbd_init` + `event_wait` + `kbd_getkey`), poll via `_kbhit()` + `thread_sleep()`, or use DOS `INT 21h` console input.
6. **Single-Hop Priority Inheritance:**
   `mt_mutex_t` boosts a lower-priority mutex owner to the highest waiting thread's priority and restores `tcb_base_prio` when the mutex is unlocked. It does not track transitive lock chains across multiple nested mutexes held simultaneously by the same thread.