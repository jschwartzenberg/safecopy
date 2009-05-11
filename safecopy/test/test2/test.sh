#!/bin/sh

basedir="$1";
tmpdir="$2";

safecopy="$basedir/src/safecopy";
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0";

echo -n " - Testing safecopy 2: Soft recovery: ";

# safecopy - must succesfully read despite the first two read attempts of block 2 will fail
LD_PRELOAD="$safecopydebug" $safecopy -R 3 -b 1024 -f 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" >/dev/null 2>&1;

# safecopy - must create correct output and badblock file when retry limit is too low
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" >/dev/null 2>&1;

# same as above with badblock marking in destination
LD_PRELOAD="$safecopydebug" $safecopy -R 3 -b 1024 -f 4* -o "$tmpdir/test3.badblocks" debug "$tmpdir/test3.dat" >/dev/null 2>&1 -M "MARKBAAD";
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test4.badblocks" debug "$tmpdir/test4.dat" >/dev/null 2>&1 -M "MARKBAAD";

if diff --brief test.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test.dat "$tmpdir/test3.dat" >/dev/null 2>&1 &&
   diff --brief test4.dat "$tmpdir/test4.dat" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test1.badblocks" >/dev/null 2>&1 &&
   diff --brief test2.badblocks "$tmpdir/test2.badblocks" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test3.badblocks" >/dev/null 2>&1 &&
   diff --brief test2.badblocks "$tmpdir/test4.badblocks" >/dev/null 2>&1; then
		echo "OK";
		exit 0;
fi;
echo "FAIL";
exit 1;
