#=============================================================================
# Makefile - MS-DOS Preemptive Multithreading Library & Demos (NMAKE)
#
# Usage Examples:
#   nmake                                  (Defaults: MSC, SMALL, 8086)
#   nmake MODEL=LARGE CPU=186              (MSC 7.00, Large model, 80186+)
#   nmake COMPILER=BCC MODEL=COMPACT       (Borland C++ 3.1 + TASM 4.1)
#   nmake COMPILER=WATCOM MODEL=SMALL      (OpenWatcom C/C++ + MASM 6.11)
#   nmake clean
#=============================================================================

# --- User-Configurable Build Options ---
COMPILER = MSC
MODEL    = SMALL
CPU      = 8086

# --- 1. Map Memory Model to Compiler Short Codes ---
!IF "$(MODEL)" == "SMALL"
MCODE_UPPER = S
MCODE_LOWER = s
!ELSEIF "$(MODEL)" == "MEDIUM"
MCODE_UPPER = M
MCODE_LOWER = m
!ELSEIF "$(MODEL)" == "COMPACT"
MCODE_UPPER = C
MCODE_LOWER = c
!ELSEIF "$(MODEL)" == "LARGE"
MCODE_UPPER = L
MCODE_LOWER = l
!ELSE
!ERROR Invalid MODEL="$(MODEL)". Choose SMALL, MEDIUM, COMPACT, or LARGE.
!ENDIF

# --- 2. Map CPU Option (8086 vs 186) ---
!IF "$(CPU)" == "186"
ASM_CPU_FLAGS = /DUSE_186
MSC_CPU_FLAGS = /G1
BCC_CPU_FLAGS = -1
WAT_CPU_FLAGS = -1
!ELSEIF "$(CPU)" == "8086"
ASM_CPU_FLAGS =
MSC_CPU_FLAGS = /G0
BCC_CPU_FLAGS = -1-
WAT_CPU_FLAGS = -0
!ELSE
!ERROR Invalid CPU="$(CPU)". Choose 8086 or 186.
!ENDIF

# --- 3. Configure Toolchain Commands & Flags ---
!IF "$(COMPILER)" == "MSC"
CC       = cl
AS       = ml
CFLAGS   = /c /A$(MCODE_UPPER) $(MSC_CPU_FLAGS) /W3 /Ox /Zi
AFLAGS   = /c /W3 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) /Zi
LINK_CMD = $(CC) /A$(MCODE_UPPER) $(MSC_CPU_FLAGS)
!ELSEIF "$(COMPILER)" == "BCC"
CC       = bcc
AS       = tasm
CFLAGS   = -c -m$(MCODE_LOWER) $(BCC_CPU_FLAGS) -w -O2 -N-
AFLAGS   = /m2 /mx /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS)
LINK_CMD = $(CC) -m$(MCODE_LOWER) $(BCC_CPU_FLAGS)
!ELSEIF "$(COMPILER)" == "WATCOM"
CC       = wcl
AS       = ml
CFLAGS   = -c -m$(MCODE_LOWER) $(WAT_CPU_FLAGS) -wx -ox -zc
AFLAGS   = /c /W3 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS)
LINK_CMD = $(CC) -m$(MCODE_LOWER) $(WAT_CPU_FLAGS)
!ELSE
!ERROR Invalid COMPILER="$(COMPILER)". Choose MSC, BCC, or WATCOM.
!ENDIF

# --- 4. Build Targets ---
TARGETS = demo.exe syncdemo.exe

all: $(TARGETS)

mthread.obj: mthread.asm mthread.inc mthread.h
    $(AS) $(AFLAGS) mthread.asm

demo.obj: demo.c mthread.h
    $(CC) $(CFLAGS) demo.c

demo.exe: demo.obj mthread.obj
    $(LINK_CMD) demo.obj mthread.obj

syncdemo.obj: syncdemo.c mthread.h
    $(CC) $(CFLAGS) syncdemo.c

syncdemo.exe: syncdemo.obj mthread.obj
    $(LINK_CMD) syncdemo.obj mthread.obj

clean:
    -if exist *.obj del *.obj
    -if exist *.exe del *.exe
    -if exist *.map del *.map
    -if exist *.bak del *.bak
    -if exist mttest.log del mttest.log
