#!/usr/bin/env bash
# Builds the PS3 PPU toolchain (binutils 2.22 + gcc 7.2.0 + newlib 1.20.0,
# the same versions as the owner's WSL toolchain), PSL1GHT and zlib/libpng
# into /usr/local/ps3dev, for Claude Code cloud sessions (Ubuntu 24.04).
#
# The GNU/sourceware tarball hosts are blocked there, so every source comes
# from a git mirror on GitHub. Takes ~25 min on 4 vCPU.
#
#   bash tools/setup-cloud-toolchain.sh
set -eo pipefail

export PS3DEV=/usr/local/ps3dev PSL1GHT=/usr/local/ps3dev
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
S=${S:-/opt/ps3src}
J=$(nproc)

if [ -x $PS3DEV/ppu/bin/ppu-gcc ] && [ -x $PS3DEV/bin/sprxlinker ]; then
  echo "toolchain already in $PS3DEV"; exit 0
fi

DEBIAN_FRONTEND=noninteractive apt-get install -y -q flex bison texinfo libgmp-dev \
  libmpfr-dev libmpc-dev libelf-dev zlib1g-dev libssl-dev autoconf automake libtool pkg-config

mkdir -p $S $PS3DEV && cd $S
clone() { [ -d "$2" ] || git clone -q --depth 1 ${3:+--branch $3} "https://github.com/$1.git" "$2"; }
clone Distrotech/binutils binutils-2.22 binutils-2_22
clone gcc-mirror/gcc gcc-7.2.0 releases/gcc-7.2.0
clone mirror/newlib-cygwin newlib-1.20.0 newlib-1_20_0
clone ps3dev/PSL1GHT PSL1GHT
clone ps3dev/ps3libraries ps3libraries
clone madler/zlib zlib-1.2.13 v1.2.13
clone glennrp/libpng libpng-1.4.4 v1.4.4
if [ ! -d ps3toolchain ]; then
  git clone -q https://github.com/ps3dev/ps3toolchain.git
  # last revision with gcc 7.2.0 (the next one moved the PPU to gcc 13)
  git -C ps3toolchain checkout -q 1087072^
fi
P=$S/ps3toolchain/patches

if [ ! -f binutils-2.22/.patched ]; then
  patch -s -p1 -d binutils-2.22 < $P/binutils-2.22-PS3.patch
  cp /usr/share/misc/config.guess /usr/share/misc/config.sub binutils-2.22/ 2>/dev/null || true
  touch binutils-2.22/.patched
fi
if [ ! -f gcc-7.2.0/.patched ]; then
  patch -s -p1 -d gcc-7.2.0 < $P/gcc-7.2.0-PS3.patch
  patch -s -p1 -d newlib-1.20.0 < $P/newlib-1.20.0-PS3.patch
  ln -sf ../newlib-1.20.0/newlib gcc-7.2.0/newlib
  ln -sf ../newlib-1.20.0/libgloss gcc-7.2.0/libgloss
  touch gcc-7.2.0/.patched
fi

# binutils
mkdir -p binutils-2.22/build-ppu && cd binutils-2.22/build-ppu
CFLAGS="-O2 -w -fcommon" ../configure --prefix=$PS3DEV/ppu --target=powerpc64-ps3-elf \
  --disable-nls --disable-shared --disable-debug --disable-dependency-tracking --disable-werror \
  --enable-64-bit-bfd --with-gcc --with-gnu-as --with-gnu-ld
make -j$J MAKEINFO=true && make MAKEINFO=true libdir=host-libs/lib MULTIOSDIR=. install
cd $S

# gcc + newlib (host gcc 13 needs the old C++ dialect for gcc 7 sources)
mkdir -p gcc-7.2.0/build-ppu && cd gcc-7.2.0/build-ppu
CFLAGS="-O2 -w -fcommon -Wno-int-conversion" CXXFLAGS="-O2 -w -std=gnu++98 -fpermissive" \
../configure --prefix=$PS3DEV/ppu --target=powerpc64-ps3-elf \
  --disable-dependency-tracking --disable-libcc1 --disable-libstdcxx-pch --disable-multilib \
  --disable-nls --disable-shared --disable-win32-registry --enable-languages=c,c++ \
  --enable-long-double-128 --enable-lto --enable-threads --with-cpu=cell --with-newlib \
  --enable-newlib-multithread --enable-newlib-hw-fp --with-system-zlib
make -j$J MAKEINFO=true all && make MAKEINFO=true MULTIOSDIR=. install
cd $PS3DEV/ppu/bin
for i in $(ls powerpc64-ps3-elf-* | cut -c19-); do [ -e ppu-$i ] || ln -s powerpc64-ps3-elf-$i ppu-$i; done
cd $S

# PSL1GHT: PPU libraries + host tools (no SPU toolchain needed)
cd PSL1GHT && make install-ctrl && make -C ppu -j$J && make -C ppu install \
  && make -C tools -j$J && make -C tools install && cd $S

# zlib + libpng portlibs
cd zlib-1.2.13 && (patch -s -N -p1 < ../ps3libraries/patches/zlib-1.2.13-PPU.patch || true)
AR=powerpc64-ps3-elf-ar CC=powerpc64-ps3-elf-gcc RANLIB=powerpc64-ps3-elf-ranlib \
  ./configure --prefix=$PS3DEV/portlibs/ppu --static
make -j$J AR=powerpc64-ps3-elf-ar ARFLAGS=rc RANLIB=powerpc64-ps3-elf-ranlib
make install AR=powerpc64-ps3-elf-ar ARFLAGS=rc RANLIB=powerpc64-ps3-elf-ranlib
cd $S/libpng-1.4.4 && cp /usr/share/misc/config.guess /usr/share/misc/config.sub . 2>/dev/null || true
mkdir -p build-ppu && cd build-ppu
CFLAGS="-I$PSL1GHT/ppu/include -I$PS3DEV/portlibs/ppu/include" \
CPPFLAGS="-I$PSL1GHT/ppu/include -I$PS3DEV/portlibs/ppu/include" \
LDFLAGS="-L$PSL1GHT/ppu/lib -L$PS3DEV/portlibs/ppu/lib -lrt -llv2" \
  ../configure --prefix=$PS3DEV/portlibs/ppu --host=powerpc64-ps3-elf --enable-static --disable-shared
make -j$J && make install

ppu-gcc --version | head -1
echo "toolchain ready in $PS3DEV"
