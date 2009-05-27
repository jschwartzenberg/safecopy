#!/bin/sh

if ! [ -e ../test/test.sh ]; then
	echo "please execute this script within its own directory!";
	exit 1;
fi;
workingdir="$( pwd )";
cd ..;
basedir="$( pwd )";
testdir="$basedir/test";
tmpdir="/tmp/safecopytest";

tests="
test0
test1
test2
test3
test4
test5
test6
test7
test8
test9
test10
test11
";

function end() {
	cd $workingdir;
	if [ -e "$tmpdir" ]; then
		rm -rf $tmpdir;
	fi;
	exit $1;
}

for instance in $tests; do
	if [ -e "$tmpdir" ]; then
		rm -rf $tmpdir;
		if [ -e "$tmpdir" ]; then
			echo "no write access to $tmpdir - aborting!";
			end 1;
		fi;
	fi;
	if ! mkdir "$tmpdir"; then
		echo "no write access to $tmpdir - aborting!";
		end 1;
	fi
	if ! cd "$testdir/$instance"; then
		echo "could not find $instance";
		end 1;
	fi;
	echo "running $instance:";
	if ./test.sh "$basedir" "$tmpdir"; then
		echo " - - successful";
	else
		echo " - - failed - aborting";
		end 1;
	fi;
done;

end 0;


