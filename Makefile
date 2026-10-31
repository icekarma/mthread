#=============================================================================
# Makefile - MS-DOS Preemptive Multithreading Kernel & Demos (NMAKE)
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
CFLAGS   = /c /A$(MCODE_UPPER) $(MSC_CPU_FLAGS) /Gy /Ox /W3 /Zi /nologo
AFLAGS   = /c /W2 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) /Zi /nologo
LINK_CMD = $(CC) /A$(MCODE_UPPER) $(MSC_CPU_FLAGS) /nologo
LIB_CMD  = lib /nologo
!ELSEIF "$(COMPILER)" == "BCC"
CC       = bcc
AS       = tasm
CFLAGS   = -c -m$(MCODE_LOWER) $(BCC_CPU_FLAGS) -w -O2 -N-
AFLAGS   = /m2 /mx /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS)
LINK_CMD = $(CC) -m$(MCODE_LOWER) $(BCC_CPU_FLAGS)
LIB_CMD  = tlib
!ELSEIF "$(COMPILER)" == "WATCOM"
CC       = wcl
AS       = ml
CFLAGS   = -c -m$(MCODE_LOWER) $(WAT_CPU_FLAGS) -wx -ox -zc
AFLAGS   = /c /W3 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS)
LINK_CMD = $(CC) -m$(MCODE_LOWER) $(WAT_CPU_FLAGS)
LIB_CMD  = wlib -q -b -n
!ELSE
!ERROR Invalid COMPILER="$(COMPILER)". Choose MSC, BCC, or WATCOM.
!ENDIF

# --- 4. Build Targets ---
TARGETS = mthread.lib vid.lib input.lib demo.exe inpdemo.exe syncdemo.exe cppdemo.exe cppinp.exe cppsync.exe

all: cls $(TARGETS)

cls:
	@cls
	@echo Building preemptive multithreading kernel & demos
	@echo Compiler:     $(COMPILER)
	@echo Memory model: $(MODEL)
	@echo Target CPU:   $(CPU)
	@echo.

mthread.obj: mthread.asm mthread.inc mthread.h
	$(AS) $(AFLAGS) mthread.asm

mthrwrap.obj: mthrwrap.cpp mthread.h mthrwrap.h
	$(CC) $(CFLAGS) mthrwrap.cpp

mthread.lib: mthread.obj mthrwrap.obj
	-if exist mthread.lib del mthread.lib
!IF "$(COMPILER)" == "MSC"
	$(LIB_CMD) mthread.lib +mthread.obj +mthrwrap.obj;
!ELSE
	$(LIB_CMD) mthread.lib +mthread.obj +mthrwrap.obj
!ENDIF

videohlp.obj: videohlp.asm
	$(AS) $(AFLAGS) /DOFFSET_DISPLAY videohlp.asm

video.obj: video.c video.h
	$(CC) $(CFLAGS) /DOFFSET_DISPLAY video.c

vid.lib: video.obj videohlp.obj
	-if exist vid.lib del vid.lib
!IF "$(COMPILER)" == "MSC"
	$(LIB_CMD) vid.lib +video.obj +videohlp.obj;
!ELSE
	$(LIB_CMD) vid.lib +video.obj +videohlp.obj
!ENDIF

kbd.obj: kbd.asm mthread.inc
	$(AS) $(AFLAGS) kbd.asm

mouse.obj: mouse.asm mthread.inc
	$(AS) $(AFLAGS) mouse.asm

input.lib: kbd.obj mouse.obj
	-if exist input.lib del input.lib
!IF "$(COMPILER)" == "MSC"
	$(LIB_CMD) input.lib +kbd.obj +mouse.obj;
!ELSE
	$(LIB_CMD) input.lib +kbd.obj +mouse.obj
!ENDIF

demo.obj: demo.c mthread.h video.h
	$(CC) $(CFLAGS) demo.c

demo.exe: demo.obj mthread.lib vid.lib
	$(LINK_CMD) demo.obj mthread.lib vid.lib

inpdemo.obj: inpdemo.c mthread.h input.h video.h
	$(CC) $(CFLAGS) inpdemo.c

inpdemo.exe: inpdemo.obj mthread.lib vid.lib input.lib
	$(LINK_CMD) inpdemo.obj mthread.lib vid.lib input.lib

syncdemo.obj: syncdemo.c mthread.h video.h
	$(CC) $(CFLAGS) syncdemo.c

syncdemo.exe: syncdemo.obj mthread.lib vid.lib
	$(LINK_CMD) syncdemo.obj mthread.lib vid.lib

cppdemo.obj: cppdemo.cpp mthread.h mthrwrap.h video.h
	$(CC) $(CFLAGS) cppdemo.cpp

cppdemo.exe: cppdemo.obj mthread.lib vid.lib
	$(LINK_CMD) cppdemo.obj mthread.lib vid.lib

cppinp.obj: cppinp.cpp mthread.h mthrwrap.h input.h video.h
	$(CC) $(CFLAGS) cppinp.cpp

cppinp.exe: cppinp.obj mthread.lib vid.lib input.lib
	$(LINK_CMD) cppinp.obj mthread.lib vid.lib input.lib

cppsync.obj: cppsync.cpp mthread.h mthrwrap.h video.h
	$(CC) $(CFLAGS) cppsync.cpp

cppsync.exe: cppsync.obj mthread.lib vid.lib
	$(LINK_CMD) cppsync.obj mthread.lib vid.lib

clean:
	-if exist *.obj del *.obj
	-if exist *.exe del *.exe
	-if exist *.lib del *.lib
	-if exist *.map del *.map
	-if exist *.bak del *.bak
	-if exist mttest.log del mttest.log

.PHONY: all clean
