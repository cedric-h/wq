set -e

rm -rf build
mkdir -p build
cd build

source ~/msvc/setup.sh

# for static build
# cl //Zi //c ../wq/wq.c
# cl //Zi ../main.c wq.obj

# tcc -shared -o wq.dll ../wq/wq.c 
# tcc -shared -DCUSTOM_MATH=1 -o wq.dll ../wq/wq.c
cl //nologo //Zi //LD //O2 ../wq/wq.c //link //out:wq.dll

# dynamic build
cl //nologo //Zi ../main.c
./main.exe
