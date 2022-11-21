gcc host.c -ldl && gcc dl.c --shared -o dl.so && ./a.out
