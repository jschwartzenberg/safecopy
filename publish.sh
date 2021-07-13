#!/bin/sh
#publishing script. prepare source tree
#first make a few checks

echo "checking for files that shouldn't be there"
badlist=""
badlist="${badlist}$( find . -iname Makefile )"
badlist="${badlist}$( find . -iname *.bak )"
badlist="${badlist}$( find . -iname *.o )"
badlist="${badlist}$( find . -type f -iname ".*" )"
[ ! -z "$badlist" ] && { echo $badlist;exit; }

echo "checks ok"
dir=$( echo $PWD |sed -e 's!.*/!!' )
dv=$( echo -n $dir |tail -c 3 )
version=$( cat configure.in |grep AM_INIT |cut -d " " -f 2 |grep -oE "([0-9]|[.])*" )
if [ "$version" != "$dv" ]; then
	echo "version in configure.in ($version)  does not equal directory version ($dv) !"
	exit
fi;

echo "cleaning up CVS directories"
rm -rf $( find . -name CVS )

echo "removing overly large document sources"
rm -rf simulator/doc/*

echo "copying documentation source"
cp webpage/analysis.pdf simulator/doc/

echo "removing webpage"
rm -rf webpage

echo "building tarball for extras"
cd ..
tar -czvhf "${dir}-extras.tar.gz" "$dir/benchmark" "$dir/simulator/doc"
cd "$dir"

echo "remove extras"
rm -rf simulator/doc
rm -rf benchmark

echo "creating configure script..."
./autogen.sh || exit
cd simulator
./autogen.sh || exit
cd ..

echo "deleting cache directories"
rm -rf autom4te.cache
rm -rf simulator/autom4te.cache

echo "deleting myself"
rm publish.sh

echo "building tarball..."
cd ..
tar -czvhf "${dir}.tar.gz" "$dir" || exit

echo "unpacking tarball"
rm -rf "$dir"
tar -zxf "${dir}.tar.gz" || exit

cd "$dir"

echo "building..."
./configure && make || exit
cd simulator
./configure && make || exit
cd ..

echo "testing"
cd test
./test.sh

