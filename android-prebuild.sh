#!/bin/bash -e



# script local variables
ROOTDIR=$(pwd)
BUILD_ANDROID=$ROOTDIR/.build/android

parsecmd() {
    while [ $# -gt 0 ]; do
        #CMDSTART
        case "$1" in
        -t|--target) #
            # set the directory where all the needed libraries will be installed
            BUILD_ANDROID=${2%/}
            shift
            ;;
        -h|--help) #
            # this help
            echo "Usage: $0 [OPTION]..."
            printf "\nCommand line arguments:\n"
            sed -rn '/CMDSTART/,/CMDEND/{/\) \#|^ +# /{s/\)? #//g;s/^    //;p}}' "$0"
            exit 0
            ;;
        *)
            echo "Unknown parameter $1"
            exit 1
            ;;
        esac
        #CMDEND
        shift
    done
}

parsecmd "$@"

mkdir -p $BUILD_ANDROID/android
mkdir -p $BUILD_ANDROID/extra
mkdir -p $BUILD_ANDROID/extra/usr
mkdir -p $BUILD_ANDROID/extra/usr/include
mkdir -p $BUILD_ANDROID/extra/usr/lib

EXTRAX86=$BUILD_ANDROID/extra_x86
EXTRA=$BUILD_ANDROID/extra/usr

ANDROID_NDK_ROOT=/home/sanne/tmp/target/android/android_standalone
ANDROID_TARGET_ARCH=armeabi-v7a
ANDROID_NDK_TOOLS_PREFIX=arm-linux-androideabi
ANDROID_NDK_TOOLCHAIN_PREFIX=arm-linux-androideabi
ANDROID_ARCHITECTURE=arm
ANDROID_NDK_TOOLCHAIN_VERSION=4.8
ANDROID_NDK_HOST=linux-x86_64
SYSROOT=$ANDROID_NDK_ROOT/sysroot
PKG_CONFIG_LIBDIR=$EXTRA/lib/pkgconfig

export ANDROID_NDK_ROOT ANDROID_TARGET_ARCH ANDROID_NDK_TOOLS_PREFIX ANDROID_NDK_TOOLCHAIN_PREFIX ANDROID_ARCHITECTURE ANDROID_NDK_TOOLCHAIN_VERSION SYSROOT JAVA_HOME PKG_CONFIG_LIBDIR

ANDROID_NDK_TOOLS_FULLPREFIX=$ANDROID_NDK_ROOT/bin/$ANDROID_NDK_TOOLS_PREFIX

CC="$ANDROID_NDK_TOOLS_FULLPREFIX-gcc"
CXX="$ANDROID_NDK_TOOLS_FULLPREFIX-g++"
CPP="$ANDROID_NDK_TOOLS_FULLPREFIX-cpp"
AR="$ANDROID_NDK_TOOLS_FULLPREFIX-ar"
STRIP="$ANDROID_NDK_TOOLS_FULLPREFIX-strip"
RANLIB="$ANDROID_NDK_TOOLS_FULLPREFIX-ranlib"
LD="$ANDROID_NDK_TOOLS_FULLPREFIX-ld"
AS="$ANDROID_NDK_TOOLS_FULLPREFIX-as"


case "$ANDROID_TARGET_ARCH" in
    armeabi-v7a)
        CFLAGS="-g -O2 -ffunction-sections -Wall -funwind-tables -fstack-protector -fno-short-enums -DANDROID -DLITTLE_ENDIAN -Wno-psabi -march=armv7-a -mfloat-abi=softfp -mfpu=vfp -Wa,--noexecstack -nostdlib --sysroot=$SYSROOT"
        ;;
    armeabi)
        CFLAGS="-g -O2 -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -DANDROID -DLITTLE_ENDIAN -Wno-psabi -march=armv5te -mtune=xscale -msoft-float -Wa,--noexecstack -nostdlib"
        ;;
esac

LIBGCC_PATH_FULL=$($CC -mthumb-interwork -print-libgcc-file-name)
LIBGCC_PATH=$(dirname "$LIBGCC_PATH_FULL")

LIBS="-lc -lgcc -L$SYSROOT/usr/lib -I$EXTRA/include -L$EXTRA/lib -I$ANDROID_NDK_ROOT/include/c++/$ANDROID_NDK_TOOLCHAIN_VERSION -L$ANDROID_NDK_ROOT/$ANDROID_NDK_TOOLCHAIN_PREFIX/lib/armv7-a"
CPPFLAGS="--sysroot=$SYSROOT -I$EXTRA/include"
CXXFLAGS="--sysroot=$SYSROOT -I$EXTRA/include"
LDFLAGS="-lc -lgnustl_shared -lOpenSLES -llog"

export CPPFLAGS LIBS CC CXX CPP AR STRIP RANLIB LD AS LDFLAGS LIBS CFLAGS

rm -rf .build
make clean

# portaudio
# the bionic stdlib supports pthread, but in the same way
# no explicit include is necessary and pthread_cancel does not exist
# so we remove the offending lines in the configure script
# and portaudio builds without pthread
sed -i 's/\(.*\)-lpthread\(.*\)/\1\2/' configure
sed -i 's/.*IRIX posix thread library.*/continue/' configure
sed -i 's/\(ac_cv_lib_pthread_pthread_create=\)no/\1yes/g' configure
sed -i 's/\(library_names_spec=.\|soname_spec=.\).{libname}.{.*\(.\)/\1${libname}${shared_ext}\2/' configure
./configure --prefix=$EXTRA --host=arm-linux-androideabi --enable-debug-output --with-opensles
make -j4 install
cp -fv .build/android/extra/usr/lib/libportaudio.* ~/tmp/target/extra/usr/lib/
