CC=gcc
CFLAGS=-lasound -lrt -lpthread -lm
SRC=src
ODIR=build

capture: $(SRC)/capture.c $(SRC)/common.c
	mkdir -p build
	$(CC) $(SRC)/common.c $(SRC)/capture.c -o $(ODIR)/capture $(CFLAGS)

filter: $(SRC)/filter.c $(SRC)/common.c
	mkdir -p build	
	$(CC) $(SRC)/common.c $(SRC)/filter.c -o $(ODIR)/filter $(CFLAGS)

playback: $(SRC)/playback.c $(SRC)/common.c
	mkdir -p build
	$(CC) $(SRC)/common.c $(SRC)/playback.c -o $(ODIR)/playback $(CFLAGS)

all: capture filter playback 

.PHONY: clean
clean:
	rm -f $(ODIR)/*
