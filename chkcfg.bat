@echo off

if not exist %1\nul mkdir %1
if not exist %2\nul mkdir %2
if not exist %3\nul mkdir %3

if exist %1\%4.cfg goto done

if exist %1\*.cfg echo Configuration changed to %4 - cleaning stale artifacts...

call clean.bat %1 %2 %3

mkdir %1
mkdir %2
mkdir %3

echo %4 > %1\%4.cfg

:done
