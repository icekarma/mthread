# MTHREAD: Preemptive Multithreading for MS-DOS (8086 / MASM 6.11)

`MTHREAD` is a compact, priority-based preemptive multithreading kernel, synchronization suite, interrupt-driven input library, and C/C++ framework for MS-DOS. Written in 8086/80186 assembly language for Microsoft Macro Assembler (MASM) 6.11 and Microsoft C/C++ 7.00, it supports all six 16-bit x86 memory models (`TINY`, `SMALL`, `MEDIUM`, `COMPACT`, `LARGE`, and `HUGE`) as well as optional per-thread 8087/80287/80387+ hardware and software-emulated floating-point context switching.

---

## Project Layout & Build System

The NMAKE `Makefile` places intermediate object files, assembler listings, linker maps, and the active build-configuration stamp in `obj\`, static libraries in `lib\`, and executable demos (`.exe` or `.com` in `TINY` model) in `bin\`.

### Static Libraries (`lib\`)
- **`mthread.lib` (`mthread.asm`, `mthrmgr.cpp`):** Core preemptive scheduler, `INT 08h` timer and `INT 21h` lazy full-SDA swapper & disk gatekeeper, optional 94-byte x87/emulator FPU context switcher, BIOS reentrancy wrappers, termination/break/critical-error traps, synchronization primitives, and the `MThreadManager` singleton implementation.
- **`video.lib` (`video.c`, `videohlp.asm`):** Direct `B800h` color text-mode video output (`video_clear`, `video_putc`, `video_puts`, `video_putu8`, `video_putu16`, `video_putu32`, `video_puthex8`, `video_ruler`) and BIOS cursor positioning (`video_gotoxy`), with automatic `OFFSET_DISPLAY` centering on $>80\times 25$ text modes (such as $80\times 43$, $80\times 50$, and $132\times 43$).
- **`input.lib` (`kbd.asm`, `mouse.asm`):** Interrupt-driven keyboard (`INT 09h`) and mouse (`INT 33h`) handlers that wake threads via `event_set_isr()` with zero polling overhead.

### Demo Executables (`bin\`)
- **`cdemo` / `cppdemo`:** Preemptive compute workers, low-priority background spinner on a caller-supplied static stack, per-thread `STKHQQ` stack probe verification, concurrent `INT 21h` file logging (`MTTEST.LOG`), and console heartbeat output.
- **`cfpu` / `cppfpu`:** Concurrent floating-point sine-wave animation across three threads using per-thread 94-byte x87/emulator context switching (`thread_enable_fpu` / `MThread::EnableFpu`).
- **`cinput` / `cppinput`:** Zero-polling high-priority input worker woken via `event_set_isr()` from `INT 09h` keyboard and `INT 33h` mouse interrupts, featuring live BIOS modifier inspection, a 32-character typeahead buffer, and clickable mouse UI controls alongside background compute threads.
- **`csync` / `cppsync`:** Showcase of manual-reset and auto-reset Win32 events, recursive mutexes with Priority Inheritance (`LOW` boosted to `HIGH` to defeat a `NORMAL`-priority CPU hog), bounded producer-consumer ring buffers using counting semaphores, and batch notifications via condition variables.

### Building with NMAKE
`Makefile` uses `chkcfg.bat` and `clean.bat` to record an 8.3 configuration stamp (`obj\<STAMP>.cfg`) and automatically cleans stale artifacts whenever `MODEL`, `CPU`, `FPU`, or `COMPILER` changes between builds:

~~~dos
nmake                                  (Defaults: MSC, SMALL, 8086, FPU=NONE)
nmake MODEL=TINY                       (MSC 7.00, tiny model .COM files in bin\)
nmake MODEL=HUGE CPU=186               (MSC 7.00, huge model, 80186+)
nmake MODEL=LARGE CPU=186 FPU=ALL      (MSC 7.00, large model, 186+, FPU all threads)
nmake FPU=OPTIN                        (MSC 7.00, per-thread FPU opt-in)
nmake clean                            (Removes obj\, lib\, bin\, and log files)
~~~

- **`MODEL`:** `TINY`, `SMALL` (default), `MEDIUM`, `COMPACT`, `LARGE`, or `HUGE`.
- **`CPU`:** `8086`/`8088`/`86`/`88` (default) or `80186`/`80188`/`186`/`188`.
- **`FPU`:** `NONE` (default), `OPTIN`, or `ALL`.

---

## How the Implementation Works

### 1. 64-Byte TCB-Resident Context & Zero-Residue User Stacks
Unlike traditional real-mode context switchers that push all general-purpose registers onto the interrupted thread's stack, `MTHREAD` stores all 14 architectural 8086 registers (`AX` through `ES`, plus `SS`, `SP`, `IP`, `CS`, and `FLAGS`) inside a 64-byte **Thread Control Block (`TCB`)**:
- When `INT 08h` or a voluntary context switch (`soft_context_switch`) fires, the entry stub immediately pops the 3-word interrupt frame (`IP`, `CS`, `FLAGS`) off the interrupted thread's stack directly into its `TCB`, records the clean `SS:SP`, and switches `SS:SP` onto a dedicated internal scheduler stack (`isr_stack`, 512 bytes by default or 768 bytes when built with `USE_FPU`).
- **Fast Indexing & Pointer Caching:** The active TCB pointer is cached in `current_tcb` for single-instruction lookup on interrupt entry, and indexing `tcb_table[tid]` requires only a 6-bit left shift (`SHL BX, 6` on 80186+ or `MOV CL, 6 / SHL BX, CL` on strict 8086) with no `MUL` instruction.

### 2. Hardware Timer (`INT 08h`), PIC Verification, PIT Scaling & Instant `HLT` Wakeup
`MTHREAD` hooks hardware `INT 08h` (IRQ0) directly rather than the BIOS user tick `INT 1Ch`:
- **Avoiding the `INT 1Ch` Double-EOI Hazard:** The IBM PC/XT/AT ROM BIOS `INT 08h` handler issues a Non-Specific EOI (`OUT 20h, 20h`) *after* `INT 1Ch` returns. Switching stacks inside `INT 1Ch` either starves the 8259 PIC of an EOI or causes a delayed Non-Specific EOI to clear an unrelated IRQ when the original thread eventually resumes.
- **8259 PIC In-Service Verification:** On entry to `isr_timer`, if `cs:[bios_lock]` is zero, `MTHREAD` reads the Master 8259 PIC In-Service Register (`OCW3 = 0Bh` at port `20h`). If any bit other than Bit 0 (`IRQ0`) is set (which also covers Slave PIC `IRQ8..15` cascaded through Master `IRQ2` on AT+ systems without touching PC/XT NMI port `0A0h`), a third-party hardware ISR that re-enabled interrupts (`STI`) is still active, so `isr_timer` defers preemption via `timer_locked_tick`.
- **Power-of-2 PIT Scaling:** `threading_init(tick_rate, main_prio)` reprograms Channel 0 of the 8253/8254 PIT to run at $1\times$ (`MT_TICK_18HZ`, 18.2 Hz), $2\times$ (`MT_TICK_36HZ`, 36.4 Hz), $4\times$ (`MT_TICK_73HZ`, 72.8 Hz), or $8\times$ (`MT_TICK_146HZ`, 145.6 Hz) the stock tick rate. On sub-ticks, `isr_timer` sends a **Specific EOI for IRQ0 (`OUT 20h, 60h`)** to the 8259 PIC; on every $2^{\text{tick\_rate}}$-th tick, it chains to the original ROM BIOS `INT 08h` handler on `isr_stack` (guarded by `bios_lock`) so the BIOS time-of-day clock and floppy motor shutoff timer remain accurate.
- **Built-in `HLT` Idle Loop with Instant IRQ Dispatch:** When no thread is `STATE_READY`, the scheduler sets `current_tid = 0xFF` (`NO_THREAD`) and executes `STI / HLT` on `isr_stack`. As soon as *any* hardware interrupt (such as `INT 09h` keyboard or a mouse IRQ calling `event_set_isr()`) wakes the CPU from `HLT` and executes `IRET`, `idle_hlt_spin` immediately executes `CLI / JMP schedule_next_thread`, dispatching the newly woken thread within microseconds without waiting for the next `INT 08h` timer tick.

### 3. Priority-Based Round-Robin Scheduling & Dual Priority Inheritance
Each thread is assigned a priority from `0` (`MT_PRIO_IDLE`) to `7` (`MT_PRIO_HIGHEST`). During every scheduling pass:
1. The dispatcher begins scanning `tcb_table` at `(current_tid + 1) & 7`.
2. It selects the first `STATE_READY` thread whose effective priority (`tcb_prio`) is **strictly greater (`>`)** than the best priority seen so far during the scan.
3. Using strict `>` combined with a rotating start index guarantees fair round-robin time-slicing among threads of equal priority while always preempting in favor of higher-priority runnable threads.
4. Each `TCB` tracks both its base priority (`tcb_base_prio`) and highest mutex-inherited priority (`tcb_mtx_prio`) so temporary priority boosts across DOS disk calls never discard an active mutex priority-inheritance boost.

### 4. MS-DOS (`INT 21h`) Reentrancy: Lazy Full-SDA Swapping & Disk Serialization
MS-DOS is non-reentrant because `INT 21h` stores caller stack pointers (`user_SS:user_SP`), internal stacks (`IOStack`, `DiskStack`, `AuxStack`), and per-call scratch state inside the **Swappable Data Area (SDA)**, while file operations also mutate global kernel structures outside the SDA (System File Tables, FAT buffers, and the `BUFFERS=` LRU chain). `MTHREAD` makes standard C runtime and DOS calls work seamlessly across threads through a three-part design:
- **Dynamic Per-Thread SDA Buffers (`AX=5D06h`):** During `threading_init()`, `MTHREAD` queries `INT 21h, AX=5D06h` to obtain the exact `DS:SI` base address and `CX` byte size of the DOS Swappable Data Area, making the kernel independent of hardcoded DOS version offsets. Each active thread owns a heap-allocated `sda_buf` initialized from the clean idle SDA.
- **Tier 0 Stackless Fast-Path (`AH = 33h, 50h, 51h, 62h, 64h`):** Calls that DOS executes directly on the caller's stack without touching `InDOS` pass straight through to `old_int21` with zero overhead.
- **Disk Call Serialization with Priority Inheritance (`AH >= 0Dh` except `59h`):** Because file and directory operations mutate global non-SDA structures (SFTs and disk buffers), `isr_int21` serializes disk calls through `disk_stack_owner`. If Thread B enters a disk call while Thread A is mid-disk-call, Thread B blocks in `STATE_WAIT_DISK` and boosts Thread A's priority (`boost_and_block_on_dos`) until Thread A exits `INT 21h`.
- **Lazy Full-SDA Swapping (`swap_full_sda`):** Compute threads pay zero SDA copy cost on timer ticks. `MTHREAD` tracks which thread's DOS state currently resides in the live SDA via `sda_owner_tid`. A full word-aligned SDA swap occurs only when:
  1. A thread cleared to enter `INT 21h` differs from `sda_owner_tid`, or
  2. `schedule_next_thread` switches back to a thread that was preempted *while inside* `INT 21h` (`TCB_FLG_IN_DOS` set) and another thread swapped the SDA in the interim.
  Because the entire SDA is exchanged, one thread blocking inside a console input call (such as `AH=01h`/`0Ah` or C `scanf`/`getch`) never blocks other threads from performing console output or disk I/O.

### 5. ROM BIOS & System Interrupt Reentrancy Guards (`bios_lock`)
- Wrappers around `INT 10h` (Video), `INT 13h` (Disk), `INT 15h` (System Services), `INT 16h` (Keyboard), `INT 17h` (Printer), `INT 25h`/`26h` (Absolute Disk Read/Write, preserving the left-on-stack FLAGS convention), `INT 29h` (Fast Console Output), `INT 2Eh` (`COMMAND.COM` Execute), `INT 2Fh` (Multiplex), `INT 33h` (Mouse Driver, if present), and `INT 67h` (LIM EMS Driver, if present) increment `cs:[bios_lock]` on entry, merge the BIOS output status flags (`0CD5h`: `CF, PF, AF, ZF, SF, DF, OF`) into the caller's `IRET` frame while preserving the caller's `IF` and `TF`, decrement `cs:[bios_lock]`, and return via `IRET`.
- External hardware ISRs and driver callbacks (`kbd.asm` and `mouse.asm`) and internal CRT `malloc()`/`free()` sections in `thread_create()` and `reap_zombie_stacks` also guard preemption via `bios_lock` (`mt_enter_isr()` / `mt_leave_isr()`).
- If `INT 08h` fires while `bios_lock > 0`, `isr_timer` takes the non-preemptive `timer_locked_tick` fast path on the current stack—advancing PIT sub-ticks, chaining to BIOS `INT 08h` when due, and decrementing `sleep_ticks` without switching stacks or threads.

### 6. Multi-Layer Termination, Break & Critical Error Defense
Because MS-DOS only restores `INT 22h`, `INT 23h`, and `INT 24h` when a program exits, terminating without unhooking `INT 08h` or `INT 21h` would leave timer interrupts firing into freed memory. `MTHREAD` prevents this with four layers of defense:
1. **Idempotent `unhook_hardware` & `threading_abort()`:** Restores the 18.2 Hz PIT divisor, resets `STKHQQ` to the main thread's stack limit, clears `ErrorMode`, and unhooks all hooked system/BIOS interrupt vectors (plus `INT 34h..3Eh` when FPU emulation hooks are active) via `old_int21` (`AH=25h`). `threading_init()` automatically registers `threading_abort()` with the C runtime's `atexit()`.
2. **DOS Termination Traps:** `isr_int21` intercepts `AH=00h`, `AH=31h` (TSR), and `AH=4Ch`, and dedicated traps on `INT 20h` (which also catches CP/M-style `CALL 0005h` terminations) and `INT 27h` invoke `unhook_hardware` before chaining to DOS.
3. **Cooperative & Emergency `Ctrl+C` / `Ctrl+Break` (`INT 1Bh` & `INT 23h`):** `isr_int1b` sets `break_flag = 1`, increments `break_count`, and clears the BIOS break bit at `0040:0071h` to suppress `^C` screen corruption. `isr_int23` allows the first two `Ctrl+C` presses to be handled cooperatively via `threading_is_break_requested()`; on the 3rd press (`break_count >= 3`), it calls `unhook_hardware` and returns with `STC / RETF` to force DOS to abort the program cleanly.
4. **Critical Error Handler (`INT 24h`):** `isr_int24` suppresses the non-reentrant DOS `"Abort, Retry, Ignore, Fail?"` prompt, records the error code (high byte = `AH` flags, low byte = `DI` code) for `threading_get_critical_error()`, and returns `AL = 03h` (Fail) if allowed by DOS (`AH` bit 3), `AL = 00h` (Ignore) if Fail is disallowed but Ignore is allowed (`AH` bit 5), or calls `unhook_hardware` and returns `AL = 02h` (Abort) if neither is allowed.

### 7. C Runtime Stack Probes (`STKHQQ`) & Lazy Heap Reaping
- **`STKHQQ` Synchronization:** Microsoft C 7.00 inserts calls to `__chkstk` at function prologues, comparing `SP` against `DGROUP:STKHQQ`. Every `TCB` records its stack's lower guard address (`stk_limit`) and updates `STKHQQ` on every context switch, providing per-thread stack overflow checking.
- **Lazy Zombie Reaping:** Because an exiting thread cannot `free()` its own stack while running on it, `thread_exit()` switches onto `isr_stack` and marks the slot `STATE_ZOMBIE`. `reap_zombie_stacks` frees the thread's dynamic `sda_buf` and auto-allocated stack safely under `bios_lock` during the next `thread_create()`, `thread_join()`, `thread_join_all()`, or `threading_shutdown()`. During `threading_shutdown()`, Thread 0's original SDA is restored to live DOS before unhooking and freeing all remaining buffers.

### 8. Memory Model Portability (`TINY` through `HUGE`)
`mthread.inc` inspects MASM's built-in `@Model`, `@CodeSize`, `@DataSize`, and `@Cpu` symbols to adapt code and data pointers automatically:
- **`TINY` Model (`.COM` Executables, `@Model == 1`):** Because `.COM` binaries cannot contain segment relocation fixups, `LOAD_DS_DGROUP` loads `DS` from `CS` (`mov ax, cs / mov ds, ax`) instead of `@data`, and the `Makefile` disables CodeView `/Zi` records and outputs `.com` targets.
- **`HUGE` Model (`@Model == 6`, `@DataSize == 2`):** `LOAD_DATA_PTR`, `thread_create`, `kbd_init`, and `mouse_init` invoke `NORMALIZE_FAR_PTR` (using unrolled 4-bit shifts on 8086) so that thread stacks and multi-byte synchronization/event structures never wrap a 64 KB segment boundary, while preserving the raw `malloc()` pointer in `stk_alloc_seg:stk_alloc_off` for `free()`.

### 9. Synchronization Primitives, Thread Joining & C++ Wrappers
All synchronization primitives use 1-byte bitmask wait queues (`1 << tid`), priority-ordered wakeups (`wake_highest_waiter`), direct ownership/token handoff, and immediate preemption (`preempt_if_higher`) when a woken thread outranks the caller:
- **Events (`mt_event_t` / `MEvent`):** Win32-style manual-reset and auto-reset events (`event_init`, `event_set`, `event_set_isr`, `event_reset`, `event_pulse`, `event_wait`).
- **Mutexes (`mt_mutex_t` / `MMutex` / `MLockGuard`):** Recursive mutexes with Priority Inheritance (`mutex_init`, `mutex_lock`, `mutex_trylock`, `mutex_unlock`) and scoped RAII lock guards.
- **Semaphores (`mt_sem_t` / `MSemaphore`):** 16-bit counting semaphores (`sem_init`, `sem_wait`, `sem_trywait`, `sem_post`).
- **Condition Variables (`mt_cond_t` / `MCondVar`):** Verified-owner atomic recursive mutex release and re-acquisition (`cond_init`, `cond_wait`, `cond_signal`, `cond_broadcast`).
- **Thread Joining & Atomics:** `thread_join(tid)` and `thread_join_all()` block until target thread(s) exit and immediately reap zombie stacks; `cmp_xchg(ptr, old_val, new_val)` provides an interrupt-atomic compare-and-exchange primitive.

### 10. Interrupt-Driven Input (`input.lib`)
- **Keyboard (`kbd.asm` / `MKeyboard`):** Hooks `INT 09h` (bracketed by `mt_enter_isr` / `mt_leave_isr`), chains to the ROM BIOS to translate scan codes, and inspects the BIOS keyboard ring-buffer pointers at `0040:001Ah` (head) and `0040:001Ch` (tail) plus rising-edge transitions of `threading_is_break_requested()`. When a keystroke or break is present, it wakes the registered `mt_event_t` via `event_set_isr()`.
- **Mouse (`mouse.asm` / `MMouse`):** Verifies that `INT 33h` is installed and not a bare `IRET` (`0CFh`), resets the driver, and registers a far callback via `INT 33h, AX=000Ch`. At IRQ time, `mouse_callback` updates a live state snapshot, enqueues events into a 16-slot ring buffer that **coalesces consecutive pure move events** (`MOUSE_EVT_MOVE`) so fast mouse motion never drops button clicks, and signals the registered `mt_event_t` via `event_set_isr()`.

### 11. Floating-Point (`x87` & Software Emulator) Context Switching (`FPU=OPTIN | ALL`)
When built with `FPU=OPTIN` (`/DUSE_FPU`) or `FPU=ALL` (`/DUSE_FPU /DFPU_DEFAULT_ALL`), `MTHREAD` provides per-thread 94-byte floating-point context management supporting both physical 8087/80287/80387+ coprocessors and C runtime software emulators (`INT 34h..3Eh`):
- **Out-of-Band 94-Byte Context Table (`fpu_table`):** To preserve the 64-byte `TCB` size and fast 6-bit shift indexing, per-thread 94-byte `FSAVE`/`FRSTOR` frames are stored in a separate `fpu_table` in `DGROUP` indexed via `fpu_off_lut`.
- **Lazy Owner-Tracked Switching (`fpu_owner` & `TCB_FLG_FPU`):** `MTHREAD` tracks which thread's state currently resides in the FPU via `fpu_owner`. Switching to an integer-only thread (where `TCB_FLG_FPU` is clear) or switching back to the thread that already owns the FPU executes zero `FSAVE`/`FRSTOR` instructions. A 94-byte `fpu_save_bx` + `fpu_restore_bx` exchange only occurs when an FPU-enabled thread preempts a *different* FPU-enabled thread.
- **Dual Hardware & Software Emulator Support:** During `threading_init()`, `MTHREAD` inspects `INT 39h`. If a C runtime 8087 emulator is installed (`FPU_MODE_EMU`), `MTHREAD` hooks `INT 34h..3Eh` to prevent `isr_timer` from preempting an emulated floating-point opcode mid-instruction and uses `INT 39h` (`CD 39 37` / `CD 39 27`) to swap the emulator's 94-byte state. Otherwise, it probes for a physical x87 via `FNINIT` / `FNSTSW` / `FNSTCW` (`FPU_MODE_HW`) and uses native `FSAVE` / `FRSTOR`.
- **Runtime Opt-In / Opt-Out API:** Regardless of whether the build default is `FPU=OPTIN` or `FPU=ALL`, any thread can dynamically enable or disable FPU tracking at runtime via `thread_enable_fpu(1)` / `thread_enable_fpu(0)` and query its state via `thread_is_fpu_enabled()` (or `MThread::EnableFpu()`, `MThread::DisableFpu()`, and `MThread::IsFpuEnabled()` in C++).

---

## MTHREAD C API Reference

Include `<mthread/mthread.h>` for the core kernel, synchronization, atomic, and ISR interfaces, `<mthread/input.h>` for interrupt-driven keyboard and mouse support, and `<mthread/video.h>` for direct `B800h` text-mode output. All public functions use the `MT_API` (`__cdecl`) calling convention.

### Types & Constants (`<mthread/_defs.h>`, `<mthread/thread.h>`)
- **`tid_t` (`unsigned char`):** Thread identifier (`0..7`), `MT_NO_THREAD` (`0xFF`), or an error code:
  - `MT_ERR_NONE` (`0x00`), `MT_ERR_NO_SLOTS` (`0xFE`), `MT_ERR_NO_MEMORY` (`0xFD`), `MT_ERR_INVALID` (`0xFC`).
- **`thread_func_t`:** Thread entry prototype: `typedef void (MT_API *thread_func_t)(void *arg);`
- **Priorities:** `MT_PRIO_IDLE` (`0`), `MT_PRIO_LOW` (`1`), `MT_PRIO_LOW_PLUS` / `MT_PRIO_NORMAL_MINUS` (`2`), `MT_PRIO_NORMAL` (`3`), `MT_PRIO_NORMAL_PLUS` / `MT_PRIO_HIGH_MINUS` (`4`), `MT_PRIO_HIGH` (`5`), `MT_PRIO_HIGH_PLUS` / `MT_PRIO_HIGHEST_MINUS` (`6`), `MT_PRIO_HIGHEST` (`7`).
- **PIT Tick Rates:** `MT_TICK_18HZ` (`0`, 18.2 Hz), `MT_TICK_36HZ` (`1`, 36.4 Hz), `MT_TICK_73HZ` (`2`, 72.8 Hz), `MT_TICK_146HZ` (`3`, 145.6 Hz).

### Kernel Lifecycle (`<mthread/thread.h>`)
~~~c
void         MT_API threading_init( unsigned char tick_rate, unsigned char main_prio );
void         MT_API threading_shutdown( void );
void         MT_API threading_abort( void );
int          MT_API threading_is_break_requested( void );
unsigned int MT_API threading_get_critical_error( void );
~~~
- **`threading_init`:** Discovers the DOS SDA, allocates Thread 0's SDA swap buffer, initializes `tcb_table`, registers `threading_abort` with `atexit()`, hooks system/BIOS vectors, and reprograms PIT Channel 0 to `tick_rate`.
- **`threading_shutdown`:** Restores Thread 0's SDA to live DOS, unhooks the PIT and all interrupt vectors, and frees all SDA buffers and auto-allocated thread stacks.
- **`threading_abort`:** Idempotent emergency unhook routine (safe to call from `atexit()` or signal handlers).
- **`threading_is_break_requested`:** Returns `1` if `Ctrl+C` or `Ctrl+Break` was pressed, `0` otherwise.
- **`threading_get_critical_error`:** Atomically reads and clears the last `INT 24h` critical error word (high byte = `AH` flags, low byte = `DI` error code).

### Thread Management (`<mthread/thread.h>`)
~~~c
tid_t         MT_API thread_create( thread_func_t func, void* arg, void* stack_ptr, unsigned int stack_size, unsigned char priority );
void          MT_API thread_yield( void );
void          MT_API thread_sleep( unsigned int ticks );
void          MT_API thread_exit( void );
int           MT_API thread_join( tid_t tid );
void          MT_API thread_join_all( void );
tid_t         MT_API thread_self( void );
unsigned char MT_API thread_get_priority( tid_t tid );
int           MT_API thread_set_priority( tid_t tid, unsigned char new_prio );
int           MT_API thread_enable_fpu( int const enable );
int           MT_API thread_is_fpu_enabled( void );
~~~
- **`thread_create`:** Allocates an SDA swap buffer and spawns a new thread (`1..7`). Pass `stack_ptr = NULL` to auto-allocate a stack via `malloc()` and `stack_size = 0` for the 1024-byte default (minimum 128 bytes). Returns the new `tid_t` or `MT_ERR_NO_SLOTS` / `MT_ERR_NO_MEMORY`.
- **`thread_yield` / `thread_sleep`:** Voluntarily yields the CPU or sleeps for `ticks` timer interrupts (`ticks == 0` yields).
- **`thread_exit`:** Terminates the calling thread (also invoked automatically if `func(arg)` returns).
- **`thread_join` / `thread_join_all`:** Blocks until `tid` (or all other active threads) exits and reaps zombie stacks/SDA buffers.
- **`thread_enable_fpu` / `thread_is_fpu_enabled`:** Enables (`enable != 0`) or disables (`enable == 0`) 94-byte x87/emulator context saving for the calling thread when built with `FPU=OPTIN` or `FPU=ALL`.

### Synchronization Primitives (`<mthread/synchron.h>`)
~~~c
/* Win32-Style Events (manual_reset: 0 = auto-reset, 1 = manual-reset) */
void MT_API event_init( mt_event_t* ev, unsigned char manual_reset, unsigned char initial_state );
void MT_API event_set( mt_event_t* ev );
void MT_API event_reset( mt_event_t* ev );
void MT_API event_pulse( mt_event_t* ev );
void MT_API event_wait( mt_event_t* ev );

/* Recursive Mutexes with Priority Inheritance */
void MT_API mutex_init( mt_mutex_t* mtx );
void MT_API mutex_lock( mt_mutex_t* mtx );
int  MT_API mutex_trylock( mt_mutex_t* mtx );
void MT_API mutex_unlock( mt_mutex_t* mtx );

/* Counting Semaphores (16-bit count) */
void MT_API sem_init( mt_sem_t* sem, unsigned int initial_count );
void MT_API sem_wait( mt_sem_t* sem );
int  MT_API sem_trywait( mt_sem_t* sem );
void MT_API sem_post( mt_sem_t* sem );

/* Condition Variables (atomically releases & re-acquires recursive mt_mutex_t) */
void MT_API cond_init( mt_cond_t* cv );
void MT_API cond_wait( mt_cond_t* cv, mt_mutex_t* mtx );
void MT_API cond_signal( mt_cond_t* cv );
void MT_API cond_broadcast( mt_cond_t* cv );
~~~

### Atomics & ISR Helpers (`<mthread/atomic.h>`, `<mthread/synchron.h>`)
~~~c
int  MT_API cmp_xchg( int* valueptr, int const old_val, int const new_val );
void MT_API mt_enter_isr( void );
void MT_API mt_leave_isr( void );
void MT_API event_set_isr( mt_event_t* ev );
~~~
- **`cmp_xchg`:** Interrupt-atomic compare-and-exchange; sets `*valueptr = new_val` and returns `1` if `*valueptr == old_val`, else returns `0`.
- **`mt_enter_isr` / `mt_leave_isr`:** Increments/decrements `cs:[bios_lock]` to defer preemption inside custom hardware ISRs or critical sections.
- **`event_set_isr`:** Signals `ev` and wakes waiting thread(s) safely from an ISR or far callback without performing an immediate context switch.

### Input & Video Libraries (`<mthread/input.h>`, `<mthread/video.h>`)
~~~c
/* Keyboard (INT 09h) */
void          MT_API kbd_init( mt_event_t* notify_ev );
void          MT_API kbd_shutdown( void );
int           MT_API kbd_available( void );
unsigned int  MT_API kbd_getkey( void );
unsigned char MT_API kbd_get_modifiers( void );

/* Mouse (INT 33h) */
int  MT_API mouse_init( mt_event_t* notify_ev, unsigned int event_mask );
void MT_API mouse_shutdown( void );
void MT_API mouse_show( void );
void MT_API mouse_hide( void );
void MT_API mouse_set_pos( unsigned char row, unsigned char col );
int  MT_API mouse_available( void );
int  MT_API mouse_get_event( mouse_event_t* out_ev );
void MT_API mouse_get_state( mouse_event_t* out_state );

/* Direct Video (B800h) & BIOS Cursor */
void MT_API video_init( void );
void MT_API video_clear( unsigned char const attr );
void MT_API video_putc( int const row, int const col, unsigned char const attr, unsigned char const c );
void MT_API video_puts( int const row, int const col, unsigned char const attr, const char* s );
void MT_API video_puthex8( int const row, int const col, unsigned char const attr, unsigned char const val );
void MT_API video_putu8( int const row, int const col, unsigned char const attr, unsigned char const val );
void MT_API video_putu16( int const row, int const col, unsigned char const attr, unsigned int const val );
void MT_API video_putu32( int const row, int const col, unsigned char const attr, unsigned long const val );
void MT_API video_ruler( unsigned char horz_bkgr_attr, unsigned char vert_bkgr_attr );
void MT_API video_gotoxy( unsigned char const row, unsigned char const col );
~~~

---

## MTHREAD C++ API Reference

When compiled as C++, `<mthread/mthread.h>` and `<mthread/input.h>` automatically expose RAII wrapper classes for lifecycle management, threads, synchronization primitives, and input devices. All wrapper classes are non-copyable.

### 1. `MThreadManager` (`<mthread/mthrmgr.h>`)
Reference-counted singleton that manages kernel initialization and clean shutdown:
- **`static MThreadManager* GetInstance()` / `unsigned int Release()`:** Acquires or decrements the singleton reference count (`Release()` automatically calls `ShutDown()` when the count reaches `0`).
- **`int Initialize(unsigned char tick_rate = MT_TICK_18HZ, unsigned char main_prio = MT_PRIO_NORMAL)`:** Atomically transitions the kernel to active and calls `threading_init()`.
- **`void ShutDown()`:** Joins all active threads (`thread_join_all()`), calls `threading_shutdown()`, and marks the manager inactive.
- **`static void Abort()` / `static int IsBreakRequested()` / `static unsigned int GetCriticalError()` / `static int IsActive()`:** Static status and emergency shutdown helpers.

### 2. `MThread` (`<mthread/mthrd.h>`)
RAII thread object that automatically joins its underlying kernel thread on destruction if still joinable:
- **Constructors:** `MThread()` (deferred creation) or `MThread(thread_func_t func, void* arg, void* stack_ptr = NULL, unsigned int stack_size = 0, unsigned char priority = MT_PRIO_NORMAL)`.
- **`tid_t Create(...)`:** Spawns the thread under a `mt_enter_isr()` / `mt_leave_isr()` guard so `GetTid()` is guaranteed to be populated before `func(arg)` executes its first instruction.
- **`int Join()` / `tid_t GetTid() const` / `unsigned char GetPriority() const` / `int SetPriority(unsigned char new_prio)`:** Instance control methods.
- **Static Helpers:** `MThread::Yield()`, `MThread::Sleep(ticks)`, `MThread::Exit()`, `MThread::JoinAll()`, `MThread::Self()`, `MThread::EnableFpu()`, `MThread::DisableFpu()`, `MThread::IsFpuEnabled()`.

### 3. Synchronization Classes (`<mthread/mevent.h>`, `<mthread/mmutex.h>`, `<mthread/mlkguard.h>`, `<mthread/msem.h>`, `<mthread/mcondvar.h>`)
- **`MEvent`:** `MEvent(unsigned char manual_reset = 0, unsigned char initial_state = 0)`, `Set()`, `Reset()`, `Pulse()`, `Wait()`, `GetHandle()`.
- **`MMutex` & `MLockGuard`:** `MMutex()` recursive priority-inheriting mutex with `Lock()`, `TryLock()`, `Unlock()`; `MLockGuard(MMutex& mutex)` acquires the lock on construction and releases it on scope exit.
- **`MSemaphore`:** `MSemaphore(unsigned int initial_count = 0)` counting semaphore with `Wait()`, `TryWait()`, `Post()`.
- **`MCondVar`:** `MCondVar()` condition variable with `Wait(MMutex& mutex)`, `Signal()`, `Broadcast()`.

### 4. Input Classes (`<mthread/mkbd.h>`, `<mthread/mmouse.h>`)
- **`MKeyboard`:** `MKeyboard(MEvent& notify_ev)` hooks `INT 09h` on construction and calls `kbd_shutdown()` in `~MKeyboard()`. Static methods: `MKeyboard::IsAvailable()`, `MKeyboard::GetKey()`, `MKeyboard::GetModifiers()`.
- **`MMouse`:** `MMouse(MEvent& notify_ev, unsigned int event_mask = MOUSE_EVT_ALL)` initializes the mouse driver and callback on construction and unhooks/hides it in `~MMouse()`. Methods: `IsInstalled()`, `GetButtonCount()`, `MMouse::Show()`, `MMouse::Hide()`, `MMouse::SetPos(row, col)`, `MMouse::IsAvailable()`, `MMouse::GetEvent(out_ev)`, `MMouse::GetState(out_state)`.

---

## Limitations & Architectural Trade-Offs

1. **Maximum of 8 Concurrent Threads (`MAX_THREADS = 8`):**
   Wait queues (`wait_mask`) and active thread masks are represented as 8-bit bitmasks (`1 << tid`) so scheduler scans and wakeups execute in constant time without pointer manipulation. Supporting more than 8 threads requires widening `wait_mask` to 16 bits (`DW`) or switching to intrusive doubly-linked TCB queues.
2. **C Runtime Library (CRT) Non-Reentrancy:**
   While `MTHREAD` swaps the MS-DOS SDA, serializes disk calls (`INT 21h`), guards ROM BIOS interrupts, and (when built with `FPU=OPTIN` or `FPU=ALL`) saves/restores per-thread x87/emulator floating-point state, standard single-threaded DOS C runtime functions that mutate static internal buffers or global heap structures (`malloc`/`free`, `printf`/`sprintf`/`strtod`, `fopen` sharing the same `FILE` stream, `strtok`, `rand`, `localtime`) are not automatically thread-safe. Concurrent C heap, formatted I/O, or shared-stream calls from worker threads should be serialized with a `mt_mutex_t` / `MMutex` (as demonstrated in `cdemo` and `cppdemo`).
3. **Per-Thread Dynamic SDA Buffer Footprint:**
   To provide complete MS-DOS console reentrancy across DOS versions without fragile stack-offset heuristics, each active thread allocates a full swap-in-DOS buffer (`AX=5D06h`, typically ~1.5 KB) from the C heap. Applications spawning many threads in near-data models (`TINY`, `SMALL`, `MEDIUM`) should ensure sufficient default heap space is available.
4. **Floating-Point Context Cost & `FPU=NONE` Default:**
   To save ~850 bytes of `DGROUP` memory in pure-integer applications, FPU context switching is disabled by default (`FPU=NONE`). When enabled (`FPU=OPTIN` or `FPU=ALL`), lazy `fpu_owner` tracking avoids `FSAVE`/`FRSTOR` overhead whenever only one thread actively uses the FPU; when multiple threads contend for the FPU concurrently, each FPU ownership handoff incurs ~1,600–1,800 clock cycles ($\approx 350\ \mu\text{s}$ on a 4.77 MHz 8088/8087).
5. **Blocking Raw BIOS Calls (`INT 16h, AH=00h`):**
   Because `bios_lock` defers preemption while executing inside a raw BIOS interrupt, calling a blocking BIOS routine like `INT 16h, AH=00h` directly from a thread holds `bios_lock > 0` until a key is pressed. Applications should use `input.lib` (`kbd_init` + `event_wait` + `kbd_getkey`), poll via `_kbhit()` + `thread_sleep()`, or use DOS `INT 21h` console input.
6. **Single-Hop Priority Inheritance:**
   `mt_mutex_t` tracks the highest priority among threads waiting on the mutex (`tcb_mtx_prio`) and restores `tcb_base_prio` when the mutex is unlocked. It does not track transitive lock chains across multiple nested mutexes held simultaneously by the same thread.
