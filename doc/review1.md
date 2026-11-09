# Review: icekarma/mthread

Static review of [icekarma/mthread](https://github.com/icekarma/mthread/) at commit `92a58ce`. I read the full source, focusing on `src/mthread/mthread.asm`, and checked the DOS-internals claims against Microsoft's published [MS-DOS 4.0 kernel source](https://github.com/microsoft/MS-DOS/tree/main/v4.0/src). I did **not** build or run it (no MASM 6.11 / MSC 7.00 available), so everything below comes from reading the code. Line numbers refer to `mthread.asm` unless noted.

## Summary

The scheduler core and synchronization primitives are solid work. The concurrent-DOS layer (the "stack tier" design) rests on an incorrect model of DOS internals and can't be made reliable as designed. Separately, there is a register-clobbering bug that corrupts the return values of most DOS calls once threading is initialized. Several specific technical claims in the README don't match the code or DOS itself.

## What's good

- **Timer handling is correct.** Hooking INT 08h directly rather than 1Ch, sending a specific EOI (`OUT 20h,60h`) on sub-ticks, and chaining to the BIOS every 2^n ticks keeps the time-of-day clock accurate. The PIT divisor math is right.
- **The scheduler is clean.** Register context lives in the TCB, the scheduler runs on a dedicated stack, round-robin within a priority tier comes from scanning at `current_tid+1` with strict `>`, and the CPU halts in `HLT` when nothing is ready.
- **The sync primitives look sound.** Bitmask wait queues, direct ownership/token handoff, and atomic block-and-switch under `CLI`. `cond_wait` atomically releases the mutex and blocks. I found no lost-wakeup paths.
- **FPU switching is reasonable.** Lazy, owner-tracked save/restore, and a standard x87 presence probe.
- **The code is tidy.** Style is consistent, comments are good, and memory-model differences are handled cleanly through macros in `mthread.inc`.

## Serious correctness issues

### 1. DL is clobbered on return from nearly every DOS call (lines ~960–1028)

After the real INT 21h returns, the post-call path saves FLAGS/DS/AX/BX/CX but **not DX**, then does `mov dl, STATE_WAIT_DISK` (or `STATE_WAIT_AUX` / `STATE_WAIT_IO`) to pass the wait state to `restore_prio_and_wake`. Every call that goes through the tier logic therefore returns with DL = 04h, 05h or 07h. This includes the main thread, since TID 0 is not `NO_THREAD`.

Effects include:

- AH=2Ah (get date) and AH=2Ch (get time) return wrong values.
- AH=42h (LSEEK) returns DX:AX with DX = 0004h even for small files, so positions are off by 256 KB.
- AH=44h/00h (IOCTL get device info) and AH=36h (free disk space) are also corrupted.
- AH=06h callers that reuse `DL=FFh` (input mode) get output mode with DL=07h (BEL) on the next call.

**Fix:** `push dx` / `pop dx` around the post-call bookkeeping.

### 2. DX and SI are corrupted on DOS stack contention (lines 862–865, 937–939, 1072)

On a collision, the tier code sets `DL = STATE_WAIT_*` before calling `boost_and_block_on_dos`, and that routine does `mov si, current_tcb`. Neither register is restored before the retry goes to DOS. For AH=3Ch/3Dh/3Fh/40h, DS:DX is the filename or buffer, so the retried call uses the wrong address. AH=47h and AH=6Ch use DS:SI and are hit the same way.

`cdemo` exercises exactly this path. `printf` goes through AH=40h, which DOS runs on **DskStack** (the comment at `cdemo.c:245` says IOStack), while the high-priority logger thread does `fopen`/`fprintf` on the same stack.

Also, `soft_context_switch` resumes the blocked thread with the *caller's* DS (via `scratch_ds`), but the retry loop at `tier_disk_retry` reads `disk_stack_owner` and `current_tid` relative to DS. In far-data models where the caller's DS ≠ DGROUP, those reads hit the wrong segment.

**Fix:** preserve DX and SI across the block, and reload DS = DGROUP after resuming.

### 3. The "disjoint stack tier" model of DOS is incorrect

The README says threads on different DOS internal stacks only collide on the 24-byte swap-always header (SDA+00h..17h). MS-DOS's own source says otherwise:

- **`user_SP` / `user_SS` are global**, not part of the 24-byte header. They are declared in [`INC/MS_DATA.ASM`](https://github.com/microsoft/MS-DOS/blob/main/v4.0/src/INC/MS_DATA.ASM), well into the swap-in-DOS region. On exit, DOS reloads `SS:SP` from them ([`DOS/DISP.ASM`](https://github.com/microsoft/MS-DOS/blob/main/v4.0/src/DOS/DISP.ASM), `LEAVEDOS`).
  - DOS keeps only one level of save (`NSP`/`NSS`), so interleaving only works if calls finish in LIFO order.
  - With preemption that order isn't guaranteed. If thread A is preempted inside a disk-tier call, thread B enters a console-tier call, and A is rescheduled and finishes first, A returns onto B's stack.
- **Much more per-call state is shared across all three stacks:**
  - the `DEVCALL` device request packet, `THISSFT`, `THISCDS`, `CURBUF`, `SaveDS`/`SaveBX`, `user_SP`/`user_SS` and the file-position scratch variables;
  - flags that every DSKROUT entry resets: `IDLEINT`, `FAILERR`, `EXTOPEN_ON`, `DOS34_FLAG`, `CONSWAP`.
- **The "16 bytes of console state" at SDA+18h..27h isn't console state.** It is `USER_IN_AX`, `PROC_ID`, `USER_ID`, `FirstArena`, `BestArena`, `LastArena`, `EndMem` and `LASTENT` ([`INC/CONST2.ASM`](https://github.com/microsoft/MS-DOS/blob/main/v4.0/src/INC/CONST2.ASM)).
- The README's statement that the 24-byte header "holds `user_SP`, `user_SS`" is also incorrect. That range is ErrorMode, InDOS, the extended-error fields, DTA, current PSP, `ConC_spsave`, the exit code, the current drive and the break flags.

The established safe approaches are:

1. Swap the **entire** SDA when switching between threads that are in DOS (AX=5D06h returns the swap-in-DOS size in CX and the swap-always size in DX).
2. Serialize all INT 21h calls behind a single lock.

With the existing priority-inheritance machinery, option 2 is small and makes the IOStack calibration and `io_swap_buf` unnecessary.

### 4. The INT 2Ah AH=82h handling can disable preemption permanently (lines 1186–1208)

DOS issues INT 2Ah AH=82h ("end all critical sections") at the start of **every** disk-class call (`DISP.ASM`, DSKROUT). `isr_int2a` decrements `bios_lock` on 82h whenever it is non-zero, so any DOS call made while a wrapper holds `bios_lock` drives it below its proper count. The clearest trigger is `isr_int2e`, because COMMAND.COM makes disk calls:

1. The `isr_int2e` wrapper sets `bios_lock` to 1.
2. A DOS call issues INT 2Ah AH=82h, which brings it to 0.
3. The wrapper exits and decrements again, so it wraps to 255.

From then on, every timer tick takes `timer_locked_tick` and no thread switch ever happens again.

### 5. Other robustness gaps

- **Third-party ISRs can be preempted mid-handler.** Handlers that `STI` before finishing (packet drivers, serial, sound) and don't call `mt_enter_isr` can be switched away from while their 8259 in-service bit is still set. Real-mode preemptive kernels usually read the 8259 ISR register (OCW3) or check InDOS/ROM CS before switching.
- **The library calls `malloc()`/`free()` internally** (`thread_create` line ~1697, `reap_zombie_stacks` line ~1631) without any lock. Users can't serialize heap access even if they follow the README's guidance for the non-reentrant C runtime.
- **IOStack offset calibration is heuristic.** It uses a hard-coded `+0458h` (line 1367), overwritten with `SP − 352` at whatever depth INT 16h happens to be called during an AH=0Bh probe (lines 760–762). The stack segment isn't checked against the SDA segment, and the depth varies with DOS version, CON driver, ANSI.SYS and similar.
- **SDA layout dependency.** The code assumes MS-DOS SDA internals, while DR-DOS, FreeDOS and others differ.

## Smaller issues

- `isr_int24` (lines 1137–1148) stores DI in `crit_err_code`, not AH, despite the comment at line 49 and the README.
- `restore_prio_and_wake` resets the current thread to its base priority after any DOS call, which discards an active mutex priority-inheritance boost.
- The SDA header is only swapped for threads with `TCB_FLG_IN_DOS` set. While one thread is inside AH=4Bh (child program running), other threads run with the **child's** PSP and DTA live, so their handle I/O goes through the child's JFT.
- If a thread hands back Fail (AL=03h) where Fail isn't allowed, DOS converts it to Abort. That terminates through DOS's internal path, bypassing both `atexit` and the INT 21h termination trap, so the vectors stay hooked into freed memory.
- `break_flag` is never cleared, so after the first Ctrl+Break, `isr_int09` signals the keyboard event on every IRQ1, including key releases.
- `in_scheduler` (line 98) is declared but never used.
- Waits have no timeouts. `cond_wait` doesn't verify that the caller owns the mutex. `thread_join` can join the wrong thread if the TID slot was reused.
- `cdemo` breaks the README's own rule about the C runtime: unsynchronized `fopen`/`fprintf`/`fclose` in the logger thread alongside `printf` in the main thread.

## README and portability accuracy

- **Stale names:** the README refers to `thread_init()`, `thread_abort()` and `thread_get_critical_error()`, but these were renamed to `threading_*` in `a8d457b`.
- **Missing file:** the README lists `mthrwrap.cpp` in `mthread.lib`, but the file is `mthrmgr.cpp`.
- **Wrong default:** the README says the default is `FPU=NONE`, but the `Makefile` sets `FPU = OPTIN`.
- **"User thread stacks never have to absorb the stack footprint of the ROM BIOS timer ISR, chained INT 1Ch TSRs…"** is only true for ticks that switch threads.
  - `timer_locked_tick` chains to the BIOS INT 08h (and therefore INT 1Ch) on the **current** stack.
  - Keyboard and mouse handlers also run on whatever thread stack was interrupted.
- **Borland C++ 3.1 build:** `video.c` now uses `__segment` / `__based(vram_seg)` (commit `da354bd`), which Borland C doesn't support. `mthread.asm` also references `STKHQQ`, which is MSC-specific, so I'd expect an unresolved external with the Borland runtime. I haven't verified this.
- **OpenWatcom build:** `mthread.asm` declares `EXTERNDEF C malloc/free/atexit` (`_malloc` and so on), but OpenWatcom's default 16-bit libraries use register-convention names (`malloc_`), and `STKHQQ` isn't defined there either. Again, I'd expect a link failure but haven't verified it.

## Suggested priorities

1. **Small fixes:** save DX on the INT 21h post-call path (#1), and save DX/SI and reload DS around the contention wait (#2).
2. **INT 2Ah:** stop decrementing `bios_lock` on INT 2Ah AH=82h, or track DOS critical sections with a separate counter (#4).
3. **DOS layer:** replace the tier/IOStack-swap design with a single priority-inheriting lock around INT 21h, or with full SDA swapping (#3).
4. **README:** correct the SDA description and verify the Borland/Watcom build claims or remove them.
