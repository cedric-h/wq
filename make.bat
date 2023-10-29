rmdir /s /q build
mkdir build
cd build

REM %USERPROFILE%\msvc\setup.bat

cl /nologo /Zi /O2 /WX /LD ../wq/wq.c /link /out:wq.dll
cl /nologo /Zi /O2 ../main.c

cd ..

echo off

REM for static build
REM cl /Zi /c ../wq/wq.c
REM cl /Zi ../main.c wq.obj

REM tcc -shared -o wq.dll ../wq/wq.c 
REM tcc -shared -DCUSTOM_MATH=1 -o wq.dll ../wq/wq.c
REM cl /fsanitize=address /Zi /nologo /LD /O2 ../wq/wq.c /link /out:wq.dll

REM compile DLL -- "wq/wq.c"
REM  [[ "dbg" == $1 ]]; then
REM se 
REM 

REM cl /nologo /Zi /fsanitize=address /LD ../wq/wq.c /link /out:wq.dll
REM cl /nologo /Zi /fsanitize=address ../main.c
REM se 
REM 

REM # run that sucker
REM if [[ "dbg" == $1 ]]; then
REM remedybg main.exe
REM else
REM ./main.exe
REM fi
