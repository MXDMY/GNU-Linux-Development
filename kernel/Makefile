AS := as
LD := ld
CC := gcc

LDFLAG := -m elf_x86_64 -Ttext 0x0 -s --oformat binary

image : linux.img

linux.img : tools/build bootsect setup
	./tools/build bootsect setup > $@

tools/build : tools/build.c
	$(CC) -o $@ $<

bootsect : bootsect.o
	$(LD) $(LDFLAG) -o $@ $<

bootsect.o : bootsect.s
	$(AS) -o $@ $<

setup : setup.o
	$(LD) $(LDFLAG) -e _start_setup -o $@ $<

setup.o : setup.s
	$(AS) -o $@ $<

clean:
	rm -f *.o
	rm -f bootsect
	rm -f setup
	rm -f tools/build
	rm -f linux.img
