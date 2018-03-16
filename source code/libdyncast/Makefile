CC=g++
AR=/usr/bin/ar

all:	libdyncast.a


libdyncast.a:	dynamic_cast.o
	$(AR) q $@ dynamic_cast.o
	

.cpp.o:
	$(CC) -fPIC -c $< -o $@

clean:
	rm -f *.a *.o
