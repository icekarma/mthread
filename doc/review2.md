# MTHREAD — Exhaustive Code Review: Consolidated Findings

**Date:** 2026-11-05 · **Team:** 7 parallel specialist reviewers + lead adjudication
**Scope:** `src/mthread/mthread.asm` (2836 L), `src/input/{kbd,mouse}.asm`, `src/video/{video.c,videohlp.asm}`, `include/mthread/*`, `src/demos/*` (8 programs), `Makefile`, `README.md`
**Per-area working reports:** [asm-timer-sched.md](asm-timer-sched.md) · [asm-lifecycle.md](asm-lifecycle.md) · [asm-sync.md](asm-sync.md) · [asm-dos-ints.md](asm-dos-ints.md) · [input-video.md](input-video.md) · [api-cpp.md](api-cpp.md) · [build-demos.md](build-demos.md) · [LEAD-verification-notes.md](LEAD-verification-notes.md)

---

## 1. Headline assessment

**As committed, this kernel cannot have completed a demo run.** Six independent Critical defects each alone break every program: the kernel should not even link (phantom CRT symbol §F-C6), and if it did link, the first `mutex_release` that wakes tid 0/1 corrupts memory outside `tcb_table`, the first wrapped BIOS call (e.g. `video_gotoxy` → `int 10h`) silently makes the calling thread non-preemptible forever, the first tiered `INT 21h` merge clobbers the app's TF, the idle path writes SDA headers into the wrong TCB, and second-thread DOS entry never gets a clean `InDOS=0` header at all.

Severity tally after cross-checking every claim (≥2 independent confirmations or a lead simulation/code-trace each):

| Severity | Count | Where |
|---|---|---|
| **Critical** | 6 | §3 |
| **High** | 10 | §4 |
| **Medium** | 23 | §5 |
| **Low** | 31 | §6 |
| **Open (toolchain-gated)** | 5 | §7 |

Disputes between reviewers were resolved by 16-bit simulation or code re-trace; every adjudication is logged in §2. "Verified clean" invariant lists are preserved in §9 to prevent re-hunting.

---

## 2. Adjudication log (who was wrong, and how we know)

| # | Dispute | Ruling | Basis |
|---|---|---|---|
| J-1 | **wake_highest_waiter mask math** — asm-sync's wait-queue review said clean; lead flagged it | **BUG (F-C1), lead confirmed** | Lead 16-bit simulation: `mov ah, tid_bit_lut[bx]` parks the wait-bit in BX bits 8–15; `SHL bx,6` then yields TCB+0x4000 for tid 0 and +0x8000 for tid 1 instead of +0x0000/+0x0040. tid 2–7 accidentally clean (bit shifts out of 16-bit BX). |
| J-2 | **`retf 2` wrapper** — lead's first pass said "loses 2 bytes of caller stack"; asm-dos-ints said "stack balanced" | **dos-ints right, lead wrong (F-C3)** | Real-mode `RETF` pops only **IP, CS** (far `CALL` pushes only CS:IP — flags were never call-pushed). On the 3-word `INT`-entry frame: pops IP+CS, `+2` consumes the FLAGS word → balanced consumption, but the caller's FLAGS word is **discarded, never restored**; execution resumes with gate-context flags (IF=0). Stack-corruption framing withdrawn. |
| J-3 | **0FD5h merge mask fix value** — code comment implies `0CD5h`; dos-ints proposed `0D55h` | **Both reviewers' proposed constants are wrong; fix = `0CD5h`** | Simulation: `0CD5` = {CF,PF,AF,ZF,SF,DF,OF} (exactly the comment's list). `0FD5` (in code) adds **TF+IF**; `0D55` (proposed fix) drops **SF** and keeps **TF**. |
| J-4 | **ADI-5 IF-starvation determinism** — lead first pass: "IF=0 always, Critical"; dos-ints: "TF deterministic, IF via TSR chains, High" | **dos-ints right → High** | DOS STIs internally during most INT 21h functions; the far-`CALL`/`RETF` boundary carries live flags (no FLAGS pop), so post-call `pushf` normally captures IF=1. Deterministic damage is TF loss (breaks CodeView single-step); IF=0 starvation when a chained TSR handler returns with IF=0. Kept as High (was lead Critical). |
| J-5 | **ALC-2 join "no re-test after wake"** | **REJECTED — false positive** | Exhaustive wake-source enumeration: every path that sets `STATE_READY` acts only on bits of the target's own `wait_mask`/`join_wait_mask`/`join_all_mask` (event/mutex/sem/cond wake helpers) or on `SLEEPING` (timer scan) or on `WAIT_DISK/AUX/IO` (`wake_dos_loop`) — none can reach a `STATE_WAIT_SYNC` joiner that is in no other mask; the sole wake for a joiner is its target's `thread_exit` `xchg` (which also clears the masks). No spurious early return exists. |
| J-6 | **ALC-4 BYTE PROC params** (lifecycle: "breaks"; api-cpp: "verified clean" → retracted) | **OPEN → §7-1** | No authoritative doc found in MASM 6.0/6.1 PG, MASM Reference, Learn, KB Q104618/Q39371; api-cpp retracted its claim and filed ACP-11. Lead additionally corrects lifecycle's **direction**: cdecl pushes right-to-left, so the **second** BYTE param (not the first) lands at the odd BP offset — e.g. `thread_set_priority(tid, new_prio)`'s `new_prio` would always read 0 (not `tid`). Also corrects api-cpp's offset table: MASM's prologue is `push bp / mov bp,sp / push <USES>` so params sit at `[bp+4]` regardless of USES registers. |
| J-7 | **`STKHQQ`** — lead: phantom symbol; demos-build: MS-C-CRT-only | **Kept Critical-conditional (F-C6)** | Documented MSC 16-bit stack-probe watermark is `_stkoff` (the Seyfarth MSJ MTHREAD design this project mirrors assigns `_stkoff` per switch); no `_STKHQQ` definition exists in-tree; non-MS toolchains definitely lack it (BD-5). Falsifiable in one command against the real LIB (§7-2). |
| J-8 | **lead `malloc` ABI worry (ES:BX)** | **WITHDRAWN** | Re-read of mthread.asm:1697–1708: result consumed as DX=segment/AX=offset = MS C's documented DX:AX 32-bit far-return. Correct. |
| J-9 | **lead `enter_idle_hlt` BL severity (High) vs timer-sched's Critical (ATS-1)** | **Critical (F-C5)** | `swap_sda_headers` scales BL by TCB stride 64; BL on the idle path is the leftover scan pointer of `schedule_next_thread` (a *non-TID* value), so the preempted-in-DOS thread's SDA header is saved into a wrong (up to far out-of-table) TCB — corruption on its next resume; every demo reaches "all blocked while a thread is in DOS" (e.g. getchar-blocked main). |
| J-10 | **`wake_all_waiters`/`block_on_sync`/`preempt_if_higher` entry contracts** | Clean | Lead re-trace agrees with asm-sync (rolling-CH scan, state-before-switch, IF=0 entry contracts honored). |

Lead independently re-verified from source (no reviewer reliance): F-C1, F-C2 epilogue, ADI-1 header-skip branches (:428–429, :452–453), `in_scheduler` zero references (only :98), `kbd_getkey` check→`int 16h` (kbd.asm:191–200), `video_putu8` ≥100 (video.c:110–113), `MThread::Create` publish order (mthrd.h:53–63), class includes inside mthread.h's `extern "C"` (mthread.h:12–34 ↔ thread.h:78–79, synchron.h:73–77), `thread_create` malloc DX:AX, unconditional hook installs (:1469–1484), `unhook_hardware` idempotence & RESTORE_VEC loop, `thread_set_priority` dual base+eff write (:2150–2151), `DEFINE_ABS_DISK_WRAPPER` plain `retf` (contrast for J-2).

---

## 3. Critical

### F-C1 — `wake_highest_waiter` computes the wrong TCB address for tids 0 and 1
**src/mthread/mthread.asm:2171–2218** — after finding the highest-priority waiter in AL, `mov bl,al / xor bh,bh / mov ah, tid_bit_lut[bx] / test cl,ah …` leaves the candidate mask bit in **AH**, then `SHL bx,6` (stride 64) shifts *BX* — waking bit ×2^14 offset added to the table base. tid 0 → TCB+0x4000, tid 1 → TCB+0x8000: writes `tcb_state=READY`, priority bookkeeping, and the preempt decision against memory far outside `tcb_table` (dgroup data/stack below); tid 2–7 accidentally correct. Every `event_set`/`mutex_release`/`sem_post`/cond wake that picks tid 0 (main thread!) or tid 1 corrupts kernel state.
**Fix:** `mov bl, al / xor bh, bh / SHL bx,6 / add bx, OFFSET tcb_table`, and test the mask with `test cl, 1`-style bit kept in a separate register (see `wake_all_waiters` :2225–2250 for the correct rolling-CH pattern).

### F-C2 — `INT 21h` gatekeeper never installs a clean SDA header for a fresh DOS entry (InDOS stays latched)
**src/mthread/mthread.asm:810–1060, 416–489** — `swap_sda_headers` saves/restores a thread's 24-byte SDA header **only** when `TCB_FLG_IN_DOS` is already set (:428–429, :452–453); a thread *newly* entering DOS gets no header install. If thread A is preempted inside real DOS (the design's whole point), the live SDA keeps A's header with `InDOS=1`; thread B's `mark_in_dos_and_call` then calls real DOS (:957–958) with InDOS=1 → DOS treats B as a re-entrant call: no per-thread stack switch, B runs on its own user stack, uses **A's DTA/PSP/CurrentPSP**, and DOS's RMS wait can spin the system. Corollaries (from asm-dos-ints): same-thread nested INT 21h is misrouted (the InDOS-gate at :843–846 diverts it to Aux tier — see F-H4 for the wrong byte), and "stackless" AH=50h/51h write the wrong thread's header. README §4's advertised cross-tier concurrency is architecturally unimplemented. Outcome per DOS version (freeze vs. quiet corruption) is open → §7-3.
**Fix (design-level):** baseline each thread's `sda_hdr` at create; in `mark_in_dos_and_call`, when the caller's IN_DOS flag was clear, install the baseline header (InDOS=0, this thread's DTA/PSP) into the live SDA before chaining, then set the flag.

### F-C3 — BIOS/ISR wrappers return with gate-context flags (caller's FLAGS discarded, IF=0)
**src/mthread/mthread.asm:727–747 (`DEFINE_BIOS_WRAPPER`), :767–774 (`isr_int16`), :1200–1207 (`isr_int2a`)** — handlers entered via `INT n` (3-word frame) end with `retf 2`: pops IP+CS, consumes the FLAGS word as the imm cleanup, resumes the interrupted code with the wrapper's live flags — captured after the gate's `cli` path → **IF=0, TF=0, DF=0 permanently for the interrupted thread**. Any thread that touches a wrapped BIOS call (every `video_gotoxy`/`int 10h` → hook installed unconditionally at :1469–1484) stops being preempted. (INT 25/26 are safe: `DEFINE_ABS_DISK_WRAPPER` :1153–1166 correctly uses plain `retf` for the leave-flags-on-stack convention — the asymmetry proves the intent.)
**Fix:** `pop` the saved FLAGS into FLAGS itself (a single `popf` from the INT frame) i.e. make the epilogue `iret`-equivalent — `pop bx; pop es`-style manual restore or restructure to `iret` with bios_lock decrement hoisted; never `retf 2` on an INT-entry frame.

### F-C4 — `finish_int21_exit` merge mask `0FD5h` clobbers TF (deterministically) and IF (TSR chains)
**src/mthread/mthread.asm:1033–1037** — `and ax, 0FD5h / and WORD PTR ss:[bx+14], NOT 0FD5h / or …` merges DOS-returned flags into the caller's IRET frame. `0FD5` = status flags **+ TF + IF**; the code comment's own list (CF,PF,AF,ZF,SF,OF,DF) is **`0CD5h`**. TF loss breaks CodeView/single-step around every gated DOS call; a TSR INT 21 chain returning IF=0 starves the thread of preemption (J-4). asm-dos-ints' proposed `0D55h` is itself wrong — it drops SF and keeps TF (J-3).
**Fix:** `0CD5h`.

### F-C5 — Idle path calls `swap_sda_headers` with a wild "outgoing TID"
**src/mthread/mthread.asm:400 (`enter_idle_hlt`) and :1959–2015 (`thread_exit` tail-jump)** — the contract at :413 requires `BL = outgoing TID`; the idle path sets only `DL = NO_THREAD` and calls with BL = `schedule_next_thread`'s leftover scan register. `swap_sda_headers` does `xor bh,bh / SHL bx,6 / add bx,OFFSET tcb_table` on it (J-9): the just-preempted thread's live SDA header is stored into the wrong TCB (arbitrary offset when BL > 7) and lost for its owner. HLT-idle with a thread parked inside DOS is the normal steady state of every demo.
**Fix:** `mov bl, current_tid` before `call swap_sda_headers` on both paths.

### F-C6 — `STKHQQ`: the MSC stack-probe watermark symbol does not exist (link blocker)
**src/mthread/mthread.asm:24 (`EXTERNDEF C STKHQQ: WORD`), writes at :375 (per-switch), :1273 (unhook), :1385 (init)** — Microsoft C 16-bit's `_chkstk` watermark is the CRT global **`_stkoff`** (the classic Seyfarth MSJ multithreading design assigns `_stkoff` per context switch — this code follows that design but with a renamed symbol). Nothing in the tree defines `_STKHQQ`; unless some CRT variant exports that exact name, **every** configuration dies at LINK with unresolved external. At minimum (BD-5): COMPILER=BCC/WATCOM can never link (no such CRT symbol there, and BCC passes `-N-` disabling stack checking entirely), so the advertised dual-toolchain support is false; the demos' on-screen "STKHQQ probes: ON" banner is unverifiable even in MS builds (BD-10: the probe demo never crosses the ~128 B `__chkstk` threshold).
**Fix:** confirm the CRT export (§7-2); use `_stkoff` (and its BCC/Watcom equivalents or drop the feature there).

---

## 4. High

| ID | Where | One-line defect |
|---|---|---|
| F-H1 | mthread.asm:142–410, :98 | **No scheduler re-entrancy guard on `isr_stack`** (ATS-2): `in_scheduler` is declared "Reentrancy guard" but referenced nowhere (lead-verified); a nested `INT 08h` during the IF=1 windows (chained BIOS INT 08 handler :247–248, TSRs) re-enters `isr_timer`, resets SS:SP to the shared `isr_stack`, and destroys the outer ISR's in-flight scratch/frames. |
| F-H2 | mthread.asm:1664–1787 | **`thread_create` slot claim race** (ALC-1): TCB slot scan runs with IF=1 (CLI only at :1787) and `malloc` executes inside the window — two concurrent creates can be handed the same tid/TCB/stack. |
| F-H3 | mthread.asm:1959–2015 | **`thread_exit` never releases tier ownership** (ALC-3 + lead L-6): a thread that dies while owning IO/Disk/Aux tier (boosted/condvar/abort paths) leaves `*_owner` pinned → permanent console-I/O or disk starvation. |
| F-H4 | mthread.asm:843–846, :1275–1278 | **SDA byte-0 is `InDOS`, not `ErrorMode`** (ADI-2; real layout InDOS@+0 / ErrorMode@+1 for DOS 3.x–6.x): the classifier's "ErrorMode≠0→Aux" gate actually tests InDOS (drives F-C2's misrouting), the real ErrorMode rule is never applied (ErrorMode-mode calls double-book the physical AuxStack), and `unhook_hardware`'s "Clear ErrorMode" actually clears InDOS. |
| F-H5 | mthread.asm:1246–1326, :1127–1131 | **INT 23h 3rd-^C abort unhooks via live DOS** (ADI-3): `RESTORE_VEC`×20 does `call [old_int21]` AH=25h mid-operation while InDOS was just clobbered → DOS re-entrancy corruption at the worst possible moment (during a disk operation). Fix: direct IVT stores under CLI. |
| F-H6 | mthread.asm:784–785, :1120–1123 | **`break_count` double-increment** (ADI-6): INT 1Bh *and* INT 23h both bump it → force-abort fires on the **2nd** Ctrl+C (README §6 says 3rd), never reset → cumulative across the program; `break_flag` sticky. |
| F-H7 | mthread.asm:752–765, :1367–1368, :1504–1507 | **IOStack calibration unsound** (ADI-9, absorbs ALC-8/ALC-10): SP sampled after two pushes (−4 skew), `SS` never validated against the SDA IOStack segment, and a silent magic `SDA+458h` fallback whenever the AH=0Bh→INT 16h probe never fires (e.g. redirected stdin at init) → the 400-byte `exchange_iostack_slice` swaps the wrong window → corruption on the first two-thread IO collision. |
| F-H8 | kbd.asm:191–200 | **`kbd_getkey` "non-blocking" is false** (IV-1, lead-verified): BDA head/tail check → `int 16h` TOCTOU — if another thread drains the ring in between, the hooked INT 16h takes `bios_lock` and blocks inside BIOS → timer fast-path `timer_locked_tick` → **whole-kernel stall** until a keystroke. (cinput demo also unhooks while a worker sits there — BD-11.) |
| F-H9 | cdemo.c:119–129/:247, cppdemo.cpp:121–131/:250 | **Demo heap race** (BD-1): HIGH-priority logger `fopen/fprintf/fclose` vs main's `printf/_getch` on non-reentrant MSC stdio/malloc, zero mutexes in these two files (csync/cppdemo prove the authors knew the README-Limitation-#2 workaround). Preemption mid-`malloc` → heap corruption — which also means any "the demos run fine" evidence is void. |
| F-H10 | Makefile:21–23, :82 | **Model/CPU/FPU switches reuse stale objects** (BD-2): `MODEL=`/`CPU=`/`FPU=`/`COMPILER=` are command-line macros that touch no timestamps → `nmake` then `nmake MODEL=LARGE` relinks the `.MODEL SMALL`-built `mthread.obj` into far-pointer app objects → guaranteed near/far ABI crash. Needs a config stamp file or separate obj dirs. |

---

## 5. Medium

| ID | Where | Defect (source report) |
|---|---|---|
| mthread.asm:540–667 | `fpu_save_bx` does `FNSAVE` **then** `FWAIT`; the wait must precede the save (pending exception handled against already-restored state) (ATS-3) |
| mthread.asm:243–253 | **DS never re-established after chained BIOS INT 08h** (ATS-5): BIOS/TSR handler may return with foreign DS; dispatcher contract requires DS=DGROUP at :314 |
| mthread.asm (IF @Model EQ 1) | **TINY+EMU build drops the CS cross-check** in emu-busy detection → deep stacks misread as "emulator busy", preemption silently disabled (ATS-4) |
| mthread.asm:1097–1100 vs :2586–2588 | **PI boost mutual-clobber** (ADI-13): `restore_prio_and_wake` and `mutex_unlock` each unconditionally write `tcb_base_prio`; plus set_priority writes base+eff (:2150–2151) erasing an in-flight boost (ALC-5) |
| mthread.asm:1370–1399, :1557 | **Re-init leaves stale scheduler state** (ALC-6 / ADI-14): `bios_lock`, tier owners, `fpu_owner`, per-thread `sda_hdr` never reset; forced `sti` |
| mthread.asm:1699–1735 | **Stack-segment 64 KB wrap handled only in HUGE** (ALC-9): `stk_limit = off+64` and `top = off+stack_size` computed without carry into the segment in COMPACT/LARGE |
| mthread.asm:1697, :1610–1650 | **Kernel `malloc`/`free` run with IF=1** (ALC-11): non-reentrant CRT heap, preemptible mid-allocation, user cannot serialize the kernel's use |
| mthread.asm:1829–1891 | **`thread_enable_fpu` files live FPU state into the *previous* owner's slot then loads a clean image** (ALC-12) — real state loss on the documented misuse sequence |
| mthread.asm:1358–1368 | **`INT 21h AX=5D06h` unchecked** (ALC-7/ADI-15): failure → `sda_seg=0`, classifier reads the IVT as SDA |
| mthread.asm:2310–2360 | **`cond_wait` releases a mutex it doesn't own** (ASY-1): drives `mtx_count` toward 0 under a live owner; no ownership check (also ACP-4 docs) |
| mthread.asm:1186–1208, :1067–1092 | **INT 2Ah critical sections asymmetric** (ADI-10 + lead L-5): AH=82h "end all" decrements once; global `bios_lock` not paired per-thread with AH=80/81; holder blocking in boost livelocks via `timer_locked_tick` |
| mthread.asm:967–1027 | **Post-call release frees the first-match tier, not the completed call's tier** (ADI-11, latent behind F-C2) |
| mthread.asm:906–930 | **IO-claim snapshots the live SDA header even when it belongs to a third IN_DOS thread** (ADI-12, latent) → owner resumes on foreign SS:SP |
| mthread.asm:1141–1143 | **INT 24h stores DI twice; AH (error code) lost** (ADI-7) → `threading_get_critical_error` useless |
| mthread.asm:1147 | **INT 24h ends `iret` instead of documented `RETF 2`** (ADI-8): far-call convention + drive word — accidental balance but FLAGS loaded from the drive number (IF=0); version-dependent, verify |
| mthrd.h:53–63 | **`MThread::Create` publishes `tid` after the thread is already runnable** (ACP-1, lead-verified): `GetTid()`'s `thread_self()` fallback masks it for self-queries but returns the *caller's* tid for never-created objects (misleading API) |
| thread.h / thread_create | **Priority/pit_shift AND-masked, not validated** (ACP-2 + ALC-14/ATS-7): out-of-range wraps to valid-looking value |
| include/mthread/* | **No `volatile` anywhere** (ACP-3): ISR-written `mt_event_t` fields pollable via `MEvent::IsSet()` → compiler-cached loop under large-model optimization |
| mthread.asm:2171–2218 (event paths) | **`event_set`/`event_pulse` carry DS in DX into `preempt_if_higher` which clobbers DX** (ASY-2, latent — mutex/sem/cond correctly use CX) |
| kbd.asm ring + README §10 | **Full ring silently drops *button* events** (IV-2) |
| kbd.asm:113–131 | **`kbd_init` ignores event pointer on re-init** (IV-3) |
| video.c:110–113 | **`video_putu8` prints `':'..'I'` for values ≥ 100** (IV-4, lead-verified): `(val>=10)?('0'+val/10):' '` assumes two digits; telemetry fields show punctuation |
| Makefile:19 vs :5 / README:23,:110 | **`FPU = OPTIN` default contradicts "disabled by default"** (BD-3) and silently doubles ISR stack |
| cfpu.c:46, cppfpu.cpp:46 | **`thread_enable_fpu` return ignored + C never gets `-DUSE_FPU`** (BD-4): `nmake FPU=NONE` → silent garbage sine wave |
| Makefile:89–114 | **BCC/Watcom toolchain rules broken**: `wcl` (C driver) compiles .cpp (BD-6); `tlib` lacks `;` terminator → prompt hang; `bcc -o$@` misuse (BD-7); Watcom `-fm=` invalid (BD-6) |
| Makefile:171–176 | **Dependency lists omit every ABI header** (BD-8): editing the structs shared with asm rebuilds nothing |
| Makefile:85 + cdemo mallocs | **No `/HEAP` sizing** (BD-9): 3–5 thread stacks + FILE buffers vs ~1 KB MS C default heap |
| cdemo.c:30–39 | **`stack_probe_test` never crosses `__chkstk`'s ~128 B threshold** (BD-10) — "STKHQQ probes: ON" banner unexercised (compounds F-C6) |
| cinput.c:321–329 | **Input shutdown unhooks INT 09h/33h while worker may be inside `kbd_getkey`/`mouse_get_event`** (BD-11, pairs with F-H8) |

---

## 6. Low (compressed)

**Timer/sched (ATS):** `in_scheduler` dead code (:98, ATS-6); `pit_shift_val` write-only + silent shift coercion (ATS-7); comment/label hygiene (ATS-6/7).
**Lifecycle (ALC):** unconditional `sti` in create/set_priority (13); mask-not-validate (14, w/ ACP-2); no NO_THREAD guard in exit/yield/sleep → OOB `tid_bit_lut` index if called before init (15 — benign today, add guard); pre-init queries use DS=0 (16); **PIT programmed mode 3 vs README "mode 2"** both init and restore (17); recycled TID inherits previous tenant's saved regs (18); caller stacks not bounds/size-checked (19); `thread_self`/`get_priority` edge semantics (20); main-thread `thread_exit` parks machine in HLT with vectors still hooked (10 — kept Medium-adjacent).
**Sync (ASY):** recursive mutex depth 255→0 wrap wedges mutex (3); `sem_post` count wraps 65535→0 (4); auto-reset `event_pulse` wakes only ONE waiter vs Win32 `PulseEvent` all-wake (5 — real README gap); non-owner `mutex_unlock` silently ignored (6).
**Input/video (IV):** `EXTERN` for C globals breaks project decoration convention (5); `video_init` triple BDA reads unshielded (6); `video_puts` no length clip + negative-offset clamp swallow (7); `kbd_shutdown` clears event ptr before restoring vector (8); non-atomic vector get-then-set / `kbd_active` set early (9); INT 33h callback doesn't preserve BP (10); >2-button reported as 2, destructive reset re-run on recalibrate (11); sticky break_flag re-signals every keystroke (12).
**API/C++ (ACP):** `NULL` without stddef in mthrd.h/mthrmgr.h (5); `isr.h` orphaned — `event_set_isr`/`mt_enter_isr`/`mt_leave_isr` undeclared on documented include paths (6); **C++ classes lexically inside mthread.h's `extern "C"`** when using the master header — via-thread-h-only, latent (7, lead-verified include chain); README names 4 nonexistent `thread_*` API names (8, = BD-15/ADI-16: `thread_init/thread_abort/thread_is_break_requested/thread_get_critical_error` → real names `threading_*`); undocumented return polarity/argument coercion (9); `video.h` `horiz_bkgr_attr` vs `video.c` `horz_bkgr_attr` drift (10).
**DOS-ints (ADI):** init hygiene (14, w/ ALC-6); 5D06h/DOS-version floor docs (15, w/ ALC-7); README §4/§6 claim drift (16).
**Build/demos (BD):** 82-char banner spills 2 chars onto row 4 (12); dead `#if defined RULER` in 6 demos (13); every `thread_create`/`Create` return value discarded in demos (14); `cls` + fake `.PHONY` + `/P` race (16); `clean` misses `.pdb/.tds` (17); TASM always `/m2` (18).

---

## 7. Open — needs real MASM/DOS toolchain (not resolvable from source)

1. **ALC-4/ACP-11 — MASM `BYTE` PROC parameter sizing** (the big one): `threading_init(pit_shift,main_prio)`, `thread_set_priority(tid,new_prio)`, `event_init(ev,manual_reset,initial_state)`, `video_gotoxy(row,col)`, `mouse_set_pos(row,col)` declare adjacent `BYTE` params (mthread.asm:1335/2132/2310, videohlp.asm:30, mouse.asm) while MSC cdecl pushes **one word per char arg**. If MASM sizes parameter slots by declared type (classic mixed-language pitfall → "declare char params as WORD"), the **second** byte of each pair reads the first param's pushed high byte = always 0 ⇒ `new_prio`/`initial_state`/`main_prio`/`col` silently zero (cursor always column 0 in every demo — which is why this *may* be fine if the author's builds worked; but this repo demonstrably never ran — see §1). **No** authoritative doc located (J-6). One-command resolution: `ml /Sx /c /Fl mthread.asm` and read the parameter offsets (expected `[bp+4]`+`[bp+6]` promoted vs `[bp+4]`+`[bp+5]` packed).
2. **F-C6 — does any CRT export `_STKHQQ`?** `strings MLIBCE.LIB | grep -i stk` (documented watermark is `_stkoff`). If absent → link failure in all configs (already treated as Critical; this check upgrades/downgrades it).
3. **F-C2 outcome per DOS version** — does the RMS InDOS wait spin with IF=1/INT 28h (livelock, recoverable) or IF=0 (freeze)? Test DOS 3.30, 5.0, 6.22. Also validate the `SDA+458h` IOStack constant per version (F-H7, ADI-9).
4. **ADI-8 — INT 24h epilogue convention** (`RETF 2` vs `iret`) on target DOS versions.
5. **387-emulator entry form** — `DB 0CDh,039h…` literal-entry pattern vs INT 3Eh/0CDh usage in the emulator-busy probe (ASM-1 open question; low risk).

## 8. README/limitations vs code (roll-up; Limitations items are *documented tradeoffs*, excluded; deviations included)

Mode 3 vs "mode 2" PIT (ALC-17) · PIT "4x = 72.8 Hz" banners vs pit_shift reality (ATS-7) · Ctrl+C "3rd press" vs double-count (F-H6) · §4 cross-tier concurrency unimplemented (F-C2) · §10 mouse button queueing (IV-2) · Win32 `PulseEvent` all-wake (ASY-5) · `FPU=NONE` default (BD-3) · BCC/Watcom support claims (BD-5/F-C6) · `threading_*` naming (ACP-8/BD-15/ADI-16) · "exact IOStack calibration" (F-H7) · `video_ruler` (BD-13) · STKHQQ probes banner (BD-10).

## 9. Verified-clean invariants (do not re-hunt)

INT 21h wrapper stack balance + caller reg fidelity (DS:DX intact at chain); IF=0 atomicity of tier claim/release/400B-swap; `io_swap_buf` single-occupancy; unhook idempotence/coverage + PIT 18.2 Hz restore; INT 25/26 `retf` convention correct; INT 23 `stc/retf` + 0040:0071; 8086 instruction legality (all 186 ops gated via SHL_IMM/SHR_IMM); `wake_all_waiters` rolling scan + `block_on_sync` + `preempt_if_higher` contracts; `reap_zombie_stack` double-free safety (OWN_STACK cleared under CLI); `soft_context_switch` synthetic-IRET frame; `swap_sda_headers` copy directions; `fpu_off_lut`/TCB-stride data layout; join-mask `xchg` handoff at exit (ALC-2 rejected, J-5); mutex/sem/cond check-then-block atomicity + token handoff; struct layouts byte-exact both compilers; 37 `PROC PUBLIC` ↔ header 1:1; `cmp_xchg` polarity; classes non-copyable; include guards; malloc DX:AX read (J-8); all 8 demos type-check vs headers; CPU flags reach ml **and** C; TINY blanks `/Zi`→`.COM`; USE_FPU no C-visible layout drift; csync ring no off-by-one; condvars always held mutex; mutexes balanced all demo exit paths; start-gate set-once; shutdown wakes all primitives pre-join; 1024 B spinner stack honors base+64/top−8; typeahead max-index 32/33; mouse/kbd teardown idempotent; `video_putu*` field widths vs templates (IV-4 is a *value* bug, not width).

## 10. Suggested fix order

1. **Link blockers**: F-C6 (`_stkoff`) — nothing is testable until this resolves.
2. **Single-line correctness wins**: F-C4 (`0CD5h`), F-C5 (`mov bl, current_tid`), F-C1 (BX/LUT restructure), F-C3 (epilogue `popf`).
3. **F-H4** (SDA byte offset) + F-C2 design fix (header baseline) — makes DOS tiering real; then re-test.
4. **F-H7 calibration hardening** (validate SS, fail loudly vs magic constant).
5. F-H1 (`in_scheduler` wiring), F-H2 (CLI-scoped slot claim), F-H3 (tier release on exit).
6. Toolchain/build matrix (BD-2 config stamp; BD-5/6/7; BD-8 deps) → *then* run demos to validate items 1–5; fix F-H9 with a stdio mutex (demos' own README workaround).
7. §7 open items on real DOS 3.3/5/6.22 + `ml /Sx` (settles ALC-4/ACP-11).
