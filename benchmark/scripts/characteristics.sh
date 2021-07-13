#!/bin/sh


function classify() {
	base=$1;
	margin=$2;

	cat |while read sector timing res; do

		if [ $timing -lt $(( base+$margin )) ]; then
			echo "$sector $base 0" 
		else
			x=$base
			y=$margin
			z=0;
			while [ $timing -ge $(( x + y )) ]; do
				x=$(( x*2 ))
				y=$(( y*2 ))
				z=$(( z+1 ))
			done;
			echo "$sector $x $z" 
		fi
	done;
}

mkdir characteristics

head -n 2000 goodtimes |tail -n 1000  | {
	sum=0
	cnt=0
	min=-1;
	max=0;
	while read sec time res; do
		if [ $min = -1 ]; then min=$time; fi
		if [ $res -gt $max ]; then max=$res; fi
		if [ $time -lt $(( min*10 )) ]; then
			cnt=$(( cnt+1 ))
			sum=$(( sum + time ))
		fi
	done
	base=$(( sum / cnt ))
	margin=$(( base/2 ))
	lastblock=$( tail -n 1 timing.log | { read a b c; echo $a | sed -e 's/^0*//'; } )
	echo "T1 is $base - E is $margin"
	echo "finding {Xs}"
	cat goodtimes |classify $base $margin >characteristics/goodtimes;
	echo "finding {Xb}"
	cat badtimes |classify $base $margin >characteristics/badtimes;
	echo "finding {Xr}"
	cat rectimes-good |classify $base $margin >characteristics/rectimes-good;
	echo "finding {Yr}"
	cat rectimes-bad |classify $base $margin >characteristics/rectimes-bad;
	echo "drawing"
	echo "creating graph1"
	echo 'set term postscript color eps enhanced;
set output "graph2.eps";
set ylabel "read time in microseconds";
set xlabel "block #";
set log y;
plot "badtimes" with points title "read attempts from unrecoverable (bad) blocks", "goodtimes" with dots title "reading from good blocks", "rectimes-bad" with dots title "unsuccessful read attempts from recoverable blocks", "rectimes-good" with dots title "successful read attempts from recoverable blocks", '$margin'*  sin(x)+'$base' title "T1+-E";
' |gnuplot
	cd characteristics
	echo "creating graph2"
	echo 'set term postscript color eps enhanced;
set output "graph.eps";
set ylabel "read time in microseconds";
set xlabel "block #";
set log y;
plot "badtimes" with points title "read attempts from unrecoverable (bad) blocks", "goodtimes" with dots title "reading from good blocks", "rectimes-bad" with dots title "unsuccessful read attempts from recoverable blocks", "rectimes-good" with dots title "successful read attempts from recoverable blocks";
' |gnuplot
	echo "creating simulation data"
	blocksize=$max
	filesize=$(( lastblock * blocksize ))
	t1=$base
	mkdir simulation
	{
		echo "blocksize=$blocksize"
		echo "filesize=$filesize"
		echo "delay=$t1"
		echo "softfailcount=2"
		echo "source=/dev/zero"
		echo "verbose=0"

		cat goodtimes | sed -e 's/^0*\([0-9][0-9]*\) /\1 /g' | while read sec time fac; do
			if [ $fac -gt 0 ]; then
				echo "slow=$sec $fac"
			fi;
		done
		cat badtimes | sed -e 's/^0*\([0-9][0-9]*\) /\1 /g' | while read sec time fac; do
			echo "hardfail=$sec $fac"
		done
		cat rectimes-good | sed -e 's/^0*\([0-9][0-9]*\) /\1 /g' | while read sec time fac; do
			grep -E "^0*$sec " rectimes-bad |head -n 1 | {
				read secx time fac2
				echo "softfail=$sec $fac2 $fac"
			}
		done
	} >simulation/simulator.cfg

}


