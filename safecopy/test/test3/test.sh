#!/bin/sh

basedir="$1"
tmpdir="$2"

safecopy="$basedir/src/safecopy"
safecopydebug="../libsafecopydebug/src/libsafecopydebuglb.so.1.0: $LD_PRELOAD"

echo -n " - Testing safecopy 3: Soft recovery + hard errors: "

# safecopy must successfully recover from the soft error but write the hard error to the badblock list correctly
LD_PRELOAD="$safecopydebug" $safecopy -R 3 -b 1024 -f 4* -o "$tmpdir/test1.badblocks" debug "$tmpdir/test1.dat" >/dev/null 2>&1

# safecopy cannot recover doe to low retry factor and must write all sectors to the badblock list. it will also not recover the blocks 7 8 and 9 due to big skipsize
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 4* -o "$tmpdir/test2.badblocks" debug "$tmpdir/test2.dat" >/dev/null 2>&1

# safecopy cannot recover the soft error block but must spot the correct sector between the soft and hard error blocks and the correct data at the end
LD_PRELOAD="$safecopydebug" $safecopy -R 2 -b 1024 -f 2* -o "$tmpdir/test3.badblocks" debug "$tmpdir/test3.dat" >/dev/null 2>&1

if diff --brief test1.dat "$tmpdir/test1.dat" >/dev/null 2>&1 &&
   diff --brief test2.dat "$tmpdir/test2.dat" >/dev/null 2>&1 &&
   diff --brief test3.dat "$tmpdir/test3.dat" >/dev/null 2>&1 &&
   diff --brief test1.badblocks "$tmpdir/test1.badblocks" >/dev/null 2>&1 &&
   diff --brief test2.badblocks "$tmpdir/test2.badblocks" >/dev/null 2>&1 &&
   diff --brief test3.badblocks "$tmpdir/test3.badblocks" >/dev/null 2>&1; then
		echo "OK"
		exit 0
fi
echo "FAIL"
exit 1
