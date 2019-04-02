CC=/home/paul/Desktop/llvm/llvm-build/Release+Asserts/bin/clang++
AR=/home/paul/Desktop/llvm/binutils-build/binutils/ar
GOLD_PLUGIN=/home/paul/Desktop/llvm/llvm-build/Release+Asserts/lib/LLVMgold.so
GOLD_DIR=/home/paul/Desktop/llvm/binutils-build/gold

all:	libdlcfi.so


libdlcfi.so:	dlcfi.o
	$(CC) -shared -B $(GOLD_DIR) -o $@ dlcfi.o -ldl
	

.cpp.o:
	$(CC) -fPIC -g -c $< -o $@

clean:
	rm -f *.a *.o
