OBJS =
LIB_NAMES = libmy1.a libmy2.a

ifeq ($(NO_LTO),OK)
AR = ar
else
AR = llvm-ar
endif

libmy1.a : A.o D.o
	$(AR) rcs $@ $^

libmy2.a : B.o C.o
	$(AR) rcs $@ $^

include ../Makefile.config
include ../Makefile.default
