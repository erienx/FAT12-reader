cmake_minimum_required(VERSION 3.26)
project(FAT_reader C)

set(CMAKE_C_STANDARD 11)

add_executable(FAT_reader
        file_reader.c
        file_reader.h
        main.c)
