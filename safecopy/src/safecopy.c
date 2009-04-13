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
#include "arglist.h"

#define MAXBLOCKSIZE 1048576
#define BLOCKSIZE 4096
#define RETRIES 3
#define SEEKS 1
#define LOWLEVELMODE 1

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

void usage(char * name) {
	fprintf(stderr,"Safecopy "VERSION" by CorvusCorax\n");
	fprintf(stderr,"Usage: %s [options] <source> <target>\n",name);
	fprintf(stderr,"Options:\n");
	fprintf(stderr,"	-b <bytes> : Blocksize in bytes for default read operations.\n");
	fprintf(stderr,"	             Set this to the physical sectorsize of your media.\n");
	fprintf(stderr,"	             Default: Driver blocksize of input device,\n");
	fprintf(stderr,"	                      if determinable, otherwise %i\n",BLOCKSIZE);
	fprintf(stderr,"	-f <bytes> : Blocksize in bytes when skipping over badblocks.\n");
	fprintf(stderr,"	             Higher settings put less strain on your hardware,\n");
	fprintf(stderr,"	             But you might miss good areas in between two bad ones.\n");
	fprintf(stderr,"	             Default: Blocksize as in -b times 16\n");
	fprintf(stderr,"	-r <bytes> : Resolution in bytes when searching for the exact\n");
	fprintf(stderr,"	             beginning or end of a bad area.\n");
	fprintf(stderr,"	             If you read data directly from a device there is no\n");
	fprintf(stderr,"	             need to set this lower than the hardware blocksize.\n");
	fprintf(stderr,"	             On mounted filesystems however, read blocks\n");
	fprintf(stderr,"	             and physical blocks could be misaligned.\n");
	fprintf(stderr,"	             Smaller values lead to very thorough attempts to read\n");
	fprintf(stderr,"	             data at the edge of damaged areas,\n");
	fprintf(stderr,"	             but increase the strain on the damaged media.\n");
	fprintf(stderr,"	             Default: Blocksize as in -b\n");
	fprintf(stderr,"	-R <number> : At least that many read attempts are made on the first\n");
	fprintf(stderr,"	              bad block of a damaged area with minimum resolution.\n");
	fprintf(stderr,"	              More retries can sometimes recover a weak sector,\n");
	fprintf(stderr,"	              but at the cost of additional strain.\n");
	fprintf(stderr,"	              Default: %i\n",RETRIES);
	fprintf(stderr,"	-Z <number> : On each error, force seek the read head from start to\n");
	fprintf(stderr,"	              end of the source device as often as specified.\n");
	fprintf(stderr,"	              That takes time, creates additional strain and might\n");
	fprintf(stderr,"	              not be supported by all devices or drivers.\n");
	fprintf(stderr,"	              Default: %i\n",SEEKS);
	fprintf(stderr,"	-L <mode> : Use low level device calls as specified:\n");
	fprintf(stderr,"	                   0  Do not use low level device calls\n");
	fprintf(stderr,"	                   1  Attempt low level device calls\n");
	fprintf(stderr,"	                      for error recovery only\n");
	fprintf(stderr,"	                   2  Always use low level device calls\n");
	fprintf(stderr,"	                      if available\n");
	fprintf(stderr,"	            Supported low level features in this version are:\n");
	fprintf(stderr,"	                SYSTEM  DEVICE TYPE   FEATURE\n");
	fprintf(stderr,"	                Linux   cdrom/dvd     bus/device reset\n");
	fprintf(stderr,"	                Linux   cdrom         read sector in raw mode\n");
	fprintf(stderr,"	                Linux   floppy        controller reset, twaddle\n");
	fprintf(stderr,"	            Default: %i\n",LOWLEVELMODE);
	fprintf(stderr,"	--sync : Use synchronized read calls (disable driver buffering)\n");
	fprintf(stderr,"	         Default: Asynchronous read buffering by the OS is allowed\n");
	fprintf(stderr,"	-s <blocks> : Start position where to start reading.\n");
	fprintf(stderr,"	              Will correspond to position 0 in the destination file.\n");
	fprintf(stderr,"	              Default: block 0\n");
	fprintf(stderr,"	-l <blocks> : Maximum length of data to be read.\n");
	fprintf(stderr,"	              Default: Entire size of input file\n");
	fprintf(stderr,"	-I <badblockfile> : Incremental mode. Assume the target file already\n");
	fprintf(stderr,"	                    exists and has holes specified in a badblockfile.\n");
	fprintf(stderr,"	                    It will be attempted to retrieve more data from\n");
	fprintf(stderr,"	                    the missing areas only.\n");
	fprintf(stderr,"	                    Default: none\n");
	fprintf(stderr,"	-i <bytes> : Blocksize to interpret the badblockfile given with -I.\n");
	fprintf(stderr,"	             Default: Blocksize as specified by -b\n");
	fprintf(stderr,"	-X <badblockfile> : Exclusion mode. Do not attempt to read blocks in\n");
	fprintf(stderr,"	                    badblockfile. If used together with -I,\n");
	fprintf(stderr,"	                    excluded blocks override included blocks.\n");
	fprintf(stderr,"	                    Default: none\n");
	fprintf(stderr,"	-x <bytes> : Blocksize to interpret the badblockfile given with -X.\n");
	fprintf(stderr,"	             Default: Blocksize as specified by -b\n");
	fprintf(stderr,"	-o <badblockfile> : Write a badblocks/e2fsck compatible bad block file.\n");
	fprintf(stderr,"	                    Default: none\n");
	fprintf(stderr,"	-S <seekscript> : Use external script for seeking in input file.\n");
	fprintf(stderr,"	                  (Might be useful for tape devices and similar).\n");
	fprintf(stderr,"	                  Seekscript must be an executable that takes the\n");
	fprintf(stderr,"	                  number of blocks to be skipped as argv1 (1-64)\n");
	fprintf(stderr,"	                  the blocksize in bytes as argv2\n");
	fprintf(stderr,"	                  and the current position (in bytes) as argv3.\n");
	fprintf(stderr,"	                  Return value needs to be the number of blocks\n");
	fprintf(stderr,"	                  successfully skipped, or 0 to indicate seek failure.\n");
	fprintf(stderr,"	                  The external seekscript will only be used\n");
	fprintf(stderr,"	                  if lseek() fails and we need to skip over data.\n");
	fprintf(stderr,"	                  Default: none\n");
	fprintf(stderr,"	-M <string> : Mark unrecovered data with this string instead of\n");
	fprintf(stderr,"	              skipping / zero-padding it. This helps in later\n");
	fprintf(stderr,"	              finding affected files on file system images\n");
	fprintf(stderr,"	              that couldn't be rescued completely.\n");
	fprintf(stderr,"	              Default: none\n");
	fprintf(stderr,"	-h | --help : Show this text\n\n");
	fprintf(stderr,"Description of output:\n");
	fprintf(stderr,"	. : Between 1 and 1024 blocks successfully read.\n");
	fprintf(stderr,"	_ : Read of block was incomplete. (possibly end of file)\n");
	fprintf(stderr,"	    The blocksize is now reduced to read the rest.\n");
	fprintf(stderr,"	|/| : Seek failed, source can only be read sequentially.\n");
	fprintf(stderr,"	> : Read failed, reducing blocksize to read partial data.\n");
	fprintf(stderr,"	! : A low level error on read attempt of smallest allowed size\n");
	fprintf(stderr,"	    leads to a retry attempt.\n");
	fprintf(stderr,"	[xx](+yy){ : Current block and number of bytes continuously\n");
	fprintf(stderr,"	             read successfully up to this point.\n");
	fprintf(stderr,"	X : Read failed on a block with minimum blocksize and is skipped.\n");
	fprintf(stderr,"	    Unrecoverable error, destination file is padded with zeros.\n");
	fprintf(stderr,"	    Data is now skipped until end of the unreadable area is reached.\n");
	fprintf(stderr,"	< : Successful read after the end of a bad area causes\n");
	fprintf(stderr,"	    backtracking with smaller blocksizes to search for the first\n");
	fprintf(stderr,"	    readable data.\n");
	fprintf(stderr,"	}[xx](+yy) : current block and number of bytes of recent\n");
	fprintf(stderr,"	             continuous unreadable data.\n\n");
	fprintf(stderr,"Copyright 2009, distributed under terms of the GPL\n\n");
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

// main
int main(int argc, char ** argv) {

// 1.declarations
	// commandline argument handler class
	struct arglist *carglist;
	// filenames
	char *sourcefile,*destfile,*bblocksinfile,*xblocksinfile,*bblocksoutfile,*seekscriptfile;
	// file descriptors
	int source,destination,bblocksout;
	// high level file descriptor
	FILE *bblocksin,*xblocksin;

	// file offset variables
	off_t readposition,cposition,sposition,writeposition;
	off_t startoffset,length,writeoffset;
	// variables for handling read/written sizes/remainders
	ssize_t remain,block,writeblock,writeremain;
	// pointer to main IO data buffer
	char * databuffer;
	// a buffer for output text
	char textbuffer[256];
	// buffer pointer for sfgets() 
	char *tmp;
	// pointer to marker string
	char *marker;
	// several local integer variables
	int blocksize,iblocksize,xblocksize,faultblocksize;
	int resolution,retries,seeks,cseeks;
	int incremental,excluding,lowlevel,syncmode;
	int counter,percent,oldpercent,newerror,newsofterror;
	int backtracemode,output,linewidth,seekable,desperate;
	// indicator wether stdin/stderr is a terminal - affects output
	int human=0;

	// error indicators and flags
	off_t softerr,harderr,lasterror,lastgood;
	// tmp vars for file offsets
	off_t tmp_pos,tmp_bytes;
	// variables to remember beginning and end of previous good/bad area
	off_t lastbadblock,lastxblock,lastsourceblock;
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

// 2.command line parsing

	// parse all commandline arguments
	carglist=arglist_new(argc,argv);
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

	// low level calls enabled?
	lowlevel=LOWLEVELMODE;
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
	blocksize=BLOCKSIZE;
	if(!stat(sourcefile,&filestatus)) {
		filesize=filestatus.st_size;
		if (filestatus.st_blksize) {
			fprintf(stdout,"Reported hw blocksize: %lu\n",filestatus.st_blksize);
			blocksize=filestatus.st_blksize;
		}
	}

	if (arglist_arggiven(carglist,"-b")==0) {
		blocksize=arglist_integer(arglist_parameter(carglist,"-b",0));
	}
	if (blocksize<1) blocksize=BLOCKSIZE;
	if (blocksize>MAXBLOCKSIZE) blocksize=MAXBLOCKSIZE;
	fprintf(stdout,"Blocksize: %lu\n",blocksize);

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
	
	resolution=blocksize;
	if (arglist_arggiven(carglist,"-r")==0) {
		resolution=arglist_integer(arglist_parameter(carglist,"-r",0));
	}
	if (resolution<1) resolution=1;
	if (resolution>blocksize) resolution=blocksize;
	fprintf(stdout,"Resolution: %u\n",resolution);

	faultblocksize=blocksize*16;
	if (arglist_arggiven(carglist,"-f")==0) {
		faultblocksize=arglist_integer(arglist_parameter(carglist,"-f",0));
	}
	if (faultblocksize<resolution) faultblocksize=resolution;
	if (faultblocksize>MAXBLOCKSIZE) faultblocksize=MAXBLOCKSIZE;
	fprintf(stdout,"Fault skip blocksize: %u\n",faultblocksize);
	
	retries=RETRIES;
	if (arglist_arggiven(carglist,"-R")==0) {
		retries=arglist_integer(arglist_parameter(carglist,"-R",0));
	}
	if (retries<1) retries=1;
	fprintf(stdout,"Min read attempts: %u\n",retries);

	seeks=SEEKS;
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
		incremental=1;
		bblocksinfile=arglist_parameter(carglist,"-I",0);
		fprintf(stdout,"Incremental mode file: %s\nIncremental mode blocksize: %lu\n",bblocksinfile,iblocksize);
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
		excluding=1;
		xblocksinfile=arglist_parameter(carglist,"-X",0);
		fprintf(stdout,"Exclusion mode file: %s\nExclusion mode blocksize: %lu\n",xblocksinfile,xblocksize);
	}

	bblocksoutfile=NULL;
	if (arglist_arggiven(carglist,"-o")==0) {
		bblocksoutfile=arglist_parameter(carglist,"-o",0);
		fprintf(stdout,"Badblocks output: %s\n",bblocksoutfile);
	}

	seekscriptfile=NULL;
	if (arglist_arggiven(carglist,"-S")==0) {
		seekscriptfile=arglist_parameter(carglist,"-S",0);
		fprintf(stdout,"Seek script (fallback): %s\n",seekscriptfile);
	}

	marker=NULL;
	if (arglist_arggiven(carglist,"-M")==0) {
		marker=arglist_parameter(carglist,"-M",0);
		fprintf(stdout,"Marker string: %s\n",marker);
	}

	startoffset=0;
	if (arglist_arggiven(carglist,"-s")==0) {
		startoffset=arglist_integer(arglist_parameter(carglist,"-s",0));
	}
	if (startoffset<1) startoffset=0;
	fprintf(stdout,"Starting block: %lu\n",startoffset);
	
	length=0;
	if (arglist_arggiven(carglist,"-l")==0) {
		length=arglist_integer(arglist_parameter(carglist,"-l",0));
	}
	if (length<1) length=-1;
	if (length>=0) {
		fprintf(stdout,"Size limit (blocks): %lu\n",length);
	}
	startoffset=startoffset*blocksize;
	length=length*blocksize;
	if (filesize==0 && length>0) {
		filesize=startoffset+length;
	}

	if (blocksize>=faultblocksize) {
		databuffer=(char*)malloc((blocksize+1)*sizeof(char));
	} else {
		databuffer=(char*)malloc((faultblocksize+1)*sizeof(char));
	}
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
	lastbadblock=-1; //most recently encountered block for output badblock file
	lastxblock=-1; //most recently encountered block number from input exclude file
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

		// start with a whole new block if we finnished the old
		if (remain==0) {
			do {
				// if necessary, repeatedly calculate include and exclude blocks
				// (for example if we seek to a new include block, but then exclude it,
				// so we seek to the next, then exclude it, etc)
				if (incremental) {
					// Incremental mode. Skip over unnecessary areas.
					// check wether the current block is in the badblocks list, 
					// if so, (or a read error condition) proceed as usual,
					// otherwise seek to the next badblock in input
					tmp_pos=(readposition+startoffset)/iblocksize;
					if (tmp_pos>lastsourceblock && newerror>0) {
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
						readposition=(lastsourceblock*iblocksize)-startoffset;
					}
				}
				if (excluding) {
					// Exclusion mode, read next new exclusion block
					// from badblocks file, if we are already read past it
					tmp_pos=(readposition+startoffset)/xblocksize;
					if (tmp_pos>lastxblock) {
						tmp=NULL;
						do {
							tmp=fgets(textbuffer,64,xblocksin);
							if (sscanf(textbuffer,"%llu",&lastxblock)!=1) tmp=NULL;
						} while (tmp!=NULL && lastxblock<tmp_pos );
						if (tmp==NULL) {
							// no more bad blocks in input file
							excluding=0;
							lastxblock=-1;
						}
					}
					if (tmp_pos==lastxblock) {
						// we have a match, clean up, then skip this block
						if (newerror==0) {
							// clean up
							newerror=retries;
							tmp_pos=readposition/blocksize;
							tmp_bytes=readposition-lastgood;
							damagesize+=tmp_bytes;
							sprintf(textbuffer,"}[%llu](+%llu)",tmp_pos,tmp_bytes);
							write(1,textbuffer,strlen(textbuffer));
							if (human) write(2,&"\n",1);
							output=1;
							linewidth=0;
							backtracemode=0;
							lasterror=readposition;
							// restore overwritten var
							tmp_pos=lastxblock;
						}
						readposition=((lastxblock+1)*xblocksize)-startoffset;
					}
				}
				// make sure any misalignment to block boundaries get corrected asap
				// be sure to use skipping size when in bad area
				if (newerror==0) {
					remain=(((readposition/blocksize)*blocksize)+faultblocksize)-readposition;
				} else {
					remain=(((readposition/blocksize)*blocksize)+blocksize)-readposition;
				}
			} while (excluding && tmp_pos==lastxblock);
			// break from above jumps here, with remain=0
		}

// 6.b navigation - attempt to seek to requested input file position and find out actual position

		// seek and read - timed
		gettimeofday(&oldtime,NULL);
		// seek only if the current file position differs from requested file position
		if (sposition!=readposition+startoffset) {
			cposition=lseek(source,readposition+startoffset,SEEK_SET);
			if (cposition>0) {
				sposition=cposition;
				seekable=1;
			} else {
				// seek failed, check why
				if (errno==EINVAL && seekable==1) {
					// tried to seek past the end of file. End reading.
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
		if (lowlevel==0 || (lowlevel==1 && !desperate)) {
			block=read(source,databuffer,remain);
		} else {
			//desperate mode means we are allowed to use low lvl 
			//IO syscalls to work around read errors
			block=read_desperately(sourcefile,&source,databuffer,sposition,remain,seekable,desperate,syncmode);
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
				} else {
					if (bblocksoutfile!=NULL) {
						// write badblocks to file if requested
						// start at first bad block in current bad set
						tmp_pos=((lastgood+startoffset)/blocksize);
						// override that with the first not yet written one
						if (lastbadblock>=tmp_pos) {
							tmp_pos=lastbadblock+1;
						}
						// then write all blocks that are smaller than the current one
						// note the different calculation that takes into account wether
						// the current block STARTS in a error but is ok here 
						// (which wouldnt be the case if we compared tmp_pos directly)
						while ((tmp_pos*blocksize)<(readposition+startoffset)) {
							lastbadblock=tmp_pos;
							sprintf(textbuffer,"%llu\n",lastbadblock);
							write(bblocksout,textbuffer,strlen(textbuffer));
							tmp_pos++;
						}
					}
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
						if (bblocksoutfile!=NULL) {
							// write badblocks to file if requested
							// start at first bad block in current bad set
							tmp_pos=((lastgood+startoffset)/blocksize);
							// override that with the first not yet written one
							if (lastbadblock>=tmp_pos) {
								tmp_pos=lastbadblock+1;
							}
							// then write all blocks that are not higher than the current one
							// note that HERE we compare blocks, not positions since a block
							// is bad if ANY part of it is bad
							while (tmp_pos<=((readposition+startoffset)/blocksize)) {
								lastbadblock=tmp_pos;
								sprintf(textbuffer,"%llu\n",lastbadblock);
								write(bblocksout,textbuffer,strlen(textbuffer));
								tmp_pos++;
							}
						}
					}

					// skip ahead(virtually)
					readposition+=remain;
					if (!backtracemode) {
						if (marker) {
							// if a marker is given, we need to write it to the
							// destination at the current position
							// first copy the marker into the data buffer
							writeoffset=0;
							writeremain=strlen(marker);
							while (writeoffset+writeremain<remain) {
								memcpy(databuffer+writeoffset,marker,writeremain);
								writeoffset+=writeremain;
							}
							memcpy(databuffer+writeoffset,marker,remain-writeoffset);
							// now write it to disk
							writeremain=remain;
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
						// force re-calculation of next blocksize to fix
						// block misalignment caused by partial data reading.
						// doing so during backtrace would cause an infinite loop.
						remain=0;
					}

				}

			}

// 6.f.2.c close and reopen source file
			// reopen source file to clear possible error flags preventing us from getting more data
			close (source);
			// do some forced seeks to move head around.
			for (cseeks=0;cseeks<seeks;cseeks++) {
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
