#!/bin/bash
as -32 -g -o start.o start.s
ld -m elf_i386 -o start start.o
./start r.txt w.txt 
echo -e "\n\nExit Status Code:$?"
rm -f start.o
