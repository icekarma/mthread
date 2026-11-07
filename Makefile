#=============================================================================
# Makefile - MS-DOS Preemptive Multithreading Kernel & Demos (NMAKE)
#
# Usage Examples:
#   nmake                                  (Defaults: MSC, SMALL, 8086, FPU=NONE)
#   nmake MODEL=TINY                       (MSC 7.00, Tiny model .COM files)
#   nmake MODEL=HUGE CPU=186               (MSC 7.00, Huge model, 80186+)
#   nmake MODEL=LARGE CPU=186 FPU=ALL      (MSC 7.00, Large model, 186+, FPU all)
#   nmake FPU=OPTIN                        (MSC 7.00, Per-thread FPU opt-in)
#   nmake COMPILER=BCC MODEL=COMPACT       (Borland C++ 3.1 + TASM 4.1)
#   nmake COMPILER=WATCOM MODEL=SMALL      (OpenWatcom C/C++ + MASM 6.11)
#   nmake clean
#=============================================================================

# --- User-configurable build options ---
COMPILER = MSC
MODEL    = SMALL
CPU      = 8086
FPU      = OPTIN

OBJDIR   = obj
BINDIR   = bin
LIBDIR   = lib
INCPATH  = include

# --- 1. Map memory model to compiler short code (MCODE) & extension (EXT) ---
EXT   = exe
MCODE = [$(MODEL)]
!IF "$(COMPILER)" == "MSC"
MCODE = $(MCODE:[TINY]=T)
MCODE = $(MCODE:[SMALL]=S)
MCODE = $(MCODE:[MEDIUM]=M)
MCODE = $(MCODE:[COMPACT]=C)
MCODE = $(MCODE:[LARGE]=L)
MCODE = $(MCODE:[HUGE]=H)
!ELSE
MCODE = $(MCODE:[TINY]=t)
MCODE = $(MCODE:[SMALL]=s)
MCODE = $(MCODE:[MEDIUM]=m)
MCODE = $(MCODE:[COMPACT]=c)
MCODE = $(MCODE:[LARGE]=l)
MCODE = $(MCODE:[HUGE]=h)
!ENDIF

!IF "$(MCODE)" == "[$(MODEL)]"
!ERROR Invalid MODEL="$(MODEL)". Choose TINY, SMALL, MEDIUM, COMPACT, LARGE, or HUGE.
!ELSEIF "$(MODEL)" == "TINY"
EXT   = com
!ENDIF

# --- 2. Validate CPU (8086 vs 186) & FPU (NONE, OPTIN, ALL) options ---
!IF "$(CPU)" == "186"
ASM_CPU_FLAGS = /DUSE_186
!ELSEIF "$(CPU)" == "8086"
ASM_CPU_FLAGS =
!ELSE
!ERROR Invalid CPU="$(CPU)". Choose 8086 or 186.
!ENDIF

!IF "$(FPU)" == "NONE"
ASM_FPU_FLAGS =
!ELSEIF "$(FPU)" == "OPTIN"
ASM_FPU_FLAGS = /DUSE_FPU
!ELSEIF "$(FPU)" == "ALL"
ASM_FPU_FLAGS = /DUSE_FPU /DFPU_DEFAULT_ALL
!ELSE
!ERROR Invalid FPU="$(FPU)". Choose NONE, OPTIN, or ALL.
!ENDIF

# --- 3. Configure toolchain commands & flags ---
!IF "$(COMPILER)" == "MSC"
!IF "$(MODEL)" == "TINY"
MSC_DBG   =
!ELSE
MSC_DBG   = /Zi
!ENDIF
CPU_FLAGS = $(CPU:8086=/G0)
CPU_FLAGS = $(CPU_FLAGS:186=/G1)
CC        = cl
AS        = ml
CFLAGS    = /c /A$(MCODE) $(CPU_FLAGS) /Gy /Ox /W3 $(MSC_DBG) /I$(INCPATH) /DOFFSET_DISPLAY /nologo
AFLAGS    = /c /W2 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(ASM_FPU_FLAGS) $(MSC_DBG) /I$(INCPATH) /DOFFSET_DISPLAY /nologo
FO_FLAG   = /Fo$@
AS_ARGS   = /Fo$@ $<
LINK_CMD  = $(CC) /A$(MCODE) $(CPU_FLAGS) /nologo
FE_FLAG   = /Fe$@ /Fm$(OBJDIR)\$(@B).map
LIB_CMD   = lib /nologo
LIB_END   = ;
!ELSEIF "$(COMPILER)" == "BCC"
CPU_FLAGS = $(CPU:8086=-1-)
CPU_FLAGS = $(CPU_FLAGS:186=-1)
CC        = bcc
AS        = tasm
CFLAGS    = -c -m$(MCODE) $(CPU_FLAGS) -w -O2 -N- -I$(INCPATH) -DOFFSET_DISPLAY
AFLAGS    = /m2 /mx /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(ASM_FPU_FLAGS) /I$(INCPATH) /DOFFSET_DISPLAY
FO_FLAG   = -o$@
AS_ARGS   = $<, $@
LINK_CMD  = $(CC) -m$(MCODE) $(CPU_FLAGS)
FE_FLAG   = -e$@
LIB_CMD   = tlib
LIB_END   =
!ELSEIF "$(COMPILER)" == "WATCOM"
CPU_FLAGS = $(CPU:8086=-0)
CPU_FLAGS = $(CPU_FLAGS:186=-1)
CC        = wcl
AS        = ml
CFLAGS    = -c -m$(MCODE) $(CPU_FLAGS) -wx -ox -zc -i=$(INCPATH) -DOFFSET_DISPLAY
AFLAGS    = /c /W3 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(ASM_FPU_FLAGS) /I$(INCPATH) /DOFFSET_DISPLAY
FO_FLAG   = -fo=$@
AS_ARGS   = /Fo$@ $<
LINK_CMD  = $(CC) -m$(MCODE) $(CPU_FLAGS)
FE_FLAG   = -fe=$@ -fm=$(OBJDIR)\$(@B).map
LIB_CMD   = wlib -q -b -n
LIB_END   =
!ELSE
!ERROR Invalid COMPILER="$(COMPILER)". Choose MSC, BCC, or WATCOM.
!ENDIF

# --- 4. Inference rules ---
.SUFFIXES:
.SUFFIXES: .obj .asm .cpp .c

{src\mthread}.asm{$(OBJDIR)}.obj:
	$(AS) $(AFLAGS) $(AS_ARGS)

{src\mthread}.cpp{$(OBJDIR)}.obj:
	$(CC) $(CFLAGS) $(FO_FLAG) $<

{src\video}.asm{$(OBJDIR)}.obj:
	$(AS) $(AFLAGS) $(AS_ARGS)

{src\video}.c{$(OBJDIR)}.obj:
	$(CC) $(CFLAGS) $(FO_FLAG) $<

{src\input}.asm{$(OBJDIR)}.obj:
	$(AS) $(AFLAGS) $(AS_ARGS)

{src\demos\c}.c{$(OBJDIR)}.obj:
	$(CC) $(CFLAGS) $(FO_FLAG) $<

{src\demos\cpp}.cpp{$(OBJDIR)}.obj:
	$(CC) $(CFLAGS) $(FO_FLAG) $<

{$(OBJDIR)}.obj{$(BINDIR)}.$(EXT):
	$(LINK_CMD) $(FE_FLAG) $**

# --- 5. Build targets & dependencies ---
LIBS     = $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib $(LIBDIR)\input.lib
CDEMOS   =   $(BINDIR)\cdemo.$(EXT)   $(BINDIR)\cfpu.$(EXT)   $(BINDIR)\cinput.$(EXT)   $(BINDIR)\csync.$(EXT)
CPPDEMOS = $(BINDIR)\cppdemo.$(EXT) $(BINDIR)\cppfpu.$(EXT) $(BINDIR)\cppinput.$(EXT) $(BINDIR)\cppsync.$(EXT)
DEMOS    = $(CDEMOS) $(CPPDEMOS)

all:      cls dirs libs demos
libs:     $(LIBS)
demos:    cdemos cppdemos
cdemos:   $(CDEMOS)
cppdemos: $(CPPDEMOS)

cls:
	@cls
	@echo Building preemptive multithreading kernel & demos
	@echo Compiler:     $(COMPILER)
	@echo Memory model: $(MODEL)
	@echo Target CPU:   $(CPU)
	@echo FPU support:  $(FPU)
	@echo.

dirs:
	@if not exist $(OBJDIR)\nul mkdir $(OBJDIR)
	@if not exist $(LIBDIR)\nul mkdir $(LIBDIR)
	@if not exist $(BINDIR)\nul mkdir $(BINDIR)

# Object header / include dependencies (compiled via inference rules)

$(OBJDIR)\mthread.obj $(OBJDIR)\videohlp.obj $(OBJDIR)\kbd.obj $(OBJDIR)\mouse.obj:      $(INCPATH)\mthread\mthread.inc
$(OBJDIR)\mthrmgr.obj:                                                                   $(INCPATH)\mthread\mthread.h $(INCPATH)\mthread\mthrmgr.h
$(OBJDIR)\video.obj:                                                                     $(INCPATH)\mthread\video.h
$(OBJDIR)\cdemo.obj $(OBJDIR)\cfpu.obj $(OBJDIR)\cinput.obj $(OBJDIR)\csync.obj:         $(INCPATH)\mthread\mthread.h $(INCPATH)\mthread\video.h
$(OBJDIR)\cppdemo.obj $(OBJDIR)\cppfpu.obj $(OBJDIR)\cppinput.obj $(OBJDIR)\cppsync.obj: $(INCPATH)\mthread\mthread.h $(INCPATH)\mthread\video.h
$(OBJDIR)\cinput.obj $(OBJDIR)\cppinput.obj:                                             $(INCPATH)\mthread\input.h

# Static libraries
$(LIBDIR)\mthread.lib: $(OBJDIR)\mthread.obj $(OBJDIR)\mthrmgr.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ +$(**: = +)$(LIB_END)

$(LIBDIR)\video.lib: $(OBJDIR)\video.obj $(OBJDIR)\videohlp.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ +$(**: = +)$(LIB_END)

$(LIBDIR)\input.lib: $(OBJDIR)\kbd.obj $(OBJDIR)\mouse.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ +$(**: = +)$(LIB_END)

# Demo executables (linked via {$(OBJDIR)}.obj{$(BINDIR)}.$(EXT) inference rule)
$(BINDIR)\cdemo.$(EXT):    $(OBJDIR)\cdemo.obj    $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib
$(BINDIR)\cfpu.$(EXT):     $(OBJDIR)\cfpu.obj     $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib
$(BINDIR)\cinput.$(EXT):   $(OBJDIR)\cinput.obj   $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib $(LIBDIR)\input.lib
$(BINDIR)\csync.$(EXT):    $(OBJDIR)\csync.obj    $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib
$(BINDIR)\cppdemo.$(EXT):  $(OBJDIR)\cppdemo.obj  $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib
$(BINDIR)\cppfpu.$(EXT):   $(OBJDIR)\cppfpu.obj   $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib
$(BINDIR)\cppinput.$(EXT): $(OBJDIR)\cppinput.obj $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib $(LIBDIR)\input.lib
$(BINDIR)\cppsync.$(EXT):  $(OBJDIR)\cppsync.obj  $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib

clean:
	-if exist $(OBJDIR)\*.obj del $(OBJDIR)\*.obj
	-if exist $(OBJDIR)\*.map del $(OBJDIR)\*.map
	-if exist $(LIBDIR)\*.lib del $(LIBDIR)\*.lib
	-if exist $(BINDIR)\*.exe del $(BINDIR)\*.exe
	-if exist $(BINDIR)\*.com del $(BINDIR)\*.com
	-if exist $(BINDIR)\*.map del $(BINDIR)\*.map
	-if exist     mttest.log del     mttest.log
	-if exist $(OBJDIR)\mttest.log del $(OBJDIR)\mttest.log
	-if exist $(LIBDIR)\mttest.log del $(LIBDIR)\mttest.log
	-if exist $(BINDIR)\mttest.log del $(BINDIR)\mttest.log
	-if exist $(OBJDIR)\nul rmdir $(OBJDIR)
	-if exist $(LIBDIR)\nul rmdir $(LIBDIR)
	-if exist $(BINDIR)\nul rmdir $(BINDIR)

.PHONY: all cls dirs clean
