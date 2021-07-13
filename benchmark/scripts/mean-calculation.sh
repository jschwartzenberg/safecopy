#!/bin/sh

cdir="."
odir="../.."

mean=0;
total1=0;
total2=0;
max=$(
max=0
x=0
join $cdir/timing.log $odir/timing.log | {
	while read sec t1 v1 t2 v2; do
	error=$(( t1-t2 ))
	relerror=$(( 200*(t1-t2)/(t1+t2) ))
	x=$(( x+relerror ));
	mean=$(( mean + error ))
	total1=$(( total1 + t1 ))
	total2=$(( total2 + t2 ))
	if [ $total1 -gt $max ]; then
		max=$total1
	fi
	if [ $total2 -gt $max ]; then
		max=$total2
	fi

	echo $sec $error $relerror $mean $total1 $total2 $x
	done >difference.log 
	echo $max
	}
)
#draw
echo 'set term postscript color eps enhanced;
set output "graph2.eps";
set ylabel "sector read time derivation in percent";
set y2tics
set y2label "total time";
set y2range [*:*];
set yrange [-500:500];
set xlabel "block #";
plot "difference.log" using 1:3 axes x1y1 with lines title "sector read time derivation in %", "difference.log" using 1:(100*($5-$6)/$6) axes x1y1 with lines title "accumulated read time derivation in %", "difference.log" using 1:6 axes x1y2 with lines title "total original read time", "difference.log" using 1:5 axes x1y2 with lines title "total simulation read time", "difference.log" using 1:4 axes x1y2 with lines title "total read time error";
pause mouse;
' |gnuplot

