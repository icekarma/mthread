# MTHREAD: Preemptive Multithreading for MS-DOS (8086 / MASM 6.11)

`MTHREAD` is a compact, priority-based preemptive multithreading kernel and synchronization library for MS-DOS, written in 8086/80186 assembly language for Microsoft Macro Assembler (MASM) 6.11 and Microsoft C/C++ 7.00 (with build support for Borland C++ 3.1 and OpenWatcom).

---

## How the Implementation Works

### 1. 64-Byte TCB-Resident Context & Zero-Residue User Stacks
Unlike traditional real-mode switchers that push general-purpose registers onto the interrupted thread's stack, `MTHREAD` stores all 14 architectural 8086 registers (`AX` through `ES`, plus `SS`, `SP`, `IP`, `CS`, and `FLAGS`) inside a 64-byte **Thread Control Block (`TCB`)**:
* When `INT 08h` or a software context switch (`soft_context_switch`) fires, the entry stub immediately pops the 3-word hardware interrupt frame (`IP`, `CS`, `FLAGS`) off the interrupted thread's stack directly into its `TCB`, records the clean `SS:SP`, and switches `SS:SP` onto a dedicated 512-byte internal scheduler stack (`isr_stack`).
* **Benefit:** User thread stacks never have to absorb the stack footprint of the ROM BIOS timer ISR, chained `INT 1Ch` TSRs, or the scheduler itself.
* Because each `TCB` is exactly 64 bytes (`40h`), indexing `tcb_table[tid]` requires only a 6-bit left shift (`SHL BX, 6` on 186+ or `MOV CL, 6 / SHL BX, CL` on strict 8086) with no `MUL` instruction.

### 2. Hardware Timer (`INT 08h`), PIT Scaling & Specific EOI
`MTHREAD` hooks hardware `INT 08h` (IRQ0) directly rather than the BIOS user tick `INT 1Ch`:
* **Avoiding the `INT 1Ch` Double-EOI Hazard:** The IBM PC/XT/AT ROM BIOS `INT 08h` handler sends a hardcoded Non-Specific EOI (`OUT 20h, 20h`) *after* `INT 1Ch` returns. Switching stacks inside `INT 1Ch` either starves the 8259 PIC of an EOI or causes a delayed Non-Specific EOI to clear an unrelated lower-priority IRQ when the original thread eventually resumes.
* **Power-of-2 PIT Scaling:** `thread_init(pit_shift, main_prio)` reprograms Channel 0 of the 8253/8254 PIT to run at $1\times$ (18.2 Hz), $2\times$ (36.4 Hz), $4\times$ (72.8 Hz), or $8\times$ (145.6 Hz) the stock tick rate. On sub-ticks, `isr_timer` issues a **Specific EOI for IRQ0 (`OUT 20h, 60h`)** directly to the 8259 PIC; on every $2^{\text{pit\_shift}}$-th tick, it chains to the original ROM BIOS `INT 08h` handler on `isr_stack` so the system clock and floppy motor shutoff timer remain accurate.
* **Built-in `HLT` Idle Loop:** When all threads are sleeping or blocked on synchronization primitives, the scheduler sets `current_tid = 0xFF` (`NO_THREAD`) and executes an interrupt-enabled `HLT` loop directly on `isr_stack`, avoiding the need to waste a `TCB` slot or stack on an idle thread.

### 3. Priority-Based Round-Robin Scheduling
Each thread is assigned a priority from `0` (`MT_PRIO_IDLE`) to `7` (`MT_PRIO_REALTIME`). During every scheduling pass:
1. The dispatcher begins scanning `tcb_table` at `(current_tid + 1) & 7`.
2. It selects the first `STATE_READY` thread whose effective priority (`tcb_prio`) is **strictly greater (`>`)** than the best priority seen so far during the scan.
3. Using strict `>` combined with a rotating start index guarantees that threads of equal priority share the CPU in round-robin order, while always preempting in favor of higher-priority runnable threads.

### 4. Hybrid MS-DOS (`INT 21h`) Reentrancy & Stack-Tier Splitting
MS-DOS is non-reentrant because `INT 21h` switches `SS:SP` onto one of three internal 384-byte stacks inside the Swappable Data Area (SDA). `MTHREAD` hooks `INT 21h` and classifies every DOS call into four tiers:
* **Tier 0 (Stackless Calls: `AH = 33h, 50h, 51h, 62h, 64h`):** Passed straight through to DOS immediately with zero locking or swapping.
* **Disjoint Stack Tiers (`IOStack` vs. `DiskStack` vs. `AuxStack`):** Because DOS uses separate 384-byte stacks for character I/O (`AH = 01h..0Ch` on `IOStack`), file/memory calls (`AH >= 0Dh` on `DiskStack`), and error handling (`AH = 59h` or `ErrorMode != 0` on `AuxStack`), two threads in *different* tiers only collide on the **24-byte (`DX = 0018h`) SDA header** at `SDA+00h..17h` (which holds `user_SP`, `user_SS`, `InDOS`, `CurrentDTA`, and `CurrentPSP`). Each `TCB` embeds a 24-byte `sda_hdr` snapshot, allowing `swap_sda_headers` to swap that header in 12 `MOVSW` instructions (~150 clock cycles) when switching between threads in different DOS stacks.
* **Same-Tier `IOStack` Collision (`AH = 01h..0Ch`):** Character device calls do not touch the global FAT or `BUFFERS=` linked lists. If Thread A is blocked waiting for console input on `IOStack` and Thread B calls `INT 21h` (`AH = 01h..0Ch`), `MTHREAD` swaps Thread A's 16-byte console state (`SDA+18h..27h`) and 384-byte `IOStack` into a 400-byte buffer (`io_swap_buf`) so Thread B can perform console I/O without waiting.
* **Same-Tier `DiskStack` / `AuxStack` Collision:** Because disk calls mutate global **non-SDA** kernel structures (SFTs, FAT cache, `BUFFERS=` LRU chain), a second thread requesting `DiskStack` blocks in `STATE_WAIT_DISK` and applies **Priority Inheritance** to boost the `DiskStack` owner's priority until it exits `INT 21h`.

### 5. ROM BIOS Reentrancy Guards (`bios_lock`)
Wrappers around `INT 10h` (Video), `INT 13h` (Disk), `INT 15h` (System), `INT 16h` (Keyboard), and `INT 17h` (Printer) increment a code-segment counter (`cs:[bios_lock]`) before calling the ROM BIOS and decrement it on return (preserving BIOS output flags via `RETF 2`). If `INT 08h` fires while `bios_lock > 0`, sleep counters still advance, but context switching is deferred until the BIOS call completes.

### 6. C Runtime Stack Probes (`STKHQQ`) & Lazy Heap Reaping
* **`STKHQQ` Synchronization:** By default, Microsoft C 7.00 inserts calls to `__chkstk` at function prologues, comparing `SP` against `DGROUP:STKHQQ`. Every `TCB` records its stack's lower guard address (`stk_limit`) and updates `STKHQQ` during every context switch, providing per-thread hardware stack overflow checking without requiring `/Gs`.
* **Lazy Zombie Stack Reaping:** When `thread_create()` is passed `stack_ptr = NULL`, it allocates the thread stack via `malloc()`. When that thread exits via `thread_exit()`, it cannot call `free()` while still running on that stack; instead, it transitions to `STATE_ZOMBIE` on `isr_stack`, and the next call to `thread_create()` or `thread_shutdown()` reclaims the heap block safely.

### 7. Synchronization Primitives
`MTHREAD` provides four primitives backed by 1-byte bitmask wait queues (`wait_mask`), priority-ordered wakeup (`wake_highest_waiter`), direct ownership/token handoff, and immediate preemption when a woken thread outranks the caller:
* **`mt_event_t`:** Win32-style Manual-Reset and Auto-Reset events (`event_set`, `event_reset`, `event_pulse`, `event_wait`).
* **`mt_mutex_t`:** Recursive mutex with priority inheritance (`mutex_lock`, `mutex_trylock`, `mutex_unlock`).
* **`mt_sem_t`:** 16-bit counting semaphore (`sem_wait`, `sem_trywait`, `sem_post`).
* **`mt_cond_t`:** Condition variable with atomic mutex release and re-acquisition (`cond_wait`, `cond_signal`, `cond_broadcast`).

---

## Limitations & Architectural Trade-Offs

1. **Maximum of 8 Concurrent Threads (`MAX_THREADS = 8`):**
   Wait queues (`wait_mask`) and active thread masks are represented as 8-bit bitmasks (`1 << tid`) so scheduler scans and wakeups execute in constant time without pointer manipulation. Supporting more than 8 threads requires widening `wait_mask` to 16 bits (`DW`) or switching to intrusive doubly-linked TCB queues.
2. **C Runtime Library (CRT) Non-Reentrancy:**
   While `MTHREAD` serializes and swaps MS-DOS (`INT 21h`) and guards ROM BIOS interrupts, standard single-threaded DOS C runtime functions that mutate static internal buffers or global heap structures (`malloc`/`free`, `printf`/`fopen` sharing the same `FILE` stream, `strtok`, `rand`, `localtime`) are not automatically thread-safe. Concurrent C heap or shared-stream calls from worker threads should be wrapped in a `mt_mutex_t`.
3. **8087 / 80287 FPU State Not Saved:**
   To keep the `TCB` at 64 bytes and context switches under ~1,000 cycles on a 4.77 MHz 8088, the 94-byte x87 math coprocessor state (`FSAVE`/`FRSTOR`) is not saved on context switches. Floating-point operations should either be confined to a single thread or guarded with a mutex.
4. **Two-Way `IOStack` Swap Buffer:**
   A single 400-byte `io_swap_buf` is statically allocated to resolve `IOStack` collisions between two concurrent console threads (e.g., one blocked in `getch()` while another prints). If a *third* thread simultaneously enters `INT 21h` (`AH = 01h..0Ch`), it blocks in `STATE_WAIT_IO` until one of the first two exits.
5. **Blocking BIOS Calls (`INT 16h, AH=00h`):**
   Because `bios_lock` defers preemption while executing inside a raw BIOS interrupt, calling a blocking BIOS routine like `INT 16h, AH=00h` (Wait for Keypress) directly from a thread holds `bios_lock > 0` until a key is pressed. Threads should either poll via `_kbhit()` (`INT 16h, AH=01h`) + `thread_sleep()`, or use DOS `INT 21h` console input (which runs with `bios_lock == 0` between polls and supports `IOStack` swapping).
6. **Single-Hop Priority Inheritance:**
   `mt_mutex_t` boosts a lower-priority mutex owner to the highest waiting thread's priority and restores `tcb_base_prio` when the mutex is unlocked. It does not track transitive lock chains across multiple nested mutexes held simultaneously by the same thread.
