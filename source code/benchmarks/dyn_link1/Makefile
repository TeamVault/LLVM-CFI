OBJS=classes.o
include ../Makefile.config

LIBS = $(patsubst lib%.a,-l%,$(LIB_NAMES))
OPT  = -O2

ifeq ($(NO_LTO),OK)
CC      = g++
LD      = $(CC)
CFLAGS  = $(OPT) -g
LDFLAGS =
AR      = ar
else
CC      = $(LLVM_BUILD_DIR)/clang++
LD      = $(CC)
CFLAGS  = $(OPT) -flto -femit-ivtbl -femit-vtbl-checks
LDFLAGS = $(OPT) -B $(BINUTILS_BUILD_DIR)/gold \
		  -Wl,-plugin $(LLVM_BUILD_DIR)/../lib/LLVMgold.so \
		  -Wl,-plugin-opt=mcpu=x86-64 \
		  -Wl,-plugin-opt=save-temps \
		  -Wl,-plugin-opt=sd-ovtbl
LDLIBS  = -L$(LLVM_DIR)/../lib -ldyncast -lctr -ldlcfi
AR      = $(LLVM_DIR)/scripts/ar
endif

ALL_OBJS = $(OBJS) main.o

all:	libT.so main

main :	$(ALL_OBJS) $(LIB_NAMES)
		$(LD) $(LDFLAGS) -o main -L. $^ $(LDLIBS) $(LIBS) -lT -Wl,-rpath,. -Wl,-rpath $(LLVM_DIR)/../lib

libT.so : lib.o classes.o
		$(LD) $(LDFLAGS) -o libT.so -shared $^ $(LIBS)

%.o: 	%.cpp
		$(CC) -fPIC -c $(CFLAGS) $< -o $@

clean:
		@rm -f *.o *.a main

clean-all: clean
		@rm -f *.bc *.ll output.txt
