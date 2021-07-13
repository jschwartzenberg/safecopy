#!/bin/sh

# sift log data for gnuplot viewing

#existing logfiles
timesfile=timing.log
badblockfile=badblocks.log

#1. any block listed in badblocks file is a bad / unrecoverable block
#2. any block listed in timings exactly once and not listed in badblocks
#   is a sucesfully read block
#3. any block listed in timings multiple times, but not listed in badblocks
#   is a "recovered" block. i.e. a block that succeeded reading on 2. or
#   3rd attempt.
# 3.1 the last entry in 3 is a successful read
# 3.2 all other entries in 3 are unsuccessful read attempts

# goal: geat timing information on every block in each of those groups


#step 1: sort all input files so join can work on them

echo "sorting badblocks file"
cat $badblockfile |while read a b; do printf "%010u\n" $a; done >badblocks.sorted

#extract list of ALL blocks
echo "extrating blocks"
cat $timesfile | sed -e 's/ .*//' >allblocks

# get a list of blocks that exist exactly once

echo "extracting blocks read once"
uniq -u allblocks >onceblocks

# get a list of recovered blocks

echo "extracting blocks read multiple times"
uniq -d allblocks >repeatblocks


# get blocks in group 2 (good sectos)

echo "joining to find out good blocks"
join -v 1 onceblocks badblocks.sorted >goodblocks

# get blocks in group 3 (recovered blocks)

echo "joining to find out recovered blocks"
join -v 1 repeatblocks badblocks.sorted >recblocks

# combine with timing data to create data plot lists
echo "joining good and bad blocks with timing data"
join $timesfile goodblocks >goodtimes-x
join $timesfile recblocks >rectimes
join $timesfile badblocks.sorted >badtimes

# find the two different subgroups
echo "finding out sucesfull and unsucessful recoveries and times"

grep -E " -1\$" rectimes >rectimes-bad
grep -vE " -1\$" rectimes >rectimes-good-x
#rectimes-good-x over-matches since it contains some good sectors that had been read twice
join rectimes-good-x rectimes-bad | cut -d " " -f 1-3 >rectimes-good

#these over-matching lines need to be added to goodtimes
join -v 1 rectimes-good rectimes-bad >>goodtimes-x
#which needs to be resorted
sort goodtimes-x >goodtimes

# delete tmp files 
echo "deleting temporary files"
rm badblocks.sorted allblocks onceblocks repeatblocks goodblocks recblocks rectimes rectimes-good-x goodtimes-x


#call gnuplot

#echo 'set term postscript color eps enhanced;
#set output "graph.eps";
#set ylabel "read time in microseconds";
#set xlabel "block #";
#set log y;
#plot "badtimes" with dots title "read attempts from unrecoverable (bad) blocks", "goodtimes" with dots title "reading from good blocks", "rectimes-bad" with dots title "unsuccessful read attempts from recoverable blocks", "rectimes-good" with dots title "successful read attempts from recoverable blocks";
#pause mouse;
#' |gnuplot
echo 'set term postscript color eps enhanced;
set output "graph.eps";
set ylabel "read time in microseconds";
set xlabel "block #";
set log y;
plot "badtimes" with dots title "read attempts from unrecoverable (bad) blocks", "goodtimes" with dots title "reading from good blocks", "rectimes-bad" with dots title "unsuccessful read attempts from recoverable blocks", "rectimes-good" with dots title "successful read attempts from recoverable blocks";
pause mouse;
' |gnuplot

#rm goodtimes badtimes rectimes-good rectimes-bad
