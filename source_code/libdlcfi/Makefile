CC=$(shell $(LLVM_CONFIG) LLVM_BUILD_DIR)/Release+Asserts/bin/clang++
AR=$(shell $(LLVM_CONFIG) LLVM_DIR)/scripts/ar
GOLD_PLUGIN=$(shell $(LLVM_CONFIG) LLVM_BUILD_DIR)/Release+Asserts/lib/LLVMgold.so
GOLD_DIR=$(shell $(LLVM_CONFIG) BINUTILS_BUILD_DIR)/binutils/gold

all:	libdlcfi.so


libdlcfi.so:	dlcfi.o
	$(CC) -shared -B $(GOLD_DIR) -o $@ dlcfi.o -ldl
	

.cpp.o:
	$(CC) -fPIC -g -c $< -o $@

clean:
	rm -f *.a *.o
