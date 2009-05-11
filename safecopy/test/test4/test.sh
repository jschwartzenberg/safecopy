#!/bin/sh

basedir="$1";
tmpdir="$2";

safecopy="$basedir/src/safecopy";
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0";

echo -n " - Testing safecopy 4: Unreadable end of file: ";

# safecopy writes a shortened output file since the last blocks are unreadable. The badblocks output must state these short read blocks
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" >/dev/null 2>&1;

# same as above but safecopy must recover the soft error
LD_PRELOAD="$safecopydebug" $safecopy -R 3 -b 1024 -f 4* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" >/dev/null 2>&1;

# same as above but with output marking. the output file needs to have the correct size with all bad data marked correctly
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test3.badblocks" debug "$tmpdir/test3.dat" -M "MARKBAAD" >/dev/null 2>&1;

# same but with the softerror corrected
LD_PRELOAD="$safecopydebug" $safecopy -R 3 -b 1024 -f 4* -o "$tmpdir/test4.badblocks" debug "$tmpdir/test4.dat" -M "MARKBAAD" >/dev/null 2>&1;

if diff --brief test1.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test3.dat "$tmpdir/test3.dat" >/dev/null 2>&1 &&
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
