#!/bin/sh

test_name="safecopy, restoring data with non-persistant IO errors and IO error on writing"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "Test if the data gets copied successfully despite of recoverable read errors."
	LD_PRELOAD="$preload" $safecopy -R 3 -b 1024 -f 2* -o "$testsuite_tmpdir/test1.badblocks" -O "$testsuite_tmpdir/test1.wbadblocks" debug debugw >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 0 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	mv "$testsuite_tmpdir/test.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"
	testsuite_assert_files_identical "test1.wbadblocks" "$testsuite_tmpdir/test1.wbadblocks"

	testsuite_debug "Test if the data gets partially copied with unrecoverable read errors."
	testsuite_debug " Since marking is off, unreadable blocks get not written"
	testsuite_debug " so one unwritable block stays undiscovered"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 2* -o "$testsuite_tmpdir/test2.badblocks" -O "$testsuite_tmpdir/test2.wbadblocks" debug debugw >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	mv "$testsuite_tmpdir/test.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test2.badblocks"
	testsuite_assert_files_identical "test2.wbadblocks" "$testsuite_tmpdir/test2.wbadblocks"

	testsuite_debug "Test if the data gets copied successfully despite of recoverable read errors (with marking option)."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 3 -b 1024 -f 2* -o "$testsuite_tmpdir/test3.badblocks" -O "$testsuite_tmpdir/test3.wbadblocks" debug debugw >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 0 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	mv "$testsuite_tmpdir/test.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test3.badblocks"
	testsuite_assert_files_identical "test1.wbadblocks" "$testsuite_tmpdir/test3.wbadblocks"

	testsuite_debug "Test if the data gets partially copied with unrecoverable read errors and the rest marked as bad."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 2* -o "$testsuite_tmpdir/test4.badblocks" -O "$testsuite_tmpdir/test4.wbadblocks" debug debugw >"$testsuite_tmpdir/test4.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test4.out"
	fi
	mv "$testsuite_tmpdir/test.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test4.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test4.badblocks"
	testsuite_assert_files_identical "test1.wbadblocks" "$testsuite_tmpdir/test4.wbadblocks"

}

testsuite_runtest


