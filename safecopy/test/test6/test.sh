#!/bin/sh
basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing safecopy 6: Incremental recovery: "

# safecopy regular run with badblocks output - produces some reference files
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -r 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" >/dev/null 2>&1

# output file is used as base for incremental runs
cp test1.dat "$tmpdir/test2.dat" >/dev/null 2>&1
cp test1.dat "$tmpdir/test3.dat" >/dev/null 2>&1
cp test1.dat "$tmpdir/test4.dat" >/dev/null 2>&1
cp test1.dat "$tmpdir/test5.dat" >/dev/null 2>&1

# first incremental run with big skipsize. must not overwrite or mark previously sucesfully recovered data in blocks 6 and 7, while correctly reading the data past the end of the last badblock thanks to low resolution
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 8* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" -I "$tmpdir/test1.badblocks" >/dev/null 2>&1

# second incremental run with narrow skipsize. must successfully read the "in between" good block the first run missed, too
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 1* -o "$tmpdir/test3.badblocks" debug "$tmpdir/test3.dat" -I "$tmpdir/test1.badblocks" >/dev/null 2>&1

# same again but with badblock marking. must mark all blocks that remain zero in the previous output files but not more
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 8* -o "$tmpdir/test4.badblocks" debug "$tmpdir/test4.dat" -I "$tmpdir/test1.badblocks" -M "MARKBAAD" >/dev/null 2>&1
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 1* -o "$tmpdir/test5.badblocks" debug "$tmpdir/test5.dat" -I "$tmpdir/test1.badblocks" -M "MARKBAAD" >/dev/null 2>&1
if diff --brief test1.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test3.dat "$tmpdir/test3.dat" >/dev/null 2>&1 &&
   diff --brief test4.dat "$tmpdir/test4.dat" >/dev/null 2>&1 &&
   diff --brief test5.dat "$tmpdir/test5.dat" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test1.badblocks" >/dev/null 2>&1 &&
   diff --brief test2.badblocks "$tmpdir/test2.badblocks" >/dev/null 2>&1 &&
   diff --brief test3.badblocks "$tmpdir/test3.badblocks" >/dev/null 2>&1 &&
   diff --brief test2.badblocks "$tmpdir/test4.badblocks" >/dev/null 2>&1 &&
   diff --brief test3.badblocks "$tmpdir/test5.badblocks" >/dev/null 2>&1; then
		echo "OK"
		exit 0
fi
echo "FAIL"
exit 1
