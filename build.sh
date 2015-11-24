#!/bin/bash

#Sample build script
#You'll likely need to customize this for your site
#real build system to come later

rm *.o a.out
clang -g -I. -c oomkiller.c
clang -g -I. -c log.c
clang++ -g -I. -std=c++11 -c find_victim.cpp
clang++ -g *.o -l cgroup
