CC := gcc

all: scanfs tpl_frame_transfer
	mv $^ ../build

scanfs: scanfs.c
	$(CC) -o $@ $<

tpl_frame_transfer: tpl_frame_transfer.c
	$(CC) -o $@ $<

clean:
	rm -f *.o
