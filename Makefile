CC = gcc

all: cs345sh

cs345sh: cs345sh.o 
	$(CC) $(CFLAGS) $^ -o $@

%.o:%.c
	$(CC) -o $@ -c $<
	
clean:
	-rm -f cs345sh *.o