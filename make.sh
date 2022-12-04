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
# cl //fsanitize=address //Zi //nologo //LD //O2 ../wq/wq.c //link //out:wq.dll

# compile DLL -- "wq/wq.c"
if [[ "dbg" == $1 ]]; then
	cl //fsanitize=address //Zi //nologo //LD ../wq/wq.c //link //out:wq.dll
else 
	cl //nologo //Zi //O2 //WX //LD ../wq/wq.c //link //out:wq.dll
fi

# compile host executable -- "main.c"
if [[ "dbg" == $1 ]]; then
	cl //fsanitize=address //Zi //nologo ../main.c
else 
	cl //nologo //Zi //O2 ../main.c
fi

# run that sucker
if [[ "dbg" == $1 ]]; then
	remedybg main.exe
else
	./main.exe
fi
