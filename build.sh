#!/bin/bash

#Sample build script
#You'll likely need to customize this for your site
#real build system to come later

rm *.o a.out
gcc -g -I. -c oomkiller.c
gcc -g -I. -c log.c
g++ -g -I. -std=c++11 -c proc_utils.cpp
g++ -g -I. -std=c++11 -c find_victim.cpp
#g++ -g -I. -std=c++11 -DUSE_SYSTEMD classifier.cpp `pkg-config --cflags --libs dbus-1` log.o proc_utils.o -o classifierd -luuid
g++ -g -I. -std=c++11 -DUSE_SYSTEMD  `pkg-config --cflags dbus-1` -c classifier.cpp
g++ -g *.o `pkg-config --cflags --libs dbus-1` -l cgroup -luuid
g++ -g -I. -std=c++11 classify.cpp -o classify
