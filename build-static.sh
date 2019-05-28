#!/bin/bash
#
# Static Build Instructions
#  libcgroup is built statically, everything else is still shared
#  
# 
#
# SLES 12sp4
#  GCC 4.8.5 (OS Provided)
#  Req's:
#   glibc-devel-static
#   pam-devel
#   dbus-1-devel
#   libuuid-devel
#
# 
# NOTES:
#  Expect warnings from the linker about statically linking
#  the application due to NSS things that use dlopen() and
#  as such this is best compiled on the OS targetted for
#
#
# Builing libcgroup statically, simple nodes
#
# #!/bin/bash
# 
# module purge -f
# tar xvf ../libcgroup-0.41.tar.bz2
# cd libcgroup-0.41 
# ./configure \
#   --prefix=/usr \
#   --enable-static \
#   --disable-shared \
#   --with-pic
# make
#

C_FILES="oomkiller.c log.c"
CXX_FILES="proc_utils.cpp find_victim.cpp classifier.cpp"

OOM_BINARY="thundercracker"
OOM_CLASSIFY="tc-classify"
OOM_PAM="pam_thundercracker.so"

CC=${CC:-/usr/bin/gcc}
CXX=${CXX:-/usr/bin/g++}

LIBCGROUP_ROOT=${LIBCGROUP_ROOT:-"./libcgroup-0.41"}
LIBCGROUP_INC="${LIBCGROUP_ROOT}/include"
LIBCGROUP_LIB="${LIBCGROUP_ROOT}/src/.libs"
LIBCGROUP_STATIC_LIB="$LIBCGROUP_LIB/libcgroup.a"

DBUS_INC=$(pkg-config --cflags dbus-1)
DBUS_LIB=$(pkg-config --static --libs-only-L dbus-1)
DBUS_LIBS=$(pkg-config --static --libs-only-l dbus-1)

LIBUUID_INC=$(pkg-config --cflags uuid)
LIBUUID_LIBS=$(pkg-config --libs uuid)

CPPFLAGS="-I. -I${LIBCGROUP_INC} -DUSE_SYSTEMD $DBUS_INC"

CFLAGS="-g -fPIC"
CXXFLAGS="-g -fPIC -std=c++11"

LDFLAGS="-g -L${LIBCGROUP_LIB}/src/.libs "
LIBS="$DBUS_LIBS $LIBUUID_LIBS -lm -lpthread"

# Set BASH option to echo commands (verbose)
set -x

# Remove old compiler output Files
rm $OOM_BINARY $OOM_CLASSIFY $OOM_PAM *.o

# Compile C-language portions
for each in $C_FILES
do
  $CC $CPPFLAGS $CFLAGS -c -o ${each%.c}.o $each || exit 1
done

# Compile C++ language portions
for each in $CXX_FILES
do
  $CXX $CPPFLAGS $CXXFLAGS -c -o ${each%.cpp}.o $each || exit 1
done

$CXX $CPPFLAGS $CXXFLAGS -o "$OOM_CLASSIFY" classify.cpp $LIBS || exit 1

# Final Linking (using CXX) - (also see NOTES above)
$CXX -o $OOM_BINARY $LDFLAGS *.o $LIBCGROUP_STATIC_LIB $LIBS || exit 1

# PAM Module - Still shared library, 'cuz PAM
$CC $CPPFLAGS -c -g -fPIC -Wall -Wextra pam_thundercracker.c || exit 1
$CC -shared -o $OOM_PAM pam_thundercracker.o -lpam || exit 1
