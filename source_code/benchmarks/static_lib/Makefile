OBJS =
LIB_NAMES = libmy1.a libmy2.a

include ../Makefile.config
include ../Makefile.default

libmy1.a : A.o D.o
	@$(AR) rcsT $@ $^

libmy2.a : B.o C.o
	@$(AR) rcsT $@ $^
