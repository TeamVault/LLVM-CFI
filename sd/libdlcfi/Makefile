CC=/home/dimo/work/sd3.0/llvm-build/Release+Asserts/bin/clang++
AR=/home/dimo/work/sd3.0/binutils-build/binutils/ar
GOLD_PLUGIN=/home/dimo/work/sd3.0/llvm-rel-build/Release+Asserts/lib/LLVMgold.so
GOLD_DIR=/home/dimo/work/sd3.0/binutils-build/gold

all:	libdlcfi.so


libdlcfi.so:	dlcfi.o
	$(CC) -shared -B $(GOLD_DIR) -o $@ dlcfi.o -ldl
	

.cpp.o:
	$(CC) -fPIC -g -c $< -o $@

clean:
	rm -f *.a *.o
