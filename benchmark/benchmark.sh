#!/bin/bash

# Benchmark for bad media

simulator="../simulator/src/libsimulatorlb.so.1.0"

disk="$1"

echo "
 Safecopy device simulator.
 This program simulates bad media and allows you to benchmark programs that try to read from it.
"
if [ ! -e "$simulator" ]; then
	echo "Error: $simulator was not found. Please make sure the simulator library has been compiled."
	exit
fi

if [ ! -e "$disk/simulator.cfg" ]; then
	echo "Error: $disk does not contain a simulator.cfg file with data on how to simulate a device.
 Usage:
$0 <disk>
 <disk> is one of the subdirectories including a simulator.cfg file set up to simulate your specific hardware.
 Example:
$0 floppy
 will simulate a damaged 5.25 inch disk."
	exit
fi

echo "Simulating $disk:
 The virtual file simulating $disk is called 'debug' and accessible in any directory.
 You can now benchmark any program attempting to read from it.

 For example:
safecopy -b 512 -o badblocks.log debug output.dat
 or
dd if=debug of=output.dat

 You can exit the simulator at any time with:
exit

"
cd "$disk"
echo "Information on $disk:"
cat README
LD_PRELOAD="../$simulator:$LD_PRELOAD" /bin/sh

