//sorry for this file being one big long unreadable mess
//the main operation loop actually sits in main()
//basically processing the following sequence
// 1.declarations
// 2.command line parsing
// 3.opening io handles
// 4.initialisations
// 5.dynamic initialisations and tests
// 6.main io loop
// 6.a planning - calculate wanted read position based on include/exclude input files
// 6.b navigation - attempt to seek to requested input file position and find out actual position
// 6.c patience - wait for availability of data
// 6.d input - attempt to read from sourcefile
// 6.e feedback - calculate and display user feedback information
// 6.f reaction - act according to result of read operation
// 6.f.1 succesfull read:
// 6.f.1.a attempt to backtrack for readable data prior to current position or...
// 6.f.1.b write to output data file
// 6.f.2 failed read
// 6.f.2.a try again or...
// 6.f.2.b skip over bad area
// 6.f.2.c close and reopen source file
// 7.closing and finalisation

#define _FILE_OFFSET_BITS 64
// make off_t a 64 bit pointer on system that support it

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "arglist.h"

#define DEBUG_FLOW 1
#define DEBUG_IO 2
#define DEBUG_BADBLOCKS 4
#define DEBUG_SEEK 8
#define DEBUG_INCREMENTAL 16
#define DEBUG_EXCLUDE 32

#define MARK_INCLUDING 1
#define MARK_EXCLUDING 2

#define FAILSTRING "BaDbLoCk"
#define MAXBLOCKSIZE 104857600
#define BLOCKSIZE 4096
#define RETRIES 3
#define SEEKS 1

#define DEFBLOCKSIZE "1*"
#define DEFFAULTBLOCKSIZE "16*"
#define DEFRESOLUTION "1*"
#define DEFRETRIES 3
#define DEFHEADMOVE 1
#define DEFLOWLEVEL 1
#define DEFFAILSTRING NULL
#define DEFOUTPUTBB NULL
#define DEFINPUTBB NULL
#define DEFEXCLBB NULL

#define VERY_FAST 100
#define FAST VERY_FAST*100
#define SLOW FAST*10
#define VERY_SLOW SLOW*10
#define VERY_VERY_SLOW VERY_SLOW*10

#define VERY_FAST_ICON "  ;-}"
#define FAST_ICON "  :-)"
#define SLOW_ICON "  :-|"
#define VERY_SLOW_ICON "  8-("
#define VERY_VERY_SLOW_ICON "  8-X"

static int debugmode=0;

int debug(int debug,char *format,...) {
	if (debugmode & debug) {
		va_list ap;
		int ret;
		va_start(ap,format);
		ret=vfprintf(stderr,format,ap);
		va_end(ap);
		return ret;
	}
	return 0;
}

void usage(char * name) {
	fprintf(stdout,"Safecopy "VERSION" by CorvusCorax\n");
	fprintf(stdout,"Usage: %s [options] <source> <target>\n",name);
	fprintf(stdout,"Options:\n");
	fprintf(stdout,"	--stage1 : Preset to rescue most of the data fast,\n");
	fprintf(stdout,"	           using no retries and avoiding bad areas.\n");
	fprintf(stdout,"	           Presets: -f 10%% -r 10%% -R 1 -Z 0 -L 2 -M %s\n",FAILSTRING);
	fprintf(stdout,"	                    -o stage1.badblocks\n");
	fprintf(stdout,"	--stage2 : Preset to rescue more data, using no retries\n");
	fprintf(stdout,"	           but searching for exact ends of bad areas.\n");
	fprintf(stdout,"	           Presets: -f 128* -r 1* -R 1 -Z 0 -L 2\n");
	fprintf(stdout,"	                    -I stage1.badblocks\n");
	fprintf(stdout,"	                    -o stage2.badblocks\n");
	fprintf(stdout,"	--stage3 : Preset to rescue everything that can be rescued\n");
	fprintf(stdout,"	           using maximum retries, head realignment tricks\n");
	fprintf(stdout,"	           and low level access.\n");
	fprintf(stdout,"	           Presets: -f 1* -r 1* -R 4 -Z 1 -L 2\n");
	fprintf(stdout,"	                    -I stage2.badblocks\n");
	fprintf(stdout,"	                    -o stage3.badblocks\n");
	fprintf(stdout,"	All stage presets can be overridden by individual options.\n");
	fprintf(stdout,"	-b <size> : Blocksize for default read operations.\n");
	fprintf(stdout,"	            Set this to the physical sectorsize of your media.\n");
	fprintf(stdout,"	            Default: 1*\n");
	fprintf(stdout,"	            Hardware block size if reported by OS, otherwise %i\n",BLOCKSIZE);
	fprintf(stdout,"	-f <size> : Blocksize when skipping over badblocks.\n");
	fprintf(stdout,"	            Higher settings put less strain on your hardware,\n");
	fprintf(stdout,"	            but you might miss good areas in between two bad ones.\n");
	fprintf(stdout,"	            Default: 16*\n");
	fprintf(stdout,"	-r <size> : Resolution in bytes when searching for the exact\n");
	fprintf(stdout,"	            beginning or end of a bad area.\n");
	fprintf(stdout,"	            If you read data directly from a device there is no\n");
	fprintf(stdout,"	            need to set this lower than the hardware blocksize.\n");
	fprintf(stdout,"	            On mounted filesystems however, read blocks\n");
	fprintf(stdout,"	            and physical blocks could be misaligned.\n");
	fprintf(stdout,"	            Smaller values lead to very thorough attempts to read\n");
	fprintf(stdout,"	            data at the edge of damaged areas,\n");
	fprintf(stdout,"	            but increase the strain on the damaged media.\n");
	fprintf(stdout,"	            Default: 1*\n");
	fprintf(stdout,"	-R <number> : At least that many read attempts are made on the first\n");
	fprintf(stdout,"	              bad block of a damaged area with minimum resolution.\n");
	fprintf(stdout,"	              More retries can sometimes recover a weak sector,\n");
	fprintf(stdout,"	              but at the cost of additional strain.\n");
	fprintf(stdout,"	              Default: %i\n",RETRIES);
	fprintf(stdout,"	-Z <number> : On each error, force seek the read head from start to\n");
	fprintf(stdout,"	              end of the source device as often as specified.\n");
	fprintf(stdout,"	              That takes time, creates additional strain and might\n");
	fprintf(stdout,"	              not be supported by all devices or drivers.\n");
	fprintf(stdout,"	              Default: %i\n",SEEKS);
	fprintf(stdout,"	-L <mode> : Use low level device calls as specified:\n");
	fprintf(stdout,"	                   0  Do not use low level device calls\n");
	fprintf(stdout,"	                   1  Attempt low level device calls\n");
	fprintf(stdout,"	                      for error recovery only\n");
	fprintf(stdout,"	                   2  Always use low level device calls\n");
	fprintf(stdout,"	                      if available\n");
	fprintf(stdout,"	            Supported low level features in this version are:\n");
	fprintf(stdout,"	                SYSTEM  DEVICE TYPE   FEATURE\n");
	fprintf(stdout,"	                Linux   cdrom/dvd     bus/device reset\n");
	fprintf(stdout,"	                Linux   cdrom         read sector in raw mode\n");
	fprintf(stdout,"	                Linux   floppy        controller reset, twaddle\n");
	fprintf(stdout,"	            Default: %i\n",DEFLOWLEVEL);
	fprintf(stdout,"	--sync : Use synchronized read calls (disable driver buffering).\n");
	fprintf(stdout,"	         Default: Asynchronous read buffering by the OS is allowed\n");
	fprintf(stdout,"	-s <blocks> : Start position where to start reading.\n");
	fprintf(stdout,"	              Will correspond to position 0 in the destination file.\n");
	fprintf(stdout,"	              Default: block 0\n");
	fprintf(stdout,"	-l <blocks> : Maximum length of data to be read.\n");
	fprintf(stdout,"	              Default: Entire size of input file\n");
	fprintf(stdout,"	-I <badblockfile> : Incremental mode. Assume the target file already\n");
	fprintf(stdout,"	                    exists and has holes specified in the badblockfile.\n");
	fprintf(stdout,"	                    It will be attempted to retrieve more data from\n");
	fprintf(stdout,"	                    the listed blocks or from beyond the file size\n");
	fprintf(stdout,"	                    of the target file only.\n");
	fprintf(stdout,"	                    Warning: Without this option, the destination file\n");
	fprintf(stdout,"	                    will be emptied prior to writing.\n");
	fprintf(stdout,"	                    Use -I /dev/null if you want to continue a previous\n");
	fprintf(stdout,"	                    run of safecopy without a badblock list.\n");
	fprintf(stdout,"	                    Default: none\n");
	fprintf(stdout,"	-i <bytes> : Blocksize to interpret the badblockfile given with -I.\n");
	fprintf(stdout,"	             Default: Blocksize as specified by -b\n");
	fprintf(stdout,"	-X <badblockfile> : Exclusion mode. If used together with -I,\n");
	fprintf(stdout,"	                    excluded blocks override included blocks.\n");
	fprintf(stdout,"	                    Safecopy will not read or write any data from\n");
	fprintf(stdout,"	                    areas covered by exclude blocks.\n");
	fprintf(stdout,"	                    Default: none\n");
	fprintf(stdout,"	-x <bytes> : Blocksize to interpret the badblockfile given with -X.\n");
	fprintf(stdout,"	             Default: Blocksize as specified by -b\n");
	fprintf(stdout,"	-o <badblockfile> : Write a badblocks/e2fsck compatible bad block file.\n");
	fprintf(stdout,"	                    Default: none\n");
	fprintf(stdout,"	-S <seekscript> : Use external script for seeking in input file.\n");
	fprintf(stdout,"	                  (Might be useful for tape devices and similar).\n");
	fprintf(stdout,"	                  Seekscript must be an executable that takes the\n");
	fprintf(stdout,"	                  number of blocks to be skipped as argv1 (1-64)\n");
	fprintf(stdout,"	                  the blocksize in bytes as argv2\n");
	fprintf(stdout,"	                  and the current position (in bytes) as argv3.\n");
	fprintf(stdout,"	                  Return value needs to be the number of blocks\n");
	fprintf(stdout,"	                  successfully skipped, or 0 to indicate seek failure.\n");
	fprintf(stdout,"	                  The external seekscript will only be used\n");
	fprintf(stdout,"	                  if lseek() fails and we need to skip over data.\n");
	fprintf(stdout,"	                  Default: none\n");
	fprintf(stdout,"	-M <string> : Mark unrecovered data with this string instead of\n");
	fprintf(stdout,"	              skipping it. This helps in later finding corrupted\n");
	fprintf(stdout,"	              files on rescued file system images.\n");
	fprintf(stdout,"	              The default is to zero unreadable data on creation\n");
	fprintf(stdout,"	              of output files, and leaving the data as it is\n");
	fprintf(stdout,"	              on any later run.\n");
	fprintf(stdout,"	              Warning: When used in combination with\n");
	fprintf(stdout,"	              incremental mode (-I) this may overwrite data\n");
	fprintf(stdout,"	              in any block that occurs in the -I file.\n");
	fprintf(stdout,"	              Blocks not in the -I file, or covered by the file\n");
	fprintf(stdout,"	              specified with -X are save from being overwritten.\n");
	fprintf(stdout,"	              Default: none\n");
	fprintf(stdout,"	-h | --help : Show this text\n\n");
	fprintf(stdout,"Valid parameters for -f -r -b <size> options are:\n");
	fprintf(stdout,"	<integer>	Amount in bytes - i.e. 1024\n");
	fprintf(stdout,"	<percentage>%%	Percentage of whole file/device size - e.g. 10%\n");
	fprintf(stdout,"	<number>*	-b only, number times blocksize reported by OS\n");
	fprintf(stdout,"	<number>*	-f and -r only, number times the value of -b\n\n");
	fprintf(stdout,"Description of output:\n");
	fprintf(stdout,"	. : Between 1 and 1024 blocks successfully read.\n");
	fprintf(stdout,"	_ : Read of block was incomplete. (possibly end of file)\n");
	fprintf(stdout,"	    The blocksize is now reduced to read the rest.\n");
	fprintf(stdout,"	|/| : Seek failed, source can only be read sequentially.\n");
	fprintf(stdout,"	> : Read failed, reducing blocksize to read partial data.\n");
	fprintf(stdout,"	! : A low level error on read attempt of smallest allowed size\n");
	fprintf(stdout,"	    leads to a retry attempt.\n");
	fprintf(stdout,"	[xx](+yy){ : Current block and number of bytes continuously\n");
	fprintf(stdout,"	             read successfully up to this point.\n");
	fprintf(stdout,"	X : Read failed on a block with minimum blocksize and is skipped.\n");
	fprintf(stdout,"	    Unrecoverable error, destination file is padded with zeros.\n");
	fprintf(stdout,"	    Data is now skipped until end of the unreadable area is reached.\n");
	fprintf(stdout,"	< : Successful read after the end of a bad area causes\n");
	fprintf(stdout,"	    backtracking with smaller blocksizes to search for the first\n");
	fprintf(stdout,"	    readable data.\n");
	fprintf(stdout,"	}[xx](+yy) : current block and number of bytes of recent\n");
	fprintf(stdout,"	             continuous unreadable data.\n\n");
	fprintf(stdout,"Copyright 2009, distributed under terms of the GPL\n\n");
}

// parse an option string
off_t parseoption(char* option, int blocksize, off_t filesize,char* defaultvalue) {
	if (option==NULL) {
		return parseoption(defaultvalue,blocksize,filesize,defaultvalue);
	}
	int len=strlen(option);
	int number;
	off_t result;
	char* newoption=strdup(option);
	if (arglist_isinteger(option)==0) {
		return(arglist_integer(option));
	}
	if (len<2) return parseoption(defaultvalue,blocksize,filesize,defaultvalue);
	if (option[len-1]=='%') {
		newoption[len-1]=0;
		if (arglist_isinteger(newoption)==0) {
			number=arglist_integer(newoption);
			if (filesize>0) {
				result=((filesize*number)/100);
			} else {
				result=(blocksize*number);
			}
			// round by blocksize
			return ((((result/blocksize)>0)?(result/blocksize):1)*blocksize);
		}
		return parseoption(defaultvalue,blocksize,filesize,defaultvalue);
	}
	if (option[len-1]=='*') {
		newoption[len-1]=0;
		if (arglist_isinteger(newoption)==0) {
			number=arglist_integer(newoption);
			return (blocksize*number);
		}
		return parseoption(defaultvalue,blocksize,filesize,defaultvalue);
	}
	return parseoption(defaultvalue,blocksize,filesize,defaultvalue);
}

// print percentage to stderr
void printpercentage(int percent) {
	char percentage[16]="100%";
	int t=0;
	if (percent>100) percent=100;
	if (percent<0) percent=0;
	sprintf(percentage,"      %i%%",percent);
	write(2,percentage,strlen(percentage));
	while (percentage[t++]!='\x0') {
		write(2,&"\b",1);
	}
}

// calculate difference in usecs between two struct timevals
long int timediff(struct timeval oldtime,struct timeval newtime) {

	long int usecs=newtime.tv_usec-oldtime.tv_usec;
	usecs=usecs+((newtime.tv_sec-oldtime.tv_sec)*1000000);
	return usecs;

}

// map delays to quality categories
int timecategory (long int time) {
	if (time<=VERY_FAST) return VERY_FAST;
	if (time<=FAST) return FAST;
	if (time<=SLOW) return SLOW;
	if (time<=VERY_SLOW) return VERY_SLOW;
	return VERY_VERY_SLOW;
}

// map quality categories to icons
char* timeicon(int timecat) {
	switch (timecat) {
		case VERY_VERY_SLOW: return VERY_VERY_SLOW_ICON;
		case VERY_SLOW: return VERY_SLOW_ICON;
		case SLOW: return SLOW_ICON;
		case FAST: return FAST_ICON;
		case VERY_FAST: return VERY_FAST_ICON;
	}
	return "  ???";
}

// print quality indicator to stderr
void printtimecategory(int timecat) {
	char * icon=timeicon(timecat);
	int t=0;
	write(2,icon,strlen(icon));
	while (icon[t++]!='\x0') {
		write(2,&"\b",1);
	}
}

// global flag variable and handler for CTRL+C
int wantabort=0;
void signalhandler(int sig) {
	wantabort=1;
}

// wrapper for external script to seek in character device files (tapes)
off_t emergency_seek(off_t new,off_t old,off_t blocksize, char* script) {
	char firstarg[128];
	char secondarg[128];
	char thirdarg[128];
	off_t delta=new-old;
	int status;
	debug(DEBUG_SEEK,"debug: emergency seek");
	// do nothing if no seek is necessary
	if (new==old) return old;
	// do nothing if no script is given
	if (script==NULL) return(-2);
	// because of the limited size of EXITSTATUS,
	// we need to call separate seeks for big skips
	while (delta>(blocksize*64)) {
		old=emergency_seek(old+(blocksize*64),old,blocksize,script);
		if (old<0) return old;
		delta=new-old;
	}
	// minimum seek is one block
	if (delta<blocksize) delta=blocksize;
	// call a script
	sprintf(firstarg,"%llu",(delta/blocksize));
	sprintf(secondarg,"%llu",blocksize);
	sprintf(thirdarg,"%llu",old);
	pid_t child=fork();
	if (child==0) {
		execlp(script,script,firstarg,secondarg,thirdarg,NULL);
		exit (-2);
	} else if( child<0) {
		return(-2);
	}
	waitpid(child,&status,0);
	// return exit code - calculate bytes from blocks in case of positive exit value
	if (WEXITSTATUS(status)==0) return (-2);
	return (old+(blocksize*WEXITSTATUS(status)));
}

// function to mark bad blocks in output
void markbadblocks(int destination, off_t writeposition, off_t remain, char* marker, char* databuffer, off_t blocksize)
{
	off_t writeoffset,writeremain,writeblock,cposition;
	char nullmarker[8]={0};
	if (remain<=0) {
		debug(DEBUG_BADBLOCKS,"debug: no bad blocks to mark\n");
		return;
	}
	debug(DEBUG_BADBLOCKS,"debug: marking %llu bad bytes at %llu\n",remain,writeposition);
	// if a marker is given, we need to write it to the
	// destination at the current position
	// first copy the marker into the data buffer
	writeoffset=0;
	writeremain=strlen(marker);
	if (writeremain==0) writeremain=8;
	while (writeoffset+writeremain<blocksize) {
		if (writeremain!=0) {
			memcpy(databuffer+writeoffset,marker,writeremain);
		} else {
			memcpy(databuffer+writeoffset,nullmarker,writeremain);
		}
		writeoffset+=writeremain;
	}
	memcpy(databuffer+writeoffset,marker,blocksize-writeoffset);
	// now write it to disk
	writeremain=remain;
	writeoffset=0;
	while (writeremain>0) {
		// write data to destination file
		debug(DEBUG_SEEK,"debug: seek in destination file: %llu\n",writeposition);
		cposition=lseek(destination,writeposition,SEEK_SET);
		if (cposition<0) {
			fprintf(stderr,"\nError: seek() in output failed");
			perror("");
			return;
		}
		debug(DEBUG_IO,"debug: writing badblock marker to destination file: %llu bytes at %llu\n",writeremain,cposition);
		writeblock=write(destination,databuffer+(writeoffset % blocksize),(blocksize>writeremain?writeremain:blocksize));
		if (writeblock<=0) {
			fprintf(stderr,"\nError: write to output failed");
			perror("");
			return;
		}
		writeremain-=writeblock;
		writeoffset+=writeblock;
		writeposition+=writeblock;
	}
}

// write blocks to badblock file - up to but not including given position
void outputbadblocks(off_t start,off_t limit,int bblocksout,off_t *lastbadblock,off_t startoffset, off_t blocksize, char* textbuffer) {
	off_t tmp_pos;
	// write badblocks to file if requested
	// start at first bad block in current bad set
	tmp_pos=(start/blocksize);
	// override that with the first not yet written one
	if (*lastbadblock>=tmp_pos) {
		tmp_pos=*lastbadblock+1;
	}
	// then write all blocks that are smaller than the current one
	// note the calculation takes into account wether
	// the current block STARTS in a error but is ok here 
	// (which wouldnt be the case if we compared tmp_pos directly)
	while ((tmp_pos*blocksize)<limit) {
		*lastbadblock=tmp_pos;
		sprintf(textbuffer,"%llu\n",*lastbadblock);
		debug(DEBUG_BADBLOCKS,"debug: declaring bad block: %llu\n",*lastbadblock);
		write(bblocksout,textbuffer,strlen(textbuffer));
		tmp_pos++;
	}
}

#define REALMARKOUTPUT_PARAMS startoffset,blocksize,lastbadblock,lastmarked,marker,databuffer,textbuffer,bblocksout,bblocksoutfile,destination,xblocksinfile,xblocksin,lastxblock,previousxblock,xblocksize,excluding 
// function to mark a given section in both destination file (badblock marking) and badblock output
void realmarkoutput (
	// these are relevant
	off_t start, off_t end, off_t min, off_t max,
	// the rest is just meta-globals and will be specified by macro above
	off_t startoffset, off_t blocksize, off_t *lastbadblock, off_t * lastmarked, char* marker, char* databuffer, char* textbuffer,int bblocksout, char* bblocksoutfile,int destination, char* xblocksinfile, FILE ** xblocksin,off_t * lastxblock, off_t * previousxblock,off_t xblocksize,int excluding
) {
	off_t first=start;
	off_t last=end;
	char * tmp;
	off_t tmp_pos;
	if (min+startoffset>first) first=min+startoffset;
	if (max+startoffset<last) last=max+startoffset;

	if (excluding) {

		// Exclusion mode, check relevant exclude sectors whether they affect the current position
		// first check if we need to backtrack in exclude file.
		debug(DEBUG_EXCLUDE,"debug: checking for exclude blocks during output, at position %llu\n",first);
		if (first<*lastxblock*xblocksize) {
			debug(DEBUG_EXCLUDE,"debug: possibly need backtracking in exclude list, next exclude block %lli\n",*lastxblock);
			if (first<*previousxblock*xblocksize) {
				// we read too far in exclude block file, probably after backtracking
				// close exclude file and reopen
				debug(DEBUG_EXCLUDE,"debug: reopening exclude file and reading from the start\n");
				fclose(*xblocksin);
				*xblocksin=fopen(xblocksinfile,"r");
				if (*xblocksin==NULL) {
					fprintf(stderr,"Error reopening exclusion badblock file for reading: %s",xblocksinfile);
					perror("");
					*previousxblock=((unsigned)-1)>>1;
					*lastxblock=((unsigned)-1)>>1;
					return;
				}
				*lastxblock=-1;
				*previousxblock=-1;
			} else if((*previousxblock+1)*xblocksize>first) {
				// backtrack just one exclude block
				*lastxblock=*previousxblock;
				debug(DEBUG_EXCLUDE,"debug: using last exclude block %lli\n",*lastxblock);
			} else {
				debug(DEBUG_EXCLUDE,"debug: false alarm, current exclude block is fine\n");
			}
		}
		tmp_pos = *lastxblock*xblocksize;
		while (tmp_pos<last) {
			if (tmp_pos+xblocksize>first) {
				if (tmp_pos<=first) {
					// start of current block is covered by exclude block. skip to end and try again
					first=tmp_pos+xblocksize;
					if (first>last) {
						debug(DEBUG_EXCLUDE,"debug: current bad block area is completely covered by xblocks, skipping\n");
						return;
					}
					debug(DEBUG_EXCLUDE,"debug: start of current bad block area is covered by xblocks shrinking\n");
					realmarkoutput(first,last,first-startoffset,last-startoffset,REALMARKOUTPUT_PARAMS);
					
					return;
				} else if(tmp_pos<last) {
					// 
// ATTENTION: there could be a reamaining part behind this xblock - needs two recursive calls to self to fix!
					debug(DEBUG_EXCLUDE,"debug: current bad block area is partially covered by xblocks, splitting\n");
					realmarkoutput(first,tmp_pos,first-startoffset,tmp_pos-startoffset,REALMARKOUTPUT_PARAMS);
					realmarkoutput(tmp_pos,last,tmp_pos-startoffset,last-startoffset,REALMARKOUTPUT_PARAMS);
					return;
				} else {
					// start of exclude block is beyond end of our area. we are done
					break;
				}
			} else {
				// read next exclude block
				tmp=fgets(textbuffer,64,*xblocksin);
				if (sscanf(textbuffer,"%llu",&tmp_pos)!=1) tmp=NULL;
				if (tmp==NULL) {
					// no more bad blocks in input file
					break;
				}
				*previousxblock=*lastxblock;
				*lastxblock=tmp_pos;
				tmp_pos=*lastxblock*xblocksize;
			}
		}
	}

	if (marker) {
		debug(DEBUG_BADBLOCKS,"debug: marking badblocks from %llu to %llu \n",first,last);
		if (*lastmarked<first-startoffset) {
			*lastmarked=first-startoffset;
		}
		markbadblocks(destination,*lastmarked,last-(*lastmarked+startoffset),marker,databuffer,blocksize);
	}
	if (bblocksoutfile!=NULL) {
		debug(DEBUG_BADBLOCKS,"debug: declaring badblocks from %llu to %llu \n",first,last);

		outputbadblocks(first,last,bblocksout,lastbadblock,startoffset,blocksize,textbuffer);
	}
}

#define MARKOUTPUT_PARAMS startoffset,blocksize,targetsize,&lastbadblock,&lastmarked,marker,databuffer,textbuffer,incremental,bblocksin,iblocksize,&lastsourceblock,bblocksout,bblocksoutfile,destination,xblocksinfile,&xblocksin,&lastxblock,&previousxblock,xblocksize,excluding
// function to mark output - taking include file information into account to not touch not mentioned blocks
void markoutput(
	// these are relevant
char* description,off_t readposition, off_t lastgood,
	// the rest is just meta-globals and will be specified by macro above
off_t startoffset, off_t blocksize, off_t targetsize, off_t * lastbadblock, off_t *lastmarked, char* marker, char* databuffer,char *textbuffer, int incremental, FILE *bblocksin, off_t iblocksize, off_t *lastsourceblock,int bblocksout,char *bblocksoutfile,int destination, char* xblocksinfile, FILE ** xblocksin,off_t * lastxblock, off_t * previousxblock,off_t xblocksize,int excluding
) {
	off_t tmp_pos;
	char *tmp;

	tmp_pos=lastgood+startoffset;
	// check for incremental mode in incremental mode, handle only sectors mentioned in include file
	// for marking, the include sector alignments are relevant, for badblock output the affected blocks
	// in our blocksize
	if (incremental) {
		off_t inc_pos= *lastsourceblock*iblocksize;
		// repeat as long as 
		while (inc_pos<readposition+startoffset) {
			if (inc_pos+iblocksize>tmp_pos) {
				debug(DEBUG_BADBLOCKS,"debug: %s %llu - %llu - marking output for infile block %lli (%llu - %llu)\n",description,tmp_pos,readposition+startoffset,inc_pos/iblocksize,inc_pos,inc_pos+iblocksize);
				realmarkoutput(inc_pos,inc_pos+iblocksize,lastgood,readposition,REALMARKOUTPUT_PARAMS);
			}
			if(inc_pos+iblocksize>readposition+startoffset) {
				// do not read in another include block
				// if the current one is still good for future use.
				break;
			}
			tmp=fgets(textbuffer,64,bblocksin);
			if (sscanf(textbuffer,"%llu",lastsourceblock)!=1) tmp=NULL;
			if (tmp==NULL) {
				// no more bad blocks in input file
				// if exists
				if ((readposition+startoffset)<targetsize) {
					// go to end of target file for resuming
					*lastsourceblock=targetsize/iblocksize;
				} else if (targetsize) {
					*lastsourceblock=(readposition+startoffset)/iblocksize;
				} else {
					break;
				}
			}
			inc_pos= *lastsourceblock*iblocksize;
		}
	} else {
		debug(DEBUG_BADBLOCKS,"debug: %s %llu - %llu - marking output for whole bad area\n",description,tmp_pos,readposition+startoffset);
		realmarkoutput(tmp_pos,readposition+startoffset,lastgood,readposition,REALMARKOUTPUT_PARAMS);
	}

}

// main
int main(int argc, char ** argv) {

// 1.declarations
	// commandline argument handler class
	struct arglist *carglist;
	// filenames
	char *sourcefile,*destfile,*bblocksinfile,*xblocksinfile,*bblocksoutfile,*seekscriptfile;
	// default options
	char *blocksizestring=DEFBLOCKSIZE;
	char *resolutionstring=DEFRESOLUTION;
	char *faultblocksizestring=DEFFAULTBLOCKSIZE;
	char *bblocksinstring=DEFINPUTBB;
	char *bblocksoutstring=DEFOUTPUTBB;
	char *xblocksinstring=DEFEXCLBB;
	char *failuredefstring=DEFFAILSTRING;
	int retriesdef=DEFRETRIES;
	int headmovedef=DEFHEADMOVE;
	int lowleveldef=DEFLOWLEVEL;
	// file descriptors
	int source,destination,bblocksout;
	// high level file descriptor
	FILE *bblocksin,*xblocksin;

	// file offset variables
	off_t readposition,cposition,sposition,writeposition;
	off_t startoffset,length,writeoffset;
	// variables for handling read/written sizes/remainders
	off_t remain,maxremain,block,writeblock,writeremain;
	// pointer to main IO data buffer
	char * databuffer;
	// a buffer for output text
	char textbuffer[256];
	// buffer pointer for sfgets() 
	char *tmp;
	// pointer to marker string
	char *marker=NULL;
	// several local integer variables
	off_t fsblocksize,blocksize,iblocksize,xblocksize,faultblocksize;
	off_t resolution;
	int retries,seeks,cseeks;
	int incremental,excluding,lowlevel,syncmode;
	int counter,percent,oldpercent,newerror,newsofterror;
	int backtracemode,output,linewidth,seekable,desperate;
	// indicator wether stdin/stderr is a terminal - affects output
	int human=0;

	// error indicators and flags
	off_t softerr,harderr,lasterror,lastgood,lastmarked;
	// tmp vars for file offsets
	off_t tmp_pos,tmp_bytes;
	// variables to remember beginning and end of previous good/bad area
	off_t lastbadblock,lastxblock,previousxblock,lastsourceblock;
	// stat() needs this
	struct stat filestatus;
	// input filesize and size of unreadable area
	off_t filesize,damagesize,targetsize;
	// times
	struct timeval oldtime,newtime;
	// and timing helper variables
	long int elapsed,oldelapsed,oldcategory;
	// select() needs these
	fd_set rfds,efds;
	// tmp int vars
	int errtmp;

// 2.command line parsing

	// parse all commandline arguments
	carglist=arglist_new(argc,argv);
	arglist_addarg (carglist,"--stage",1);
	arglist_addarg (carglist,"--stage1",0);
	arglist_addarg (carglist,"--stage2",0);
	arglist_addarg (carglist,"--stage3",0);
	arglist_addarg (carglist,"--debug",1);
	arglist_addarg (carglist,"--help",0);
	arglist_addarg (carglist,"-h",0);
	arglist_addarg (carglist,"--sync",0);
	arglist_addarg (carglist,"-b",1);
	arglist_addarg (carglist,"-f",1);
	arglist_addarg (carglist,"-r",1);
	arglist_addarg (carglist,"-R",1);
	arglist_addarg (carglist,"-s",1);
	arglist_addarg (carglist,"-L",1);
	arglist_addarg (carglist,"-l",1);
	arglist_addarg (carglist,"-o",1);
	arglist_addarg (carglist,"-I",1);
	arglist_addarg (carglist,"-i",1);
	arglist_addarg (carglist,"-X",1);
	arglist_addarg (carglist,"-x",1);
	arglist_addarg (carglist,"-S",1);
	arglist_addarg (carglist,"-Z",1);
	arglist_addarg (carglist,"-M",1);

	// find out wether the user is wetware
	human=(isatty(1) & isatty(2));
	
	if ((arglist_arggiven(carglist,"--debug")==0)) {
		debugmode=arglist_integer(arglist_parameter(carglist,"--debug",0));
	}
	if ((arglist_arggiven(carglist,"--help")==0)
			|| (arglist_arggiven(carglist,"-h")==0)
			|| (arglist_parameter(carglist,"VOIDARGS",2)!=NULL)
			|| (arglist_parameter(carglist,"VOIDARGS",1)==NULL)) {
		usage(argv[0]);
		arglist_kill(carglist);
		return 0;
	}
	sourcefile=arglist_parameter(carglist,"VOIDARGS",0);
	destfile=arglist_parameter(carglist,"VOIDARGS",1);

	if (arglist_arggiven(carglist,"--stage1")==0 ||arglist_integer(arglist_parameter(carglist,"--stage",0))==1) {
		faultblocksizestring="10%";
		resolutionstring="10%";
		retriesdef=1;
		headmovedef=0;
		lowleveldef=2;
		failuredefstring=FAILSTRING;
		bblocksoutstring="stage1.badblocks";
	}
	if (arglist_arggiven(carglist,"--stage2")==0 || arglist_integer(arglist_parameter(carglist,"--stage",0))==2) {
		faultblocksizestring="128*";
		resolutionstring="1*";
		retriesdef=1;
		headmovedef=0;
		lowleveldef=2;
		bblocksinstring="stage1.badblocks";
		bblocksoutstring="stage2.badblocks";
	}
	if (arglist_arggiven(carglist,"--stage3")==0 || arglist_integer(arglist_parameter(carglist,"--stage",0))==3) {
		faultblocksizestring="1*";
		resolutionstring="1*";
		bblocksinstring="stage2.badblocks";
		bblocksoutstring="stage3.badblocks";
		retriesdef=4;
		headmovedef=1;
		lowleveldef=2;
	}

	// low level calls enabled?
	lowlevel=lowleveldef;
	if (arglist_arggiven(carglist,"-L")==0) {
		lowlevel=arglist_integer(arglist_parameter(carglist,"-L",0));
	}
	if (lowlevel<0) lowlevel=0;
	if (lowlevel>2) lowlevel=2;
	fprintf(stdout,"Low level device calls enabled mode: %i\n",lowlevel);

	// synchronous IO
	syncmode=0;
	if (arglist_arggiven(carglist,"--sync")==0) {
		fprintf(stdout,"Using synchronized IO on source.\n");
		syncmode=O_RSYNC;
	}

	// find out source file size and block size
	filesize=0;
	fsblocksize=BLOCKSIZE;
	if(!stat(sourcefile,&filestatus)) {
		filesize=filestatus.st_size;
		if (filestatus.st_blksize) {
			fprintf(stdout,"Reported hw blocksize: %lu\n",filestatus.st_blksize);
			fsblocksize=filestatus.st_blksize;
		}
	}
	if (lowlevel>0) {
		filesize=lowlevel_filesize(sourcefile,filesize);
		fsblocksize=lowlevel_blocksize(sourcefile,fsblocksize);
		fprintf(stdout,"Reported low level blocksize: %lu\n",fsblocksize);
	}

	if (filesize!=0) {
		fprintf(stdout,"File size: %llu\n",filesize);
	} else {
		fprintf(stderr,"Filesize not reported by stat(), trying seek().\n");
		source=open(sourcefile,O_RDONLY | syncmode);
		if (source) {
			filesize=lseek(source,0,SEEK_END);
			close(source);
		}
		if (filesize<=0) {
			filesize=0;
			fprintf(stderr,"Unable to determine input file size.\n");
		} else {
			fprintf(stdout,"File size: %llu\n",filesize);
		}
	}
	
	tmp=blocksizestring;
	if (arglist_arggiven(carglist,"-b")==0) {
		blocksizestring=arglist_parameter(carglist,"-b",0);
	}
	blocksize=parseoption(blocksizestring,fsblocksize,filesize,tmp);
	if (blocksize<1) blocksize=fsblocksize;
	if (blocksize>MAXBLOCKSIZE) blocksize=MAXBLOCKSIZE;
	fprintf(stdout,"Blocksize: %llu\n",blocksize);

	tmp=faultblocksizestring;
	if (arglist_arggiven(carglist,"-f")==0) {
		faultblocksizestring=arglist_parameter(carglist,"-f",0);
	}
	faultblocksize=parseoption(faultblocksizestring,blocksize,filesize,tmp);
	if (faultblocksize<blocksize) faultblocksize=blocksize;
	if (faultblocksize>MAXBLOCKSIZE) faultblocksize=MAXBLOCKSIZE;
	fprintf(stdout,"Fault skip blocksize: %llu\n",faultblocksize);

	tmp=resolutionstring;
	if (arglist_arggiven(carglist,"-r")==0) {
		resolutionstring=arglist_parameter(carglist,"-r",0);
	}
	resolution=parseoption(resolutionstring,blocksize,filesize,tmp);
	if (resolution<1) resolution=1;
	if (resolution>faultblocksize) resolution=faultblocksize;
	fprintf(stdout,"Resolution: %llu\n",resolution);
	
	retries=retriesdef;
	if (arglist_arggiven(carglist,"-R")==0) {
		retries=arglist_integer(arglist_parameter(carglist,"-R",0));
	}
	if (retries<1) retries=1;
	fprintf(stdout,"Min read attempts: %u\n",retries);

	seeks=headmovedef;
	if (arglist_arggiven(carglist,"-Z")==0) {
		seeks=arglist_integer(arglist_parameter(carglist,"-Z",0));
	}
	if (seeks<0) seeks=0;
	fprintf(stdout,"Head moves on read error: %i\n",seeks);

	iblocksize=blocksize;
	if (arglist_arggiven(carglist,"-i")==0) {
		iblocksize=arglist_integer(arglist_parameter(carglist,"-i",0));
	}
	if (iblocksize<1 || iblocksize>MAXBLOCKSIZE) {
		fprintf(stderr,"Error: Invalid blocksize given for bad block include file!\n");
		arglist_kill(carglist);
		return 2;
	}

	incremental=0;
	if (arglist_arggiven(carglist,"-I")==0) {
		bblocksinstring=arglist_parameter(carglist,"-I",0);
	}
	if (bblocksinstring!=NULL) {
		incremental=1;
		bblocksinfile=bblocksinstring;
		fprintf(stdout,"Incremental mode file: %s\nIncremental mode blocksize: %llu\n",bblocksinfile,iblocksize);
	}

	xblocksize=blocksize;
	if (arglist_arggiven(carglist,"-x")==0) {
		xblocksize=arglist_integer(arglist_parameter(carglist,"-x",0));
	}
	if (xblocksize<1 || xblocksize>MAXBLOCKSIZE) {
		fprintf(stderr,"Error: Invalid blocksize given for bad block exclude file!\n");
		arglist_kill(carglist);
		return 2;
	}

	excluding=0;
	if (arglist_arggiven(carglist,"-X")==0) {
		xblocksinstring=arglist_parameter(carglist,"-X",0);
	}
	if (xblocksinstring!=NULL) {
		excluding=1;
		xblocksinfile=xblocksinstring;
		fprintf(stdout,"Exclusion mode file: %s\nExclusion mode blocksize: %llu\n",xblocksinfile,xblocksize);
	}

	bblocksoutfile=NULL;
	if (arglist_arggiven(carglist,"-o")==0) {
		bblocksoutstring=arglist_parameter(carglist,"-o",0);
	}
	if (bblocksoutstring!=NULL) {
		bblocksoutfile=bblocksoutstring;
		fprintf(stdout,"Badblocks output: %s\n",bblocksoutfile);
	}

	seekscriptfile=NULL;
	if (arglist_arggiven(carglist,"-S")==0) {
		seekscriptfile=arglist_parameter(carglist,"-S",0);
		fprintf(stdout,"Seek script (fallback): %s\n",seekscriptfile);
	}

	if (arglist_arggiven(carglist,"-M")==0) {
		failuredefstring=arglist_parameter(carglist,"-M",0);
		if (failuredefstring==NULL) failuredefstring="";
	}
	if (failuredefstring!=NULL) {
		marker=failuredefstring;
		fprintf(stdout,"Marker string: %s\n",marker);
	}

	startoffset=0;
	if (arglist_arggiven(carglist,"-s")==0) {
		startoffset=arglist_integer(arglist_parameter(carglist,"-s",0));
	}
	if (startoffset<1) startoffset=0;
	fprintf(stdout,"Starting block: %llu\n",startoffset);
	
	length=0;
	if (arglist_arggiven(carglist,"-l")==0) {
		length=arglist_integer(arglist_parameter(carglist,"-l",0));
	}
	if (length<1) length=-1;
	if (length>=0) {
		fprintf(stdout,"Size limit (blocks): %llu\n",length);
	}
	startoffset=startoffset*blocksize;
	length=length*blocksize;
	if (filesize==0 && length>0) {
		filesize=startoffset+length;
	}

	databuffer=(char*)malloc((blocksize+1)*sizeof(char));
	if (databuffer==NULL) {
		perror("MEMORY ALLOCATION ERROR!\nCOULDNT ALLOCATE MAIN BUFFER");
		return 2;
	}

// 3.opening io handles
		
	//open files
	fprintf(stdout,"Source: %s\nDestination: %s\n",sourcefile,destfile);
	source=open(sourcefile,O_RDONLY | O_NONBLOCK | syncmode );
	if (source==-1) {
		fprintf(stderr,"Error opening sourcefile: %s",sourcefile);
		perror("");
		if (human) usage(argv[0]);
		arglist_kill(carglist);
		return 2;
	}
	if (excluding==1) {
		xblocksin=fopen(xblocksinfile,"r");
		if (xblocksin==NULL) {
			close(source);
			fprintf(stderr,"Error opening exclusion badblock file for reading: %s",xblocksinfile);
			perror("");
			arglist_kill(carglist);
			return 2;
		}
	}
	targetsize=0;
	if (incremental==1) {
		bblocksin=fopen(bblocksinfile,"r");
		if (bblocksin==NULL) {
			close(source);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening badblock file for reading: %s",bblocksinfile);
			perror("");
			arglist_kill(carglist);
			return 2;
		}
		destination=open(destfile,O_WRONLY,0666 );
		if (destination==-1) {
			close(source);
			fclose(bblocksin);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening destination: %s",destfile);
			perror("");
			if (human) usage(argv[0]);
			arglist_kill(carglist);
			return 2;
		}
		// try to complete incomplete (aborted) safecopies by comparing file sizes
		if (!fstat(destination,&filestatus)) {
			targetsize=filestatus.st_size;
		}
		if (!targetsize) {
			fprintf(stderr,"Destination filesize not reported by stat(), trying seek().\n");
			targetsize=lseek(destination,0,SEEK_END);
			if (targetsize<0) targetsize=0;
		}
		if (!targetsize) {
			fprintf(stderr,"Error determining destination file size, cannot resume!");
		} else {
			fprintf(stdout,"Current destination size: %llu\n",targetsize);
		}
	} else {
		destination=open(destfile,O_WRONLY | O_TRUNC | O_CREAT,0666 );
		if (destination==-1) {
			close(source);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening destination: %s",destfile);
			perror("");
			if (human) usage(argv[0]);
			arglist_kill(carglist);
			return 2;
		}
	}
	if (bblocksoutfile!=NULL) {
		bblocksout=open(bblocksoutfile,O_WRONLY | O_TRUNC | O_CREAT,0666);
		if (bblocksout==-1) {
			close(source);
			close(destination);
			if (incremental==1) fclose(bblocksin);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening badblock file for writing: %s",bblocksoutfile);
			perror("");
			arglist_kill(carglist);
			return 2;
		}
	}

// 4.initialisations

	// setting signal handler
	signal(SIGINT, signalhandler);

	// initialise all vars
	readposition=0;	//current wanted reading position in sourcefile relative to startoffset
	writeposition=0; //current writing position in destination file
	block=-1; //amount of bytes read from the current reading position, negative values indicate errors
	remain=0; //remainder if a read operation read less than blocksize bytes
	writeremain=0; //remainder if a write operation wrote less than blocksize bytes
	softerr=0; //counter for recovered read errors
	harderr=0; //counter for unrecoverable read errors
	counter=1; //counter for visual output - a dot is printed all 1024 blocks
	newerror=retries; //counter for repeated read attempts on the current block - zero indicates
		//we are currently dealing with an unrecoverable error and need to find the end of the bad area
	newsofterror=0; //flag that indicates previous read failures on the current block
	lasterror=0; //address of the last encountered bad area in source file
	lastgood=0; //address of the last encountered succesfull read in source file
	lastmarked=0; //last known address already marked in output file
	lastbadblock=-1; //most recently encountered block for output badblock file
	lastxblock=-1; //most recently encountered block number from input exclude file
	previousxblock=-1; //2nd most recently encountered block number from input exclude file
	lastsourceblock=-1; //most recently encountered block number from input badblock file
	damagesize=0; //counter for size of unreadable source file data
	backtracemode=0; //flag that indicates safecopy is searching for the end of a bad area
	percent=-1; //current progress status, relative to source file size
	oldpercent=-1; //previously output percentage, needed to indicate required updates
	oldelapsed=0; //recent average time period for reading one block
	oldcategory=timecategory(oldelapsed);
		//timecategories translate raw milliseconds into human readable classes
	elapsed=0; //time elapsed while reading the current block
	output=0; //flag indicating that output (smily, percentage, ...) needs to be updated
	linewidth=0; //counter for x position in text output on terminal
	sposition=0; //actual file pointer position in sourcefile
	seekable=1; //flag that sourcefile allows seeking
	desperate=0; //flag set to indicate required low level source device access

// 5.dynamic initialisations and tests

	// attempt to seek to start position to find out wether source file is seekable
	cposition=lseek(source,startoffset,SEEK_SET);
	if (cposition<0) {
		perror("Warning: Input file is not seekable");
		seekable=0;
		close(source);
		cposition=emergency_seek(startoffset,0,blocksize,seekscriptfile);
		if (cposition>=0) {
			sposition=cposition;
		}
		source=open(sourcefile,O_RDONLY | O_NONBLOCK | syncmode);
		if (source==-1) {
			perror("Error reopening sourcefile after external seek");
			close(destination);
			if (incremental==1) fclose(bblocksin);
			if (excluding==1) fclose(xblocksin);
			if (bblocksoutfile!=NULL) close(bblocksout);
			arglist_kill(carglist);
			return 2;
		}
		fprintf(stdout,"|/|");
	} else {
		sposition=cposition;
		seekable=1;
	}
	
// 6.main io loop

	fflush(stdout);
	fflush(stderr);
	// main data loop. Continue until all data has been read or CTRL+C has been pressed
	while (!wantabort && block!=0 && (readposition<length || length<0)) {

// 6.a planning - calculate wanted read position based on include/exclude input files

		debug(DEBUG_FLOW,"debug: start of cycle\n");
		// start with a whole new block if we finnished the old
		if (remain==0) {
			debug(DEBUG_FLOW,"debug: preparing to read a new block\n");
			// if necessary, repeatedly calculate include and exclude blocks
			// (for example if we seek to a new include block, but then exclude it,
			// so we seek to the next, then exclude it, etc)
			if (incremental && newerror!=0) {
				debug(DEBUG_INCREMENTAL,"debug: incremental - searching next block\n");
				// Incremental mode. Skip over unnecessary areas.
				// check wether the current block is in the badblocks list, 
				// if so, proceed as usual,
				// otherwise seek to the next badblock in input
				tmp_pos=(readposition+startoffset)/iblocksize;
				if (tmp_pos>lastsourceblock) {
					tmp=NULL;
					do {
						tmp=fgets(textbuffer,64,bblocksin);
						if (sscanf(textbuffer,"%llu",&lastsourceblock)!=1) tmp=NULL;
					} while (tmp!=NULL && lastsourceblock<tmp_pos );
					if (tmp==NULL) {
						// no more bad blocks in input file
						// if exists
						if ((readposition+startoffset)<targetsize) {
							// go to end of target file for resuming
							lastsourceblock=targetsize/iblocksize;
						} else if (targetsize) {
							lastsourceblock=tmp_pos;
						} else {
							// othewise end immediately
							remain=0;
							break;
						}
					}
					debug(DEBUG_INCREMENTAL,"debug: incremental - target is %llu position is %llu\n",lastsourceblock,tmp_pos);
					readposition=(lastsourceblock*iblocksize)-startoffset;
				} else {
					debug(DEBUG_INCREMENTAL,"debug: incremental - still in same block\n");
				}
			}
			// make sure any misalignment to block boundaries get corrected asap
			// be sure to use skipping size when in bad area
			if (newerror==0) {
				remain=(((readposition/blocksize)*blocksize)+faultblocksize)-readposition;
			} else {
				remain=(((readposition/blocksize)*blocksize)+blocksize)-readposition;
			}
			debug(DEBUG_FLOW,"debug: prepared to read block %lli size %llu at %llu\n",(readposition+startoffset)/blocksize,remain,readposition+startoffset);
		}
		if (excluding && remain!=0) {
			// Exclusion mode, check relevant exclude sectors whether they affect the current position
			// first check if we need to backtrack in exclude file.
			debug(DEBUG_EXCLUDE,"debug: checking for exclude blocks at position %llu\n",readposition+startoffset);
			if (readposition+startoffset<lastxblock*xblocksize) {
				debug(DEBUG_EXCLUDE,"debug: possibly need backtracking in exclude list, next exclude block %lli\n",lastxblock);
				if (readposition+startoffset<previousxblock*xblocksize) {
					// we read too far in exclude block file, probably after backtracking
					// close exclude file and reopen
					debug(DEBUG_EXCLUDE,"debug: reopening exclude file and reading from the start\n");
					fclose(xblocksin);
					xblocksin=fopen(xblocksinfile,"r");
					if (xblocksin==NULL) {
						excluding=0;
						fprintf(stderr,"Error reopening exclusion badblock file for reading: %s",xblocksinfile);
						perror("");
						wantabort=1;
						break;
					}
					lastxblock=-1;
					previousxblock=-1;
				} else if((previousxblock+1)*xblocksize>readposition+startoffset) {
					// backtrack just one exclude block
					lastxblock=previousxblock;
					debug(DEBUG_EXCLUDE,"debug: using last exclude block %lli\n",lastxblock);
				} else {
					debug(DEBUG_EXCLUDE,"debug: false alarm, current exclude block is fine\n");
				}
			}
			tmp_pos = lastxblock*xblocksize;
			debug(DEBUG_EXCLUDE,"debug: checking with xblock %lli at %llu\n",lastxblock,tmp_pos);
			tmp_bytes=0; // using this as indicator for restart
			while (tmp_pos<readposition+startoffset+remain) {
				if (tmp_pos+xblocksize>readposition+startoffset) {
					if (tmp_pos<=readposition+startoffset) {
						// start of current block is covered by exclude block. skip.
						debug(DEBUG_EXCLUDE,"debug: skipping ahead to avoid exclude block\n");
						tmp_bytes=1;
						if (tmp_pos+xblocksize<readposition+startoffset+remain) {
							remain=(tmp_pos+xblocksize)-(readposition+startoffset);
							debug(DEBUG_EXCLUDE,"debug: remain set to %llu\n",remain);
						} else {
							if (!backtracemode) {
								remain=0;
							} else {
								remain=1;
								debug(DEBUG_EXCLUDE,"debug: remain set to %llu\n",remain);
							}
						}
						readposition=(tmp_pos+xblocksize)-startoffset;
						break;
					} else if (tmp_pos<readposition+startoffset+remain) {
						debug(DEBUG_EXCLUDE,"debug: shrinking block size because of exclude block from %llu to %llu\n",remain,(tmp_pos-(readposition+startoffset)));
						remain=tmp_pos-(readposition+startoffset);
						break;
					} else {
						// start of exclude block is beyond end of our area. we are done
						break;
					}
				} else {
					// read next exclude block
					debug(DEBUG_EXCLUDE,"debug: reading another exclude block\n");
					tmp=fgets(textbuffer,64,xblocksin);
					if (sscanf(textbuffer,"%llu",&tmp_pos)!=1) tmp=NULL;
					if (tmp==NULL) {
						// no more bad blocks in input file
						break;
					}
					previousxblock=lastxblock;
					lastxblock=tmp_pos;
					tmp_pos=lastxblock*xblocksize;
					debug(DEBUG_EXCLUDE,"debug: reading another exclude block: %lli at %llu\n",lastxblock,tmp_pos);
				}
			}
			if (tmp_bytes==1) {
				debug(DEBUG_EXCLUDE,"debug: recalculation needed because of exclude blocks, restarting cycle\n");
				continue;
			}
		}

// 6.b navigation - attempt to seek to requested input file position and find out actual position

		// seek and read - timed
		gettimeofday(&oldtime,NULL);
		// seek only if the current file position differs from requested file position
		if (sposition!=readposition+startoffset) {
			debug(DEBUG_SEEK,"debug: seeking in input from %llu to %llu\n",sposition,readposition+startoffset);
			cposition=lseek(source,readposition+startoffset,SEEK_SET);
			if (cposition>0) {
				sposition=cposition;
				seekable=1;
			} else {
				// seek failed, check why
				errtmp=errno;
				if ((readposition+startoffset)<filesize && lowlevel_canseek() && (lowlevel==2 || (lowlevel==1 && desperate))) {
					// lowlevel operation will take care of seeking.
					// and can do it
					cposition=readposition+startoffset;
					sposition=cposition;
				} else if (errtmp==EINVAL && seekable==1) {
					// tried to seek past the end of file.
					break;
				} else {
					// input file is not seekable!
					if (seekable) {
						write(1,&"|/|",3);
						seekable=0;
					}
					if (readposition+startoffset>sposition) {
						// emergency seek will only handle positive seeks
						// close input file for seek/skip
						close (source);
						cposition=emergency_seek(startoffset+readposition,sposition,blocksize,seekscriptfile);
						if (cposition<0 && newerror==0) {
							// bail if we cannot skip over hard errors!
							fprintf(stderr,"\nError: Unable to skip over bad area.\n");
							break;
						}
						// reopen input file
						source=open(sourcefile,O_RDONLY | O_NONBLOCK | syncmode );
						if (source==-1) {
							perror("\nError reopening sourcefile after external seek");
							close(destination);
							if (incremental==1) fclose(bblocksin);
							if (excluding==1) fclose(xblocksin);
							if (bblocksoutfile!=NULL) close(bblocksout);
							arglist_kill(carglist);
							return 2;
						}
						if (cposition>=0) {
							sposition=cposition;
						}
					}
				}
			}
		}
		// prevent negative write offsets
		if (sposition>startoffset) {
			readposition=sposition-startoffset;
		} else {
			readposition=0;
		}
		// make sure not to read beyond the specified end
		if (length>=0) {
			if (readposition>=length) readposition=length;
			if (readposition+remain>=length) remain=length-readposition;
		}
		// write where we read
		writeposition=readposition;
		if (filesize>startoffset) {
			percent=(100*(readposition))/(filesize-startoffset);
		}

// 6.c patience - wait for availability of data

		// select for reading. Have a fallback output in case of timeout.
		do {
			newtime.tv_sec=10;
			newtime.tv_usec=0;
			FD_ZERO(&rfds);
			FD_ZERO(&efds);
			FD_SET(source,&rfds);
			FD_SET(source,&efds);
			select(source+1,&rfds,NULL,&efds,&newtime);
			if (! ( FD_ISSET(source,&rfds))) {
				desperate=1;
				if (human) {
					if (filesize) {
						printpercentage(percent);
					}
					printtimecategory(VERY_VERY_SLOW);
				}
			}
			if (wantabort) break;
		} while (! ( FD_ISSET(source,&rfds) || FD_ISSET(source,&efds)));
		if (wantabort) break;
// 6.d input - attempt to read from sourcefile
		// read input data
		// to limit memory usage do not allow to read chunks bigger
		// than one block, even if faultskipsize is set
		maxremain=remain;
		if (maxremain>blocksize) maxremain=blocksize;
		if (lowlevel==0 || (lowlevel==1 && !desperate)) {
			debug(DEBUG_IO,"debug: normal read\n");
			block=read(source,databuffer,maxremain);
		} else {
			//desperate mode means we are allowed to use low lvl 
			//IO syscalls to work around read errors
			debug(DEBUG_IO,"debug: low level read\n");
			block=read_desperately(sourcefile,&source,databuffer,sposition,maxremain,seekable,desperate,syncmode);
		}
		// time reading for quality calculation
		gettimeofday(&newtime,NULL);
		elapsed=timediff(oldtime,newtime);

// 6.e feedback - calculate and display user feedback information

		// smooth times, react sensitive to high times
		if (timecategory(elapsed)>timecategory(oldelapsed)) {
			oldelapsed=(((9*oldelapsed)+elapsed)/10);
		} else if (timecategory(elapsed)<timecategory(oldelapsed)) {
			oldelapsed=(((99*oldelapsed)+elapsed)/100);
		}
		// update percentage if changed
		if (filesize && ( percent!=oldpercent || output)) {
			if (human) {
				printpercentage(percent);
				printtimecategory(timecategory(oldelapsed));
			}
			oldpercent=percent;
		}
		// update quality if changed
		if (timecategory(oldelapsed)!=oldcategory) {
			if (human) {
				if (filesize) printpercentage(percent);
				printtimecategory(timecategory(oldelapsed));
			}
			oldcategory=timecategory(oldelapsed);
		} else if (output && human) {
			// or if any output has taken place
			if (filesize) printpercentage(percent);
			printtimecategory(timecategory(oldelapsed));
		}
		// break lines after 40 chars to prevent backspace bug of terminals
		if (linewidth>40) {
			if (human) {
				tmp_pos=readposition/blocksize;
				sprintf(textbuffer," [%lli]    \n",tmp_pos);
				write(2,textbuffer,strlen(textbuffer));
			}
			linewidth=0;
		}
		output=0;

// 6.f processing - react according to result of read operation
// 6.f.1 succesfull read:
		if (block>0) {
			debug(DEBUG_IO,"debug: succesfull read\n");
			sposition=sposition+block;
			// successfull read, if happening during soft recovery
			// (downscale or retry) list a single soft error
			if (newsofterror==1) {
				newsofterror=0;
				softerr++;
			}
			// read successfull, test for end of damaged area
			if (newerror==0) {
// 6.f.1.a attempt to backtrack for readable data prior to current position or...
				// we are in recovery since we just read past the end of a damaged area
				// so we go back until the beginning of the readable area is found (if possible)

				if (remain>resolution && seekable) {
				 	remain=remain/2;
					readposition-=remain;
					write(1,&"<",1);
					output=1;
					linewidth++;
					backtracemode=1;
					debug(DEBUG_FLOW,"debug: shrinking remain to %llu\n",remain);
				} else {
					newerror=retries;
					remain=0;
					tmp_pos=readposition/blocksize;
					tmp_bytes=readposition-lastgood;
					damagesize+=tmp_bytes;
					sprintf(textbuffer,"}[%llu](+%llu)",tmp_pos,tmp_bytes);
					write(1,textbuffer,strlen(textbuffer));
					if (human) write(2,&"\n",1);
					output=1;
					linewidth=0;
					backtracemode=0;
					markoutput("end of bad area",readposition,lasterror,MARKOUTPUT_PARAMS);
					lasterror=readposition;
				}
				
			} else {
// 6.f.1.b write to output data file
				//disable desperate mode (use normal high lvl IO)
				desperate=0;
				newerror=retries;
				if (block<remain) {
					// not all data we wanted got read, note that
					write(1,&"_",1);
					output=1;
					linewidth++;
					counter=1;
				} else if (--counter<=0) {
					write(1,&".",1);
					output=1;
					linewidth++;
					counter=1024;
				}

				remain-=block;
				readposition+=block;
				writeremain=block;
				writeoffset=0;
				if (sposition<startoffset) {
					// handle cases where unwanted data has been read doe to seek error
					if ((sposition+block)<=startoffset) {
						// we read data we are supposed to skip! Ignore.
						writeremain=0;
					} else {
						// partial skip
						writeremain=(sposition+block)-startoffset;
						writeoffset=startoffset-sposition;
					}
				}
				while (writeremain>0) {
					// write data to destination file
					debug(DEBUG_SEEK,"debug: seek in destination file: %llu\n",writeposition);
					cposition=lseek(destination,writeposition,SEEK_SET);
					if (cposition<0) {
						fprintf(stderr,"\nError: seek() in %s failed",destfile);
						perror("");
						close(destination);
						close(source);
						if (incremental==1) fclose(bblocksin);
						if (excluding==1) fclose(xblocksin);
						if (bblocksoutfile!=NULL) close(bblocksout);
						arglist_kill(carglist);
						return 2;
					}
					debug(DEBUG_IO,"debug: writing data to destination file: %llu bytes at %llu\n",writeremain,cposition);
					writeblock=write(destination,databuffer+writeoffset,writeremain);
					if (writeblock<=0) {
						fprintf(stderr,"\nError: write to %s failed",destfile);
						perror("");
						close(destination);
						close(source);
						if (incremental==1) fclose(bblocksin);
						if (excluding==1) fclose(xblocksin);
						if (bblocksoutfile!=NULL) close(bblocksout);
						arglist_kill(carglist);
						return 2;
					}
					writeremain-=writeblock;
					writeoffset+=writeblock;
					writeposition+=writeblock;
				}
			}
		} else if (block<0) {
// 6.f.2 failed read
			debug(DEBUG_IO,"debug: read failed\n");
			// operation failed
			counter=1;
			// use low level IO for error correction if allowed
			desperate=1;
// 6.f.2.a try again or...
			if (remain > resolution && newerror>0) {
				// start of a new erroneous area - decrease readsize in
				// case we can read partial data from the beginning
				newsofterror=1;
				remain=remain/2;
				write(1,&">",1);
				linewidth++;
				output=1;
			} else {
				if (newerror>1) {
					// if we are at minimal size, attempt a couple of retries
					newsofterror=1;
					newerror--;
					write(1,&"!",1);
					linewidth++;
					output=1;
				} else {
// 6.f.2.b skip over bad area
					// readsize is already minimal, out of retry attempts
					// unrecoverable error, go one sector ahead and try again there 

					if (newerror==1) {
						// if this is still the start of a damaged area,
						// also print the amount of successfully read sectors
						newsofterror=0;
						newerror=0;
						tmp_pos=readposition/blocksize;
						tmp_bytes=readposition-lasterror;
						sprintf(textbuffer,"[%llu](+%llu){",tmp_pos,tmp_bytes);
						write(1,textbuffer,strlen(textbuffer));
						output=1;
						linewidth+=strlen(textbuffer);
						// and we set the read size high enough to go over the damaged area quickly
						// (next block boundary)
						remain=(((readposition/blocksize)*blocksize)+faultblocksize)-readposition;
						lastgood=readposition;
					} 

					if (!backtracemode) {
						harderr++;
						write(1,&"X",1);
						output=1;
						linewidth++;
						markoutput("continuous bad area",readposition,(lasterror>lastgood?lasterror:lastgood),MARKOUTPUT_PARAMS);
						lasterror=readposition;
					}

					// skip ahead(virtually)
					readposition+=remain;
					if (!backtracemode) {
						// force re-calculation of next blocksize to fix
						// block misalignment caused by partial data reading.
						// doing so during backtrace would cause an infinite loop.
						remain=0;
					} else {
						debug(DEBUG_FLOW,"debug: bad block during backtrace - remain is %llu at %llu\n",remain,readposition+startoffset);
					}

				}

			}

// 6.f.2.c close and reopen source file
			// reopen source file to clear possible error flags preventing us from getting more data
			close (source);
			// do some forced seeks to move head around.
			for (cseeks=0;cseeks<seeks;cseeks++) {
				debug(DEBUG_SEEK,"debug: forced head realignment\n");
				source=open(sourcefile,O_RDONLY|O_RSYNC );
				if (source) {
					lseek(source,0,SEEK_SET);
					read(source,&textbuffer,1);
					close(source);
				}
				source=open(sourcefile,O_RDONLY|O_RSYNC );
				if (source) {
					lseek(source,-blocksize,SEEK_END);
					read(source,&textbuffer,1);
					close(source);
				}
				if (wantabort) break;
			}
			source=open(sourcefile,O_RDONLY | O_NONBLOCK | syncmode );
			if (source==-1) {
				perror("\nError reopening sourcefile after read error");
				close(destination);
				if (incremental==1) fclose(bblocksin);
				if (excluding==1) fclose(xblocksin);
				if (bblocksoutfile!=NULL) close(bblocksout);
				arglist_kill(carglist);
				return 2;
			}
			if (seekable) {
				// in seekable input, a re-opening sets the pointer to zero
				// we must reflect that.
				sposition=0;
			}
		}
	}

// 7.closing and finalisation

	debug(DEBUG_FLOW,"debug: main loop ended\n");
	fflush(stdout);
	fflush(stderr);
	if (newerror==0) {
		// if theres an error at the end of input, treat as if we had one succesfull read afterwards
		tmp_pos=readposition/blocksize;
		tmp_bytes=readposition-lastgood;
		damagesize+=tmp_bytes;
		sprintf(textbuffer,"}[%llu](+%llu)",tmp_pos,tmp_bytes);
		write(1,textbuffer,strlen(textbuffer));
		if (human) write(2,&"\n",1);
		// mark badblocks in output if not aborted manually
		if (filesize && lastgood+startoffset<filesize && readposition+startoffset>=filesize) {
			markoutput("end of file - filling to filesize",filesize,lastgood,MARKOUTPUT_PARAMS);
		} else {
			markoutput("end of file - filling to last seen position",readposition+startoffset,lastgood,MARKOUTPUT_PARAMS);
		}
	}
	if (wantabort) {
		fprintf(stdout,"\nAborted!\n");
	} else {
		fprintf(stdout,"\nDone!\n");
	}
	fprintf(stdout,"Recovered bad blocks: %llu\n",softerr);
	fprintf(stdout,"Unrecoverable bad blocks (bytes): %llu (%llu)\n",harderr,damagesize);
	fprintf(stdout,"Blocks (bytes) copied: %llu (%llu)\n",(writeposition)/blocksize,writeposition);

	close(destination);
	close(source);
	if (incremental==1) fclose(bblocksin);
	if (excluding==1) fclose(xblocksin);
	if (bblocksoutfile!=NULL) close(bblocksout);
	arglist_kill(carglist);
	return(0);
}
