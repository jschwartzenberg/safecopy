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
#define BLOCKSIZE 512
#define RESOLUTION 4
#define RETRIES 2

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
	fprintf(stderr,"Usage: %s [options] <source> <target>\n",name);
	fprintf(stderr,"Options:\n");
	fprintf(stderr,"	-b <bytes> : Blocksize in bytes, also used as skipping offset\n");
	fprintf(stderr,"	             when searching for the end of a bad area.\n");
	fprintf(stderr,"	             Set this to the physical sectorsize of your media.\n");
	fprintf(stderr,"	             Default: Blocksize of input device, if determinable,\n");
	fprintf(stderr,"	                      otherwise %i\n",BLOCKSIZE);
	fprintf(stderr,"	-r <bytes> : Resolution in bytes when searching for the exact\n");
	fprintf(stderr,"	             beginning or end of a bad area.\n");
	fprintf(stderr,"	             Smaller values lead to very thorough attempts to read\n");
	fprintf(stderr,"	             data at the edge of damaged areas,\n");
	fprintf(stderr,"	             but increase the strain on the damaged media.\n");
	fprintf(stderr,"	             Default: %i\n",RESOLUTION);
	fprintf(stderr,"	-R <number> : At least that many read attempts are made on the first\n");
	fprintf(stderr,"	              bad block of a damaged area with minimum resolution.\n");
	fprintf(stderr,"	              Higher values can sometimes recover a weak sector,\n");
	fprintf(stderr,"	              but at the cost of additional strain.\n");
	fprintf(stderr,"	              Default: %i\n",RETRIES);
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
	fprintf(stderr,"	-i <bytes> : Blocksize to interprete the badblockfile given with -I.\n");
	fprintf(stderr,"	             Default: Blocksize as specified by -b\n");
	fprintf(stderr,"	-X <badblockfile> : Exclusion mode. Do not attemp to read blocks in\n");
	fprintf(stderr,"	                    badblockfile. If used together with -I,\n");
	fprintf(stderr,"	                    excluded blocks override included blocks.\n");
	fprintf(stderr,"	                    Default: none\n");
	fprintf(stderr,"	-x <bytes> : Blocksize to interprete the badblockfile given with -X.\n");
	fprintf(stderr,"	             Default: Blocksize as specified by -b\n");
	fprintf(stderr,"	-o <badblockfile> : Write a badblocks/e2fsck compatible bad block file.\n");
	fprintf(stderr,"	                    Default: none\n");
	fprintf(stderr,"	-S <seekscript> : Use external script for seeking in input file.\n");
	fprintf(stderr,"	                  (Might be usefull for tape devices and similar).\n");
	fprintf(stderr,"	                  Seekscript must be an executable that takes the\n");
	fprintf(stderr,"	                  number of blocks to be skipped as argv1 (1-64)\n");
	fprintf(stderr,"	                  the blocksize in bytes as argv2\n");
	fprintf(stderr,"	                  and the current position (in bytes) as argv3.\n");
	fprintf(stderr,"	                  Return value needs to be the number of blocks\n");
	fprintf(stderr,"	                  successfully skipped, or 0 to indicate seek failure.\n");
	fprintf(stderr,"	                  The external seekscript will only be used\n");
	fprintf(stderr,"	                  if lseek() fails and we need to skip over data.\n");
	fprintf(stderr,"	                  Default: none\n");
	fprintf(stderr,"	-h | --help : Show this text\n\n");
	fprintf(stderr,"Description of output:\n");
	fprintf(stderr,"	. : Between 1 and 1024 blocks successfully read.\n");
	fprintf(stderr,"	_ : Read of block was incomplete. (possibly end of file)\n");
	fprintf(stderr,"	    The blocksize is now reduced to read the rest.\n");
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

off_t lseek_dummy(int a,int b, int c) {
	return -1;
}

// main
int main(int argc, char ** argv) {

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
	// several local integer variables
	int blocksize,iblocksize,xblocksize,resolution,retries;
	int incremental,excluding;
	int counter,percent,oldpercent,newerror,newsofterror;
	int backtracemode,output,linewidth,seekable;

	// error indicators and flags
	off_t softerr,harderr,lasterror,lastgood;
	// tmp vars for file offsets
	off_t tmp_pos,tmp_bytes;
	// variables to remember beginning and end of previous good/bad area
	off_t lastbadblock,lastxblock,lastsourceblock;
	// stat() needs this
	struct stat filestatus;
	// input filesize and size of unreadable area
	off_t filesize,damagesize;
	// times
	struct timeval oldtime,newtime;
	// and timing helper variables
	long int elapsed,oldelapsed,oldcategory;
	// select() needs these
	fd_set rfds,efds;

	// parse all commandline arguments
	carglist=arglist_new(argc,argv);
	arglist_addarg (carglist,"--help",0);
	arglist_addarg (carglist,"-h",0);
	arglist_addarg (carglist,"-b",1);
	arglist_addarg (carglist,"-r",1);
	arglist_addarg (carglist,"-R",1);
	arglist_addarg (carglist,"-s",1);
	arglist_addarg (carglist,"-l",1);
	arglist_addarg (carglist,"-o",1);
	arglist_addarg (carglist,"-I",1);
	arglist_addarg (carglist,"-i",1);
	arglist_addarg (carglist,"-X",1);
	arglist_addarg (carglist,"-x",1);
	arglist_addarg (carglist,"-S",1);

	
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
	
	// find out source file size and block size
	filesize=0;
	blocksize=BLOCKSIZE;
	if(!stat(sourcefile,&filestatus)) {
		filesize=filestatus.st_size;
		if (filestatus.st_blksize) {
			fprintf(stderr,"Source file blocksize is %lu bytes according to stat().\n",filestatus.st_blksize);
			blocksize=filestatus.st_blksize;
		}
	}
	if (arglist_arggiven(carglist,"-b")==0) {
		blocksize=arglist_integer(arglist_parameter(carglist,"-b",0));
	}
	if (blocksize<1) blocksize=BLOCKSIZE;
	if (blocksize>MAXBLOCKSIZE) blocksize=MAXBLOCKSIZE;
	fprintf(stdout,"Blocksize is %i bytes.\n",blocksize);

	if (filesize!=0) {
		fprintf(stderr,"Determined input file size is %llu bytes.\n");
	} else {
		fprintf(stderr,"Unable to determine input file size.\n");
	}
	
	resolution=RESOLUTION;
	if (arglist_arggiven(carglist,"-r")==0) {
		resolution=arglist_integer(arglist_parameter(carglist,"-r",0));
	}
	if (resolution<1) resolution=RESOLUTION;
	if (resolution>blocksize) resolution=blocksize;
	fprintf(stdout,"Resolution is %i bytes.\n",resolution);
	
	retries=RETRIES;
	if (arglist_arggiven(carglist,"-R")==0) {
		retries=arglist_integer(arglist_parameter(carglist,"-R",0));
	}
	if (retries<1) retries=1;
	fprintf(stdout,"Attempting to read damaged data at least %i times.\n",retries);

	iblocksize=blocksize;
	if (arglist_arggiven(carglist,"-i")==0) {
		iblocksize=arglist_integer(arglist_parameter(carglist,"-i",0));
	}
	if (iblocksize<1 || iblocksize>MAXBLOCKSIZE) {
		fprintf(stderr,"Invalid blocksize given for bad block include file! Aborting!\n");
		arglist_kill(carglist);
		return 2;
	}

	incremental=0;
	if (arglist_arggiven(carglist,"-I")==0) {
		incremental=1;
		bblocksinfile=arglist_parameter(carglist,"-I",0);
		fprintf(stdout,"Incremental mode, incoming badblocks file: %s with blocksize %i.\n",bblocksinfile,iblocksize);
	}

	xblocksize=blocksize;
	if (arglist_arggiven(carglist,"-x")==0) {
		xblocksize=arglist_integer(arglist_parameter(carglist,"-x",0));
	}
	if (xblocksize<1 || xblocksize>MAXBLOCKSIZE) {
		fprintf(stderr,"Invalid blocksize given for bad block exclude file! Aborting!\n");
		arglist_kill(carglist);
		return 2;
	}

	excluding=0;
	if (arglist_arggiven(carglist,"-X")==0) {
		excluding=1;
		xblocksinfile=arglist_parameter(carglist,"-X",0);
		fprintf(stdout,"Exclusion mode, excluding badblocks file: %s with blocksize %i.\n",xblocksinfile,xblocksize);
	}

	bblocksoutfile=NULL;
	if (arglist_arggiven(carglist,"-o")==0) {
		bblocksoutfile=arglist_parameter(carglist,"-o",0);
		fprintf(stdout,"Writing badblocks file: %s.\n",bblocksoutfile);
	}

	seekscriptfile=NULL;
	if (arglist_arggiven(carglist,"-S")==0) {
		seekscriptfile=arglist_parameter(carglist,"-S",0);
		fprintf(stderr,"Using %s for emergency seeking.\n",seekscriptfile);
	}

	startoffset=0;
	if (arglist_arggiven(carglist,"-s")==0) {
		startoffset=arglist_integer(arglist_parameter(carglist,"-s",0));
	}
	if (startoffset<1) startoffset=0;
	fprintf(stdout,"Starting read at block %i.\n",startoffset);
	
	length=0;
	if (arglist_arggiven(carglist,"-l")==0) {
		length=arglist_integer(arglist_parameter(carglist,"-l",0));
	}
	if (length<1) length=-1;
	if (length>=0) {
		fprintf(stdout,"Limiting size to %i blocks.\n",length);
	}
	startoffset=startoffset*blocksize;
	length=length*blocksize;
	if (filesize==0 && length>0) {
		filesize=startoffset+length;
	}

	databuffer=(char*)malloc((blocksize+1024)*sizeof(char));
	if (databuffer==NULL) {
		fprintf(stderr,"MEMORY ALLOCATION ERROR!\nCOULDNT ALLOCATE MAIN BUFFER!\nBAILING!\n");
		perror("Reason");
		return 2;
	}
		
	//open files
	fprintf(stdout,"Trying to safe copy from %s to %s ... \n",sourcefile,destfile);
	source=open(sourcefile,O_RDONLY | O_NONBLOCK );
	if (source==-1) {
		fprintf(stderr,"Error opening sourcefile: %s \n",sourcefile);
		perror("Reason");
		usage(argv[0]);
		arglist_kill(carglist);
		return 2;
	}
	if (excluding==1) {
		xblocksin=fopen(xblocksinfile,"r");
		if (xblocksin==NULL) {
			close(source);
			fprintf(stderr,"Error opening exclusion badblock file for reading: %s \n",xblocksinfile);
			perror("Reason");
			arglist_kill(carglist);
			return 2;
		}
	}
	if (incremental==1) {
		bblocksin=fopen(bblocksinfile,"r");
		if (bblocksin==NULL) {
			close(source);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening badblock file for reading: %s \n",bblocksinfile);
			perror("Reason");
			arglist_kill(carglist);
			return 2;
		}
		destination=open(destfile,O_WRONLY,0666 );
		if (destination==-1) {
			close(source);
			fclose(bblocksin);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening destination: %s \n",destfile);
			perror("Reason");
			usage(argv[0]);
			arglist_kill(carglist);
			return 2;
		}
	} else {
		destination=open(destfile,O_WRONLY | O_TRUNC | O_CREAT,0666 );
		if (destination==-1) {
			close(source);
			if (excluding==1) fclose(xblocksin);
			fprintf(stderr,"Error opening destination: %s \n",destfile);
			perror("Reason");
			usage(argv[0]);
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
			fprintf(stderr,"Error opening badblock file for writing: %s \n",bblocksoutfile);
			perror("Reason");
			arglist_kill(carglist);
			return 2;
		}
	}

	// setting signal handler
	signal(SIGINT, signalhandler);

	// initialise all vars
	readposition=0;
	writeposition=0;
	block=-1;
	remain=0;
	writeremain=0;
	softerr=0;
	harderr=0;
	counter=1;
	newerror=retries;
	newsofterror=0;
	lasterror=0;
	lastgood=0;
	lastbadblock=-1;
	lastxblock=-1;
	lastsourceblock=-1;
	damagesize=0;
	backtracemode=0;
	percent=-1;
	oldpercent=-1;
	oldelapsed=0;
	oldcategory=timecategory(oldelapsed);
	elapsed=0;
	output=0;
	linewidth=0;
	sposition=0;
	seekable=1;

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
		source=open(sourcefile,O_RDONLY );
		if (source==-1) {
			fprintf(stderr,"\nError on reopening sourcefile during error recovery - copy failed!\n");
			perror("Reason");
			close(destination);
			if (incremental==1) fclose(bblocksin);
			if (excluding==1) fclose(xblocksin);
			if (bblocksoutfile!=NULL) close(bblocksout);
			arglist_kill(carglist);
			return 2;
		}
	} else {
		sposition=cposition;
		seekable=1;
	}
	
	// main data loop. Continue until all data has been read or CTRL+C has been pressed
	while (!wantabort && block!=0 && (readposition<length || length<0)) {

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
							// make sure main loop exits asap
							remain=0;
							break;
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
							write(2,&"\n",1);
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
				remain=((((readposition/blocksize)+1)*blocksize)-readposition);
			} while (excluding && tmp_pos==lastxblock);
			// break from above jumps here, with remain=0
		}


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
				if (errno==EINVAL && seekable==1 && newerror==0) {
					// tried to seek past the end of file during skipping of erroneous area
					break;
				} else {
					// input file is not seekable!
					seekable=0;
					if (readposition+startoffset>sposition) {
						// emergency seek will only handle positive seeks
						// close input file for seek/skip
						close (source);
						cposition=emergency_seek(startoffset+readposition,sposition,blocksize,seekscriptfile);
						if (cposition<0 && newerror==0) {
							// bail if we cannot skip over hard errors!
							perror("\nError! Non-recoverable error in non-seekable input");
							break;
						}
						// reopen input file
						source=open(sourcefile,O_RDONLY );
						if (source==-1) {
							fprintf(stderr,"\nError on reopening sourcefile after external seek - copy failed!\n");
							perror("Reason");
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
				if (filesize) {
					printpercentage(percent);
				}
				printtimecategory(VERY_VERY_SLOW);
			}
			if (wantabort) break;
		} while (! ( FD_ISSET(source,&rfds) || FD_ISSET(source,&efds)));
		if (wantabort) break;
		// read input data
		block=read(source,databuffer,remain);
		// time reading for quality calculation
		gettimeofday(&newtime,NULL);
		elapsed=timediff(oldtime,newtime);

		// smooth times, react sensitive to high times
		if (timecategory(elapsed)>timecategory(oldelapsed)) {
			oldelapsed=(((9*oldelapsed)+elapsed)/10);
		} else if (timecategory(elapsed)<timecategory(oldelapsed)) {
			oldelapsed=(((99*oldelapsed)+elapsed)/100);
		}
		// update percentage if changed
		if (filesize && ( percent!=oldpercent || output)) {
			printpercentage(percent);
			printtimecategory(timecategory(oldelapsed));
			oldpercent=percent;
		}
		// update quality if changed
		if (timecategory(oldelapsed)!=oldcategory) {
			if (filesize) printpercentage(percent);
			printtimecategory(timecategory(oldelapsed));
			oldcategory=timecategory(oldelapsed);
		} else if (output) {
			// or if any output has taken place
			if (filesize) printpercentage(percent);
			printtimecategory(timecategory(oldelapsed));
		}
		// break lines after 40 chars to prevent backspace bug of terminals
		if (linewidth>40) {
			tmp_pos=readposition/blocksize;
			sprintf(textbuffer," [%lli]\n",tmp_pos);
			write(2,textbuffer,strlen(textbuffer));
			linewidth=0;
		}
		output=0;

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
					newerror=retries;
					remain=0;
					tmp_pos=readposition/blocksize;
					tmp_bytes=readposition-lastgood;
					damagesize+=tmp_bytes;
					sprintf(textbuffer,"}[%llu](+%llu)",tmp_pos,tmp_bytes);
					write(1,textbuffer,strlen(textbuffer));
					write(2,&"\n",1);
					output=1;
					linewidth=0;
					backtracemode=0;
					lasterror=readposition;
				}
				
			} else {
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
						fprintf(stderr,"\nSEEK IN %s FAILED\n",destfile);
						perror("Reason");
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
						fprintf(stderr,"\nWRITING TO %s FAILED, BAILING!\n",destfile);
						perror("Reason");
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
			// write operation failed
			counter=1;
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
						remain=((((readposition/blocksize)+1)*blocksize)-readposition);
						lastgood=readposition;
					} 

					if (!backtracemode) {
						harderr++;
						write(1,&"X",1);
						output=1;
						linewidth++;
						if (bblocksoutfile!=NULL) {
							// write badblocks to file if requested
							tmp_pos=((readposition+startoffset)/blocksize);
							if (tmp_pos>lastbadblock) {
								lastbadblock=tmp_pos;
								sprintf(textbuffer,"%llu\n",lastbadblock);
								write(bblocksout,textbuffer,strlen(textbuffer));
							}
							
						}
					}

					// skip ahead(virtually)
					readposition+=remain;
					if (!backtracemode) {
						// force re-calculation of next blocksize to fix
						// block misalignment caused by partial data reading.
						// doing so during backtrace would cause an infinite loop.
						remain=0;
					}

				}

			}

			// reopen source file to clear possible error flags preventing us from getting more data
			close (source);
			source=open(sourcefile,O_RDONLY );
			if (source==-1) {
				fprintf(stderr,"\nError on reopening sourcefile during error recovery - copy failed!\n");
				perror("Reason");
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
	if (newerror==0) {
		// if theres an error at the end of input, treat as if we had one succesfull read afterwards
		tmp_pos=readposition/blocksize;
		tmp_bytes=readposition-lastgood;
		damagesize+=tmp_bytes;
		sprintf(textbuffer,"}[%llu](+%llu)",tmp_pos,tmp_bytes);
		write(1,textbuffer,strlen(textbuffer));
		write(2,&"\n",1);
	}
	fprintf(stdout,"\nCopying done ");
	if (harderr==0) {
		if (wantabort) {
			fprintf(stdout,"after CTRL-C, no errors.\n");
		} else {
			fprintf(stdout,"successfully :)\n");
		}
	} else {
		if (wantabort) {
			fprintf(stdout,"after CTRL-C, ");
		}
		fprintf(stdout,"with errors !\n");
	}
	fprintf(stdout,"%llu recoverable and %llu non recoverable errors occured.\n",softerr,harderr);
	fprintf(stdout,"%llu bytes (%llu blocks) copied.\n",(readposition-startoffset),(readposition-startoffset)/blocksize);
	fprintf(stdout,"%llu bytes in %llu blocks were unrecoverable.\n",damagesize,harderr);

	close(destination);
	close(source);
	if (incremental==1) fclose(bblocksin);
	if (excluding==1) fclose(xblocksin);
	if (bblocksoutfile!=NULL) close(bblocksout);
	arglist_kill(carglist);
	return(0);
}
