#!/bin/sh
basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing safecopy 10: Incremental recovery with blocksize variation and exclude blocks "

# main emphasis on this test is that no data from exclude blocks are ever read nor written.

# safecopy regular run with badblocks output - produces some reference files with and withouzt marked badblocks
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1150 -f 4* -r 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" -x 490 -X xblocks >/dev/null 2>&1
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1150 -f 4* -r 4* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" -x 490 -X xblocks -M "MARKBAAD" >/dev/null 2>&1

# output file is used as base for incremental runs
cp test2.dat "$tmpdir/test3.dat" >/dev/null 2>&1
cp test2.dat "$tmpdir/test4.dat" >/dev/null 2>&1
cp test2.dat "$tmpdir/test5.dat" >/dev/null 2>&1
cp test2.dat "$tmpdir/test6.dat" >/dev/null 2>&1

# first incremental run with big skipsize. Must recover the missing data at the end of the affected areas. Must transpose the badblock list to new sector sizes
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 8* -o "$tmpdir/test3.badblocks" debug "$tmpdir/test3.dat" -i 1150 -I "$tmpdir/test1.badblocks" -x 490 -X xblocks >/dev/null 2>&1

# second incremental run with narrow skipsize. must successfully read ALL the "in between" data too
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 1* -r 1 -o "$tmpdir/test4.badblocks" debug "$tmpdir/test4.dat" -i 1150 -I "$tmpdir/test1.badblocks" -x 490 -X xblocks >/dev/null 2>&1

# same again but with badblock marking. this one may overwrite successfully rescued data as long as it affects only blocks marked as bad in the include file
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 8* -o "$tmpdir/test5.badblocks" debug "$tmpdir/test5.dat" -i 1150 -I "$tmpdir/test1.badblocks" -x 490 -X xblocks -M "MARKBAAD" >/dev/null 2>&1
# this one must only differ from test4.dat in internal shifts of marker strings, not in data
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 1* -r 1 -o "$tmpdir/test6.badblocks" debug "$tmpdir/test6.dat" -i 1150 -I "$tmpdir/test1.badblocks" -x 490 -X xblocks -M "MARKBAAD" >/dev/null 2>&1

if diff --brief test1.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test3.dat "$tmpdir/test3.dat" >/dev/null 2>&1 &&
   diff --brief test4.dat "$tmpdir/test4.dat" >/dev/null 2>&1 &&
   diff --brief test5.dat "$tmpdir/test5.dat" >/dev/null 2>&1 &&
   diff --brief test6.dat "$tmpdir/test6.dat" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test1.badblocks" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test2.badblocks" >/dev/null 2>&1 &&
   diff --brief test3.badblocks "$tmpdir/test3.badblocks" >/dev/null 2>&1 &&
   diff --brief test4.badblocks "$tmpdir/test4.badblocks" >/dev/null 2>&1 &&
   diff --brief test3.badblocks "$tmpdir/test5.badblocks" >/dev/null 2>&1 &&
   diff --brief test4.badblocks "$tmpdir/test6.badblocks" >/dev/null 2>&1; then
		echo "OK"
		exit 0
fi
echo "FAIL"
exit 1
