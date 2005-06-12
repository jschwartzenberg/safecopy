#define _FILE_OFFSET_BITS 64
// make off_t a 64 bit pointer on system that support it

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "arglist.h"

#define MAXBLOCKSIZE 1048576
#define BLOCKSIZE 512
#define RESOLUTION 4

void usage(char * name) {
	fprintf(stderr,"Usage: %s [options] <source> <target>\n",name);
	fprintf(stderr,"Options:\n");
	fprintf(stderr,"	-b <bytes> : Blocksize in bytes, also used for skipping\n");
	fprintf(stderr,"	             offset when searching for the end of a bad area.\n");
	fprintf(stderr,"	             Set this to physical sectorsize of your media.\n");
	fprintf(stderr,"	             Default: 512\n");
	fprintf(stderr,"	-r <bytes> : Resolution in bytes when searching for the exact\n");
	fprintf(stderr,"	             beginning or end of a bad area\n");
	fprintf(stderr,"	             Bigger values increase performace at potential cost\n");
	fprintf(stderr,"	             of valid data close to damaged areas.\n");
	fprintf(stderr,"	             Default: 4\n");
	fprintf(stderr,"	-s <blocks> : Start position where to start reading.\n");
	fprintf(stderr,"	             Will correspond to position 0 in the destination file.\n");
	fprintf(stderr,"	             Default: block 0\n");
	fprintf(stderr,"	-l <blocks> : Length of data to be read.\n");
	fprintf(stderr,"	              Default: size of input file\n");
	fprintf(stderr,"	-h | --help : Show this text\n\n");
	fprintf(stderr,"Description of output:\n");
	fprintf(stderr,"	. : between 1 and 1024 blocks successfully read.\n");
	fprintf(stderr,"	_ : read was incomplete. (possibly end of file)\n");
	fprintf(stderr,"	    blocksize is reduced to read the rest.\n");
	fprintf(stderr,"	> : read failed, reducing blocksize to read partial data.\n");
	fprintf(stderr,"	[xx](+yy) : current block and number of blocks (or bytes)\n");
	fprintf(stderr,"	            continuously read successfully up to this point.\n");
	fprintf(stderr,"	X : read failed on block with minimum blocksize and is skipped.\n");
	fprintf(stderr,"	    Unrecoverable error, destination file is padded with zeros.\n");
	fprintf(stderr,"	    Data is now skipped until end of the unreadable area is reached\n");
	fprintf(stderr,"	< : Successfull read- test after the end of a bad area causes\n");
	fprintf(stderr,"	    backtracking to search for the first readable data.\n");
	fprintf(stderr,"	[xx](+yy) : current block and number of blocks (or bytes)\n");
	fprintf(stderr,"	            of recent continuous unreadable data.\n\n");
	fprintf(stderr,"Copyright 2005, distributed under terms of the GPL\n\n");
}

int main(int argc, char ** argv) {

	struct arglist *carglist;
	char *sourcefile,*destfile;
	int source,destination;
	off_t readposition,writeposition;
	off_t startoffset,length;
	ssize_t remain,block,writeblock,writeremain;
	char * databuffer;
	int blocksize,resolution;
	int writeoffset,counter,ecounter,newerror;
	off_t softerr,harderr,lasterror,lastgood;

	// read arguments
	carglist=arglist_new(argc,argv);
	arglist_addarg (carglist,"--help",0);
	arglist_addarg (carglist,"-h",0);
	arglist_addarg (carglist,"-b",1);
	arglist_addarg (carglist,"-r",1);
	arglist_addarg (carglist,"-s",1);
	arglist_addarg (carglist,"-l",1);

	
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
	
	blocksize=BLOCKSIZE;
	if (arglist_arggiven(carglist,"-b")==0) {
		blocksize=arglist_integer(arglist_parameter(carglist,"-b",0));
	}
	if (blocksize<1) blocksize=BLOCKSIZE;
	if (blocksize>MAXBLOCKSIZE) blocksize=MAXBLOCKSIZE;
	fprintf(stderr,"Blocksize is %i bytes.\n",blocksize);
	
	resolution=RESOLUTION;
	if (arglist_arggiven(carglist,"-r")==0) {
		resolution=arglist_integer(arglist_parameter(carglist,"-r",0));
	}
	if (resolution<1) resolution=RESOLUTION;
	if (resolution>blocksize) resolution=blocksize;
	fprintf(stderr,"Resolution is %i bytes.\n",resolution);
	
	startoffset=0;
	if (arglist_arggiven(carglist,"-s")==0) {
		startoffset=arglist_integer(arglist_parameter(carglist,"-s",0));
	}
	if (startoffset<1) startoffset=0;
	fprintf(stderr,"Starting read at block %i\n",startoffset);
	
	length=0;
	if (arglist_arggiven(carglist,"-l")==0) {
		length=arglist_integer(arglist_parameter(carglist,"-l",0));
	}
	if (length<1) length=-1;
	if (length>=0) {
		fprintf(stderr,"Copying ends after %i blocks read\n",length);
	}
	startoffset=startoffset*blocksize;
	length=length*blocksize;

	databuffer=(char*)malloc((blocksize+1024)*sizeof(char));
	if (databuffer==NULL) {
		fprintf(stderr,"MEMORY ALLOCATION ERROR!\nCOULDNT ALLOCATE MAIN BUFFER!\nBAILING!\n");
		return 2;
	}
		
	//open files
	fprintf(stdout,"Trying to safe copy from %s to %s ... \n",sourcefile,destfile);
	source=open(sourcefile,O_RDONLY );
	if (source==-1) {
		fprintf(stderr,"Error opening sourcefile: %s \n",sourcefile);
		usage(argv[0]);
		arglist_kill(carglist);
		return 2;
	}
	destination=open(destfile,O_WRONLY | O_TRUNC | O_CREAT,0666 );
	if (destination==-1) {
		close(source);
		fprintf(stderr,"Error opening destination: %s \n",destfile);
		usage(argv[0]);
		arglist_kill(carglist);
		return 2;
	}

	// start at file start
	readposition=0;
	writeposition=0;
	block=-1;
	remain=0;
	writeremain=0;
	softerr=0;
	harderr=0;
	counter=1;
	ecounter=1;
	newerror=1;
	lasterror=0;
	lastgood=0;
	
	while (block!=0 && (readposition<length || length<0)) {
		// write where we read
		writeposition=readposition;

		// and start with a hole new block if we finnished the old
		if (remain==0) remain=blocksize;

		// calculate how much is left to copy
		if (readposition+remain>length && length>=0) {
			remain=length-readposition;
		}

		// seek and read
		lseek(source,readposition+startoffset,SEEK_SET);
		block=read(source,databuffer,remain);

		if (block>0) {
			// read successfull, test for end of damaged area
			if (newerror==0) {
				// we are in recovery since we just read past the end of a damaged area
				// so we go back until the beginning of the readable area is found

				if (remain>resolution) {
				 	remain=remain/2;
					readposition-=remain;
					write(0,&">",1);
				} else {
					newerror=1;
					remain=0;
					writeremain=readposition/blocksize;
					writeoffset=(readposition-lastgood)/blocksize;
					if (writeoffset<4) {
						writeoffset=readposition-lastgood;
						sprintf(databuffer,"<[%i](+%iBytes)",writeremain,writeoffset);
						
					} else {
						sprintf(databuffer,"<[%i](+%i)",writeremain,writeoffset);
					}
					write(0,databuffer,strlen(databuffer));
					lasterror=readposition;
				}
				
			} else {
				ecounter=1;
				if (block<remain) {
					// not all data we wanted got read, note that
					write(0,&"_",1);
					counter=1;
				} else if (--counter<=0) {
					write(0,&".",1);
					counter=1024;
				}

				remain-=block;
				readposition+=block;
				writeremain=block;
				writeoffset=0;

				while (writeremain>0) {
					lseek(destination,writeposition,SEEK_SET);
					writeblock=write(destination,databuffer+writeoffset,writeremain);
					if (writeblock<=0) {
						fprintf(stderr,"\nWRITING TO %s FAILED, BAILING!\n",destfile);
						close(destination);
						close(source);
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
			if (remain > resolution && newerror==1) {
				// start of a new erroneous area - decrease readsize in
				// case we can read partial data from the beginning
				softerr++;
				remain=remain/2;

				write(0,&">",1);
			} else {
				// readsize is already minimal
				// unrecoverable error, go one sector ahead and try again there 

				if (newerror==1) {
					// if this is still the start of a damaged area,
					// also print the amount of successfully read sectors
					newerror=0;
					writeremain=readposition/blocksize;
					writeoffset=(readposition-lasterror)/blocksize;
					if (writeoffset<4) {
						writeoffset=readposition-lasterror;
						sprintf(databuffer,"[%i](+%iBytes)",writeremain,writeoffset);
						
					} else {
						sprintf(databuffer,"[%i](+%i)",writeremain,writeoffset);
					}
					write(0,databuffer,strlen(databuffer));
					// and we set the read size high enough to go over the damaged area quickly
					remain=blocksize;
					lastgood=readposition;
				} 

				harderr++;
				readposition+=remain;


				write(0,&"X",1);

			}

			// reopen source file to clear possible error flags preventing us from getting more data
			close (source);
			source=open(sourcefile,O_RDONLY );
			if (source==-1) {
				fprintf(stderr,"\nError on reopening sourcefile during error recovery - copy failed!\n");
				close(destination);
				arglist_kill(carglist);
				return 2;
			}
		}
	}
	fprintf(stdout,"\nCopying done ");
	if (harderr==0) {
		fprintf(stdout,"successfully :)\n");
	} else {
		fprintf(stdout,"with skipped parts !\n");
	}
	fprintf(stdout,"%lli recoverable and %lli non recoverable errors occured.\n",softerr,harderr);
	fprintf(stdout,"%lli bytes (%lli blocks) read/written\n",readposition,readposition/blocksize);

	close(destination);
	close(source);
	arglist_kill(carglist);
}
