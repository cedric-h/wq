set -e

rm -rf build
mkdir -p build
cd build

source ~/msvc/setup.sh

# for static build
# cl //Zi //c ../wq/wq.c
# cl //Zi ../main.c wq.obj

# tcc -shared -o wq.dll ../wq/wq.c 
cl //Zi //LD //O2 -o wq.dll ../wq/wq.c

# dynamic build
cl //Zi ../main.c
./main.exe
