rmdir /s /q build
mkdir build
cd build

REM %USERPROFILE%\msvc\setup.bat

clang ../wq/wq.c -O2 -Wall --shared -o wq.dll
clang ../main.c  -O2 -Wall          -o main.exe

cd ..

echo off
