#!/bin/sh

basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing test debug library functionality: "

# test if the test libray gets loaded and works. the destination file must be cropped at 2048 byte
# since cat - unlike safecopy - can not read beyond IO errors.
LD_PRELOAD="$safecopydebug" cat debug >"$tmpdir/test.dat" 2>/dev/null

if [ -e "$tmpdir/test.dat" ]; then
	size=$( du -b "$tmpdir/test.dat" | cut -f 1 )
	if [ $size -eq 2048 ]; then
		echo "OK"
		exit 0
	fi
fi
echo "FAIL"
exit 1
