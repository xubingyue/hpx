#!/bin/bash
#
# Copyright (c) 2009-2012 Bryce Adelstein-Lelbach
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file BOOST_LICENSE_1_0.rst or copy at http://www.boost.org/LICENSE_1_0.txt)

usage()
{
    echo "Usage: $0 -d <directory> [args]"
    echo
    echo "This script downloads and performs a minimal production build of HPX on Linux systems. Prereqs:"
    echo "  * g++ is an alias for the GCC C++ Compiler, version 4.6 or newer."
    echo "  * -d <directory> is on a filesystem with 5GB of storage free." 
    echo "  * Bash (>=4.0), GAWK (>=3.8), Wget (>=1.0), GNU Make (>=3.5), Git (>=1.7), Tar (>=1.23) and Python (>=2.6) are available."
    echo
    echo "Options:"
    echo "  -v    Git version or branch [default: master]" 
    echo "  -n    Don't download anything (expects that the -d directory has already been populated)"
    echo "  -t    Number of threads to use while building [default: number of processors]" 
}

DIRECTORY=

VERSION="master"

DOWNLOAD=1

# Physical, not logical cores.
THREADS=`grep -c ^processor /proc/cpuinfo`

###############################################################################
# Argument parsing
while getopts "hnt:d:v:x:" OPTION; do case $OPTION in
    h)
        usage
        exit 0
        ;;
    n)
        DOWNLOAD=0
        ;;
    d)
        DIRECTORY=$OPTARG
        ;;
    v)
        VERSION=$OPTARG
        ;;
    t)
        if [[ $OPTARG =~ ^[0-9]+$ ]]; then 
            THREADS=$OPTARG 
        else
            echo "ERROR: -t argument was invalid"
            usage
            exit 1
        fi
        ;;
    ?)
        usage
        exit 1
        ;;
esac; done

if ! [[ $DIRECTORY ]]; then
    echo "ERROR: No directory specified."
    echo
    usage
    exit 1
fi

if ! [[ $VERSION ]]; then
    echo "ERROR: No version specified."
    echo
    usage
    exit 1
fi

###############################################################################
if ! bash --version > /dev/null 2>&1
then
    echo "ERROR: GNU Bash unavailable."
    echo
    usage
    exit 1 
fi

if ! awk --version > /dev/null 2>&1
then
    echo "ERROR: GNU Awk unavailable."
    echo
    usage
    exit 1 
fi

if ! wget --version > /dev/null 2>&1
then
    echo "ERROR: GNU Wget unavailable."
    echo
    usage
    exit 1 
fi

if ! make --version > /dev/null 2>&1
then
    echo "ERROR: GNU Make unavailable."
    echo
    usage
    exit 1 
fi

if ! git --version > /dev/null 2>&1
then
    echo "ERROR: GNU Bash unavailable."
    echo
    usage
    exit 1 
fi

if ! python --version > /dev/null 2>&1
then
    echo "ERROR: Python unavailable."
    echo
    usage
    exit 1 
fi

if ! g++ -dumpversion > /dev/null 2>&1
then
    echo "ERROR: GCC C++ Compiler unavailable."
    echo
    usage
    exit 1 
fi

if ! python -c "import sys; sys.exit(not $(g++ -dumpversion)>=4.6)" 
then
    echo "ERROR: GCC C++ Compiler ('g++') reported version $(g++ -dumpversion) - 4.6 or higher is needed."
    echo "ERROR: If G++ >=4.6 is available on your system under another name, please place it into your path under the name 'g++'."
    echo
    usage
    exit 1
fi

###############################################################################
mkdir -p $DIRECTORY/install > /dev/null 2>&1

if [[ ! -d $DIRECTORY/install ]]
then
    echo "ERROR: directory $DIRECTORY is not accessible (check permissions).";
    echo
    usage
    exit 1
fi

###############################################################################
separator()
{
    for i in $(seq 1 80)
    do
        echo -n "#" 
    done

    echo
}

separator

echo "Kernel : $(uname -a)"

if [[ -e /etc/redhat-release ]]
then
    echo "OS     : Redhat ($(cat /etc/redhat-release))"
elif [[ -e /etc/debian_version ]];
then
    echo "OS     : Debian ($(cat /etc/debian_version))"
else
    echo "OS     : Unknown"
fi

echo "CPU    : $(cat /proc/cpuinfo | grep 'model name' | head -1 | awk -F ':' {' print $2 '} | cut -c2-)"
echo "G++    : $(g++ --version | head -1)"

###############################################################################

separator

echo "STRATEGY:"
echo "  * Download CMake, Hwloc, Jemalloc tarballs."
echo "  * Checkout HPX from github."
echo "  * Run build_boost.sh from HPX."
echo "  * Bootstrap CMake."
echo "  * Build Hwloc."
echo "  * Build Jemalloc."
echo "  * Configure HPX."
echo "  * Build HPX."

ORIGINAL_DIRECTORY=$PWD
DIRECTORY=$(cd $DIRECTORY; pwd)

error()
{
    cd $ORIGINAL_DIRECTORY
    exit 1
}

###############################################################################

separator
echo "STEP: Download CMake, Hwloc, Jemalloc tarballs."
separator

CMAKE_URL="http://www.cmake.org/files/v2.8"
CMAKE_TARBALL="cmake-2.8.12.2.tar.gz"
CMAKE_DIRECTORY="cmake-2.8.12.2"

HWLOC_URL="http://www.open-mpi.org/software/hwloc/v1.8/downloads"
HWLOC_TARBALL="hwloc-1.8.1.tar.bz2"
HWLOC_DIRECTORY="hwloc-1.8.1"

JEMALLOC_URL="http://www.canonware.com/download/jemalloc"
JEMALLOC_TARBALL="jemalloc-3.6.0.tar.bz2"
JEMALLOC_DIRECTORY="jemalloc-3.6.0"

cd $DIRECTORY

# CMake
if [[ $DOWNLOAD == "1" && ! -e $CMAKE_TARBALL ]]; then
    wget $CMAKE_URL/$CMAKE_TARBALL 
    if ! [[ $? == "0" ]]; then echo "ERROR: Unable to download CMake."; error; fi
fi

echo "Unpacking CMake..."
tar --no-same-owner -xf $CMAKE_TARBALL 
if ! [[ $? == "0" ]]; then echo "ERROR: Unable to unpack `pwd`/${CMAKE_TARBALL}."; error; fi

# Hwloc
if [[ $DOWNLOAD == "1" && ! -e $HWLOC_TARBALL ]]; then
    wget $HWLOC_URL/$HWLOC_TARBALL 
    if ! [[ $? == "0" ]]; then echo "ERROR: Unable to download Hwloc."; error; fi
fi

echo "Unpacking Hwloc..."
tar --no-same-owner -xf $HWLOC_TARBALL 
if ! [[ $? == "0" ]]; then echo "ERROR: Unable to unpack `pwd`/${HWLOC_TARBALL}."; error; fi

# Jemalloc
if [[ $DOWNLOAD == "1" && ! -e $JEMALLOC_TARBALL ]]; then
    wget $JEMALLOC_URL/$JEMALLOC_TARBALL 
    if ! [[ $? == "0" ]]; then echo "ERROR: Unable to download Jemalloc."; error; fi
fi

echo "Unpacking Jemalloc..."
tar --no-same-owner -xf $JEMALLOC_TARBALL 
if ! [[ $? == "0" ]]; then echo "ERROR: Unable to unpack `pwd`/${JEMALLOC_TARBALL}."; error; fi

###############################################################################

separator
echo "STEP: Checkout HPX from github."
separator

cd $DIRECTORY

if [[ $DOWNLOAD == "1" && ! -e hpx ]]; then
    git clone https://github.com/STEllAR-GROUP/hpx
    if ! [[ $? == "0" ]]; then echo "ERROR: Could not clone HPX."; error; fi
fi

cd $DIRECTORY/hpx && git checkout $VERSION 
if ! [[ $? == "0" ]]; then echo "ERROR: Could not checkout version ${VERSION}."; error; fi

###############################################################################

separator
echo "STEP: Run build_boost.sh from HPX."
separator

cd $DIRECTORY

if [[ ! -e $DIRECTORY/hpx/tools/build_boost.sh ]]
then
    echo "ERROR: $DIRECTORY/hpx/tools/build_boost.sh was not found."; error
fi

#if [[ $DOWNLOAD == "1" ]]; then
#    $DIRECTORY/hpx/tools/build_boost.sh -d boost -t $THREADS
#    if ! [[ $? == "0" ]]; then echo "ERROR: build_boost.sh failed."; error; fi
#else
#    $DIRECTORY/hpx/tools/build_boost.sh -d boost -t $THREADS -n
#    if ! [[ $? == "0" ]]; then echo "ERROR: build_boost.sh failed."; error; fi
#fi

###############################################################################

separator
echo "STEP: Bootstrap CMake." 
separator

cd $DIRECTORY/$CMAKE_DIRECTORY

#./bootstrap --prefix=$DIRECTORY/install && make -j $THREADS && make -j $THREADS install
#if ! [[ $? == "0" ]]; then echo "ERROR: Failed to bootstrap CMake."; error; fi

###############################################################################

separator
echo "STEP: Build Hwloc."
separator

cd $DIRECTORY/$HWLOC_DIRECTORY

#./configure --prefix=$DIRECTORY/install && make && make install
#if ! [[ $? == "0" ]]; then echo "ERROR: Failed to bootstrap Hwloc."; error; fi

###############################################################################

separator
echo "STEP: Build Jemalloc"
separator

cd $DIRECTORY/$JEMALLOC_DIRECTORY

#./configure --prefix=$DIRECTORY/install && make && make install
#if ! [[ $? == "0" ]]; then echo "ERROR: Failed to bootstrap Jemalloc."; error; fi

###############################################################################

separator
echo "STEP: Configure HPX"
separator

mkdir -p $DIRECTORY/hpx/bootstrap_build
cd $DIRECTORY/hpx/bootstrap_build

if [[ -e CMakeCache.txt ]]; then
  rm -rf CMakeCache.txt
fi

$DIRECTORY/install/bin/cmake                        \
    -DCMAKE_CXX_COMPILER="g++"                      \
    -DCMAKE_BUILD_TYPE=Release                      \
    -DHPX_THREAD_GUARD_PAGE=OFF                     \
    -DBOOST_ROOT="$DIRECTORY/boost/release"         \
    -DJEMALLOC_ROOT="$DIRECTORY/install"            \
    -DHWLOC_ROOT="$DIRECTORY/install"               \
    -DHPX_MALLOC="jemalloc"                         \
    -DCMAKE_INSTALL_PREFIX="$DIRECTORY/install" $DIRECTORY/hpx
if ! [[ $? == "0" ]]; then echo "ERROR: Failed to configure HPX."; error; fi

###############################################################################

separator
echo "STEP: Build HPX"
separator

cd $DIRECTORY/hpx/bootstrap_build

make -j $THREADS && make -j $THREADS install
if ! [[ $? == "0" ]]; then echo "ERROR: Failed to build HPX."; error; fi

###############################################################################

separator
echo "Successfully built HPX"
echo
echo "  HPX_LOCATION=$DIRECTORY/install"
echo
separator

