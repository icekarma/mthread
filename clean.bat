@echo off

if "x%1" == "x" goto error
if "x%2" == "x" goto error
if "x%3" == "x" goto error

if exist %1\*.obj del %1\*.obj
if exist %1\*.lst del %1\*.lst
if exist %1\*.map del %1\*.map
if exist %1\*.cfg del %1\*.cfg
if exist %2\*.lib del %2\*.lib
if exist %3\*.exe del %3\*.exe
if exist %3\*.com del %3\*.com
if exist %3\*.map del %3\*.map
if exist    mttest.log del    mttest.log
if exist %1\mttest.log del %1\mttest.log
if exist %2\mttest.log del %2\mttest.log
if exist %3\mttest.log del %3\mttest.log
if exist %1\nul rmdir %1
if exist %2\nul rmdir %2
if exist %3\nul rmdir %3
goto done

:error
echo Usage: clean.bat OBJDIR LIBDIR BINDIR

:done
