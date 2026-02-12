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
    "test")
        cd ./build || exit 36
        cmake ../test/
        # shellcheck disable=SC2154
        make -j"${nproc}"
        ;;
    "tools")
        cd ./tools || exit 36
        make -j"${nproc}"
        make clean
        ;;
    "x86asm_att")
        cd ./x86asm_att || exit 36
        as -32 -g -o start.o start.s
        ld -m elf_i386 -o start start.o
        mv ./start ../build
        rm -f start.o
        printf "Built target start\n"
        ;;
    *)
        printf "Usages: ./build.sh [driver|kernel|test|tools|x86asm_att]\n"
        ;;
esac
printf "Execution end\n"
