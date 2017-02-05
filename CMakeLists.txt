cmake_minimum_required(VERSION 3.6)
project(DALIGNER)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        align.c
        align.h
        daligner.c
        DB.c
        DB.h
        DB2Falcon.c
        filter.c
        filter.h
        HPC.daligner.c
        LA4Falcon.c
        LA4Ice.c
        LAcat.c
        LAcheck.c
        LAdump.c
        LAindex.c
        LAmerge.c
        LAshow.c
        LAsort.c
        LAsplit.c
        QV.c
        QV.h)

add_executable(DALIGNER ${SOURCE_FILES})
