#!/bin/sh
export CFLAGS="-O3 -static-libgcc -flto -fno-use-linker-plugin \
-march=pentium-m"

export CXXFLAGS="-O3 -static-libgcc -static-libstdc++ -flto -fno-use-linker-plugin \
-march=pentium-m"

cd vs2015/sdl
if test -f configure; then
	configure --prefix=/mingw --disable-shared --enable-static
else
	bash autogen.sh
	configure --prefix=/mingw --disable-shared --enable-static
fi

make -j4
make install

cd ../../
if test -f configure; then
	configure
else
	bash autogen.sh
	configure
fi
make -j4
cd src
strip dosbox-x.exe
mv dosbox-x.exe ../dosbox-x-eeepc.exe
cd ..