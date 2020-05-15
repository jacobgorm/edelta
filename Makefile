LDFLAGS = -lz  
CFLAGS = -Wall -ansi -finline-functions 
edelta: edelta.c sha1.c

clean: 
	rm edelta
