cmake_minimum_required(VERSION 3.6)
project(DALIGNER)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES
        align.c
        align.h
        daligner.c
        DB.c
        DB.h
        dumpLA.c
        filter.c
        filter.h
        HPC.daligner.c
        LAa2b.c
        LAb2a.c
        LAcat.c
        LAcheck.c
        LAdump.c
        LAmerge.c
        LAshow.c
        LAsort.c
        LAsplit.c
        QV.c
        QV.h)

add_executable(DALIGNER ${SOURCE_FILES})
