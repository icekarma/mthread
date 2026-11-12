#=============================================================================
# Makefile - MS-DOS Preemptive Multithreading Kernel & Demos (NMAKE)
#
# Usage Examples:
#   nmake                                  (Defaults: MSC, SMALL, 8086, FPU=NONE)
#   nmake MODEL=TINY                       (MSC 7.00, tiny model .COM files)
#   nmake MODEL=HUGE CPU=186               (MSC 7.00, huge model, 80186+)
#   nmake MODEL=LARGE CPU=186 FPU=ALL      (MSC 7.00, large model, 186+, FPU all threads)
#   nmake FPU=OPTIN                        (MSC 7.00, per-thread FPU opt-in)
#   nmake COMPILER=BCC MODEL=COMPACT       (Borland C++ 3.1 + TASM 4.1)
#   nmake COMPILER=WATCOM MODEL=SMALL      (OpenWatcom C/C++ + MASM 6.11)
#   nmake clean
#=============================================================================

# --- User-configurable build options ---
COMPILER  = MSC
MODEL     = SMALL
CPU       = 8086
FPU       = NONE

OBJDIR    = obj
BINDIR    = bin
LIBDIR    = lib
INCPATH   = include

# --- 1. Map memory model to compiler short code (MCODE) & extension (EXT) ---
MCODE     = [$(MODEL)]
!IF "$(COMPILER)" == "MSC"
MCODE     = $(MCODE:[TINY]=T)
MCODE     = $(MCODE:[SMALL]=S)
MCODE     = $(MCODE:[MEDIUM]=M)
MCODE     = $(MCODE:[COMPACT]=C)
MCODE     = $(MCODE:[LARGE]=L)
MCODE     = $(MCODE:[HUGE]=H)
!ELSE
MCODE     = $(MCODE:[TINY]=t)
MCODE     = $(MCODE:[SMALL]=s)
MCODE     = $(MCODE:[MEDIUM]=m)
MCODE     = $(MCODE:[COMPACT]=c)
MCODE     = $(MCODE:[LARGE]=l)
MCODE     = $(MCODE:[HUGE]=h)
!ENDIF
!IF "$(MCODE)" == "[$(MODEL)]"
!ERROR Invalid MODEL="$(MODEL)". Choose TINY, SMALL, MEDIUM, COMPACT, LARGE, or HUGE.
!ENDIF

!IF "$(MODEL)" == "TINY"
EXT       = com
!ELSE
EXT       = exe
!ENDIF

# --- 2a. Validate CPU choice (8086 vs 80186) ---
!IF ( "$(CPU)" == "80186" ) || ( "$(CPU)" == "80188" ) || ( "$(CPU)" == "186" ) || ( "$(CPU)" == "188" )
CPU=186
ASM_CPU_FLAGS = /DUSE_186
!ELSEIF ( "$(CPU)" == "8086" ) || ( "$(CPU)" == "8088" ) || ( "$(CPU)" == "86" ) || ( "$(CPU)" == "88" )
CPU=86
ASM_CPU_FLAGS =
!ELSE
!ERROR Invalid CPU="$(CPU)". Choose 8086/8088/86/88 or 80186/80188/186/188.
!ENDIF

# --- 2b. Validate FPU choice (NONE, OPTIN, ALL) ---
!IF "$(FPU)" == "NONE"
FPU_FLAGS =
FCODE     = N
!ELSEIF "$(FPU)" == "OPTIN"
FPU_FLAGS = /DUSE_FPU
FCODE     = O
!ELSEIF "$(FPU)" == "ALL"
FPU_FLAGS = /DUSE_FPU /DFPU_DEFAULT_ALL
FCODE     = A
!ELSE
!ERROR Invalid FPU="$(FPU)". Choose NONE, OPTIN, or ALL.
!ENDIF

# --- 3. Configure toolchain commands & flags ---
!IF "$(COMPILER)" == "MSC"
CCODE     = M
!IF "$(MODEL)" == "TINY"
MSC_DBG   =
!ELSE
MSC_DBG   = /Zi
!ENDIF
CPU_FLAGS = $(CPU:186=/G1)
CPU_FLAGS = $(CPU_FLAGS:86=/G0)
CC        = cl
AS        = ml
CFLAGS    = /c /A$(MCODE) $(CPU_FLAGS) $(FPU_FLAGS) /Gy /Ox /W3 $(MSC_DBG) /I$(INCPATH) /DOFFSET_DISPLAY /nologo
AFLAGS    = /c /W2 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(FPU_FLAGS) $(MSC_DBG) /I$(INCPATH) /DOFFSET_DISPLAY /nologo /Sg /Fl$(OBJDIR)\$(@B).lst
FO_FLAG   = /Fo$@
AS_ARGS   = /Fo$@ $<
LINK_CMD  = $(CC) /A$(MCODE) $(CPU_FLAGS) /nologo
FE_FLAG   = /Fe$@ /Fm$(OBJDIR)\$(@B).map
LIB_CMD   = lib /nologo
LIB_END   = ;
!ELSEIF "$(COMPILER)" == "BCC"
CCODE     = B
CPU_FLAGS = $(CPU:186=-1)
CPU_FLAGS = $(CPU_FLAGS:86=-1-)
CC        = bcc
AS        = tasm
CFLAGS    = -c -m$(MCODE) $(CPU_FLAGS) $(FPU_FLAGS) -w -O2 -N- -I$(INCPATH) -DOFFSET_DISPLAY
AFLAGS    = /m2 /mx /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(FPU_FLAGS) /I$(INCPATH) /DOFFSET_DISPLAY
FO_FLAG   = -o$@
AS_ARGS   = $<, $@
LINK_CMD  = $(CC) -m$(MCODE) $(CPU_FLAGS)
FE_FLAG   = -e$@
LIB_CMD   = tlib
LIB_END   =
!ELSEIF "$(COMPILER)" == "WATCOM"
CCODE     = W
CPU_FLAGS = $(CPU:186=-1)
CPU_FLAGS = $(CPU_FLAGS:86=-0)
CC        = wcl
AS        = ml
CFLAGS    = -c -m$(MCODE) $(CPU_FLAGS) $(FPU_FLAGS) -wx -ox -zc -i=$(INCPATH) -DOFFSET_DISPLAY
AFLAGS    = /c /W3 /DMODEL_NAME=$(MODEL) $(ASM_CPU_FLAGS) $(FPU_FLAGS) /I$(INCPATH) /DOFFSET_DISPLAY
FO_FLAG   = -fo=$@
AS_ARGS   = /Fo$@ $<
LINK_CMD  = $(CC) -m$(MCODE) $(CPU_FLAGS)
FE_FLAG   = -fe=$@ -fm=$(OBJDIR)\$(@B).map
LIB_CMD   = wlib -q -b -n
LIB_END   =
!ELSE
!ERROR Invalid COMPILER="$(COMPILER)". Choose MSC, BCC, or WATCOM.
!ENDIF

# 8.3-safe build configuration stamp (e.g., obj\SNM86.cfg)
STAMP     = $(MCODE)$(FCODE)$(CCODE)$(CPU)

# --- 4. Inference rules ---
.SUFFIXES:
.SUFFIXES: .obj .asm .cpp .c

{src\mthread}.asm{$(OBJDIR)}.obj:
	$(AS) @<<
$(AFLAGS) $(AS_ARGS)
<<

{src\mthread}.cpp{$(OBJDIR)}.obj:
	$(CC) @<<
$(CFLAGS) $(FO_FLAG) $<
<<

{src\video}.asm{$(OBJDIR)}.obj:
	$(AS) @<<
$(AFLAGS) $(AS_ARGS)
<<

{src\video}.c{$(OBJDIR)}.obj:
	$(CC) @<<
$(CFLAGS) $(FO_FLAG) $<
<<

{src\input}.asm{$(OBJDIR)}.obj:
	$(AS) @<<
$(AFLAGS) $(AS_ARGS)
<<

{src\demos\c}.c{$(OBJDIR)}.obj:
	$(CC) @<<
$(CFLAGS) $(FO_FLAG) $<
<<

{src\demos\cpp}.cpp{$(OBJDIR)}.obj:
	$(CC) @<<
$(CFLAGS) $(FO_FLAG) $<
<<

{$(OBJDIR)}.obj{$(BINDIR)}.$(EXT):
	$(LINK_CMD) @<<
$(FE_FLAG) $**
<<

# --- 5. Build targets & dependencies ---
LIBS      = $(LIBDIR)\mthread.lib $(LIBDIR)\video.lib $(LIBDIR)\input.lib
CDEMOS    =   $(BINDIR)\cdemo.$(EXT)   $(BINDIR)\cfpu.$(EXT)   $(BINDIR)\cinput.$(EXT)   $(BINDIR)\csync.$(EXT)
CPPDEMOS  = $(BINDIR)\cppdemo.$(EXT) $(BINDIR)\cppfpu.$(EXT) $(BINDIR)\cppinput.$(EXT) $(BINDIR)\cppsync.$(EXT)
DEMOS     = $(CDEMOS) $(CPPDEMOS)

all:      cls dirs libs demos
libs:     dirs $(LIBS)
demos:    cdemos cppdemos
cdemos:   dirs $(CDEMOS)
cppdemos: dirs $(CPPDEMOS)

cls:
	@cls
	@echo Building preemptive multithreading kernel & demos
	@echo Compiler:     $(COMPILER)
	@echo Memory model: $(MODEL)
	@echo Target CPU:   80$(CPU)
	@echo FPU support:  $(FPU)
	@echo.

dirs:
	@chkcfg.bat $(OBJDIR) $(LIBDIR) $(BINDIR) $(STAMP)

# Object header / include dependencies (compiled via inference rules)
CORE_HDRS  = $(INCPATH)\mthread\_defs.h $(INCPATH)\mthread\mthread.h $(INCPATH)\mthread\thread.h $(INCPATH)\mthread\synchron.h $(INCPATH)\mthread\atomic.h $(INCPATH)\mthread\msg.h
CPP_HDRS   = $(INCPATH)\mthread\mthrd.h $(INCPATH)\mthread\mthrmgr.h $(INCPATH)\mthread\mevent.h $(INCPATH)\mthread\mmutex.h $(INCPATH)\mthread\msem.h $(INCPATH)\mthread\mcondvar.h $(INCPATH)\mthread\mlkguard.h
INPUT_HDRS = $(INCPATH)\mthread\input.h $(INCPATH)\mthread\kbd.h $(INCPATH)\mthread\mouse.h $(INCPATH)\mthread\mkbd.h $(INCPATH)\mthread\mmouse.h

$(OBJDIR)\mthread.obj $(OBJDIR)\videohlp.obj $(OBJDIR)\kbd.obj $(OBJDIR)\mouse.obj:      $(INCPATH)\mthread\mthread.inc
$(OBJDIR)\mthrmgr.obj:                                                                   $(CORE_HDRS) $(CPP_HDRS)
$(OBJDIR)\video.obj:                                                                     $(INCPATH)\mthread\_defs.h $(INCPATH)\mthread\video.h
$(OBJDIR)\cdemo.obj   $(OBJDIR)\cfpu.obj   $(OBJDIR)\cinput.obj   $(OBJDIR)\csync.obj:   $(CORE_HDRS) $(INCPATH)\mthread\video.h
$(OBJDIR)\cppdemo.obj $(OBJDIR)\cppfpu.obj $(OBJDIR)\cppinput.obj $(OBJDIR)\cppsync.obj: $(CORE_HDRS) $(CPP_HDRS) $(INCPATH)\mthread\video.h
$(OBJDIR)\cinput.obj $(OBJDIR)\cppinput.obj:                                             $(INPUT_HDRS)

# Static libraries
$(LIBDIR)\mthread.lib: $(OBJDIR)\mthread.obj $(OBJDIR)\mthrmgr.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ @<<
+$(**: = +)$(LIB_END)
<<

$(LIBDIR)\video.lib: $(OBJDIR)\video.obj $(OBJDIR)\videohlp.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ @<<
+$(**: = +)$(LIB_END)
<<

$(LIBDIR)\input.lib: $(OBJDIR)\kbd.obj $(OBJDIR)\mouse.obj
	-if exist $@ del $@
	$(LIB_CMD) $@ @<<
+$(**: = +)$(LIB_END)
<<

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
	-clean.bat $(OBJDIR) $(LIBDIR) $(BINDIR)

.PHONY: all cls dirs clean demos
