#!/bin/bash

if [ ! -d "./build" ]; then
    mkdir ./build
else
    rm -rf ./build
    mkdir ./build
fi

printf "Execution options: %s\n" "$1"
case "$1" in
    "driver")
        printf "No executable projects available\n"
        ;;
    "kernel")
        printf "No executable projects available\n"
        ;;
    "unsafety")
        cd ./build || exit 1
        cmake ../unsafety/
        # shellcheck disable=SC2154
        make -j"${nproc}"
        ;;
    "tools")
        cd ./tools || exit 1
        make -j"${nproc}"
        make clean
        ;;
    "asm")
        cd ./asm || exit 1
        as -32 -g -o start.o start.s
        ld -m elf_i386 -o start start.o
        mv ./start ../build
        rm -f start.o
        printf "Built target start\n"
        ;;
    *)
        printf "Usages: ./build.sh [driver|kernel|unsafety|tools|asm]\n"
        ;;
esac
printf "Execution end\n"
