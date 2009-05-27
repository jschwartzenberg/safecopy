#!/bin/sh

basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing safecopy 5: Completely unreadable file: "

# safecopy: unreadable file without marking. result must be an empty file and a complete block list
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" >/dev/null 2>&1

# safecopy: unreadable file with marking. result must be a file of exact source size filled completely with marker data
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" -M "MARKBAAD" >/dev/null 2>&1

if diff --brief test1.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test1.badblocks" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test2.badblocks" >/dev/null 2>&1; then
		echo "OK"
		exit 0
fi
echo "FAIL"
exit 1
