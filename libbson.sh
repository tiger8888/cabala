#! /bin/sh

[ -f ./deps/libbson/.libs/libbson.a ] && exit 0

if [ ! -d deps ]; then
    mkdir deps
fi
cd deps

git clone https://github.com/mongodb/libbson
cd libbson

./autogen.sh
make