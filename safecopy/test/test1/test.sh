#!/bin/sh

basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing safecopy 1: Simple plain file copy: "

# test without the deug library. safecopy must duplicate the file
# without errors
$safecopy test.dat "$tmpdir/test.dat" >/dev/null 2>&1
if diff --brief test.dat "$tmpdir/test.dat" >/dev/null 2>&1; then
	echo "OK"
	exit 0
fi
echo "FAIL"
exit 1
