
C = gcc
CFLAGS = -Wall -O2 -lm

OFILES = Allocator.o

.SUFFIXES: .o .cpp

link: $(OFILES)
	$(C) $(CFLAGS) $^

clean:
	rm -rf *~ *.o
