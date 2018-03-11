#!/bin/sh
export CFLAGS="-O2 -static-libgcc -march=i586"

export CXXFLAGS="-O2 -static-libgcc -static-libstdc++ -march=i586"

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
	configure --disable-mt32
else
	bash autogen.sh
	configure --disable-mt32
fi
make -j4
cd src
strip dosbox-x.exe
mv dosbox-x.exe ../dosbox-x-slo.exe
cd ..