#!/bin/false

safecopy="$testsuite_basedir/src/safecopy --debug 255"

debuglib="$testsuite_basedir/simulator/src/libsimulatorlb.so.1.0"
debugwlib="$testsuite_basedir/simulator/src/libsimulatorwlb.so.1.0"

preload="$debuglib: $debugwlib: $LD_PRELOAD"



