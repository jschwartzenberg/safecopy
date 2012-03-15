#!/bin/sh

test_name="debug library functionality (writing)"

source "../libtestsuite.sh"

function test_current() {


	testsuite_debug "Test if the test library gets loaded and works."
	testsuite_debug " Runnign cat on the virtual debug file should result in a 2048 byte file"
	testsuite_debug " since that is the address of the first simulated IO error."
	LD_PRELOAD="$preload" dd if=source.dat of="debugw" >"$testsuite_tmpdir/test.out" 2>&1
	if ! testsuite_assert_filesize "$testsuite_tmpdir/test.dat" 2048; then
		testsuite_debug "program reported:"
		testsuite_debug_file "$testsuite_tmpdir/test.out"
	fi

}

testsuite_runtest


