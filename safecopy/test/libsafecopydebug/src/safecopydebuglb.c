/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
#define _FILE_OFFSET_BITS 64
#include <config.h>

#ifdef USE_GNU_SOURCE
#define _GNU_SOURCE
#endif

#define CONFIGFILE "safecopydebug.cfg"
#define MAXLIST 1000000

#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
//#include <unistd.h>
#include <string.h>
//#include <fcntl.h>
#ifndef _FCNTL_H
#define _FCNTL_H        1
#include <bits/fcntl.h>
#endif
#include <stdarg.h>

static int (*realopen)(const char*,int, ...);
static int (*realclose)(int);
static off_t (*reallseek)(int,off_t,int);
static ssize_t (*realread)(int,void*,size_t);

static int mydesc=-1;
static off_t current=0;

void _init(void);
int open(const char*,int,...);
int open64(const char*,int,...);
off_t lseek(int,off_t,int);
off64_t lseek64(int,off64_t,int);
ssize_t read(int,void*,size_t);
int close(int);

// these are needed on some newer glibc:
int __open_2(const char*,int);
int __open64_2(const char*,int);

ssize_t write(int, const void *, size_t);


static int slowsector[MAXLIST]={0};
static int softerror[MAXLIST]={0};
static int softerrorcount[MAXLIST]={0};
static int harderror[MAXLIST]={0};
static int slowsectordelay=0;
static int softerrordelay=0;
static int harderrordelay=0;
static int slowsectors=0;
static int slowsectorptr=0;
static int softerrors=0;
static int softerrorptr=0;
static int harderrors=0;
static int harderrorptr=0;
static int blocksize=1024;
static int softfailcount=0;
static int filesize=10240;
static char filename[256]="/dev/urandom";


static inline void delay(int milliseconds) {
	static struct timespec x;
	if (milliseconds==0) return;
	x.tv_sec=milliseconds/1000;
	x.tv_nsec=(milliseconds%1000)*1000000;
	nanosleep(&x,NULL);
}

void addtolist(int *array,int *count,int value) {
// insert value into sorted array of length *count
	if (*count==MAXLIST) {
		fprintf(stderr,"debugfile: Cannot store any more sectors in list - out of hardcoded memory limit!\n");
		return;
	}
	if (!*count) {
		array[*count]=value;
		*count=1;
	} else {
		if (value>array[*count]) {
			*count=*count+1;
			array[*count]=value;
		} else {
			int t;
			for (t=0; t<*count;t++) {
				if (array[t]==value) {
					return;
				} else if (array[t]<value) {
					memmove(&array[t+1],&array[t],(MAXLIST-t)-1);
					array[t]=value;
					*count=*count+1;
					return;
				}
			}
		}

	}
}

static inline int isinlist(int *array, int *pos, int *count,int value) {
// tell if a value is ina sorted list, speed up assuming the list is asked sequentially
	if (!*count) return 0;
	if (value<array[0]) return 0;
	if (value==array[0]) {
		fprintf(stderr," sec 0 matches at 0\n");
		*pos=0;
		return 1;
	}
	if (value>array[*count]) return 0;
	if (value==array[*count]) {
		*pos=*count;
		return 1;
	}
	// move current pointer forward if necessary
	while (*pos<*count && value>array[*pos]) {
		*pos=*pos+1;
	}
	// move current pointer backward if necessary
	while (*pos>0 && value<array[*pos]) {
		*pos=*pos-1;
	}
	// check wether we are where we want to be
	if (array[*pos]==value) return 1;
	return 0;
}


void readoptions() {
	FILE *fd;
	char line[256];
	char *number;
	int x;

	softerrors=0;
	harderrors=0;
	fd=fopen(CONFIGFILE,"r");
	if (!fd) {
		perror("debugfile could not open config file "CONFIGFILE);
		return;
	}
	while (fgets(line,255,fd)) {
		number=strchr(line,'=');
		if (number) {
			*number=0;
			number++;
			current=0;
			if (strcmp(line,"blocksize")==0) {
				sscanf(number,"%u",&blocksize);
				fprintf(stderr,"debugfile simulated blocksize: %u\n",blocksize);
			} else if (strcmp(line,"filesize")==0) {
				sscanf(number,"%u",&filesize);
				fprintf(stderr,"debugfile simulated filesize: %u\n",filesize);
			} else if (strcmp(line,"source")==0) {
				sscanf(number,"%s",filename);
				fprintf(stderr,"debugfile opening data source: %s\n",filename);
			} else if (strcmp(line,"softfailcount")==0) {
				sscanf(number,"%u",&softfailcount);
				fprintf(stderr,"debugfile simulated soft error count: %u\n",softfailcount);
			} else if (strcmp(line,"delayslow")==0) {
				sscanf(number,"%u",&slowsectordelay);
				fprintf(stderr,"debugfile delay on \"slow\" sectors: %u ms\n",slowsectordelay);
			} else if (strcmp(line,"delaysoft")==0) {
				sscanf(number,"%u",&softerrordelay);
				fprintf(stderr,"debugfile delay on soft errors: %u ms\n",softerrordelay);
			} else if (strcmp(line,"delayhard")==0) {
				sscanf(number,"%u",&harderrordelay);
				fprintf(stderr,"debugfile delay on hard errors: %u ms\n",harderrordelay);
			} else if (strcmp(line,"slow")==0) {
				sscanf(number,"%u",&x);
				addtolist(slowsector,&slowsectors,x);
				fprintf(stderr,"debugfile simulating read difficulty in block: %u\n",x);
			} else if (strcmp(line,"softfail")==0) {
				sscanf(number,"%u",&x);
				addtolist(softerror,&softerrors,x);
				fprintf(stderr,"debugfile simulating soft error in block: %u\n",x);
				softerrorcount[softerrors]=0;
			} else if (strcmp(line,"hardfail")==0) {
				sscanf(number,"%u",&x);
				addtolist(harderror,&harderrors,x);
				fprintf(stderr,"debugfile simulating hard error in block: %u\n",x);
			}
		}
	}
}


static inline void myprint(char* text) {
	size_t len=0;
	while ((char) *(text+len)) len++;
	write(2,text,len);
}

static inline void myprinthex(unsigned int num) {
	char buffer[17];
	buffer[16]=0x0;
	unsigned int num2;
	num2=num;
	char current='0';
	int pos;
	pos=16;
	myprint("0x");
	if (num2==0) {
		myprint("0");
	} else {
		while (num2>0) {
			current=(num2 & 15);
			num2=num2/16;
			if (current<10) {
				current+=48;
			} else {
				current+=87;
			}
			buffer[--pos]=current;
		}
		myprint(&buffer[pos]);
	}
}
static inline void myprintint(unsigned int num) {
	char buffer[33];
	buffer[32]=0x0;
	unsigned int num2=num;
	char current='0';
	int pos=32;
	if (num2==0) {
		myprint("0");
	} else {
		while (num2>0) {
			current=(num2 % 10);
			num2=num2/10;
			current+=48;
			buffer[--pos]=current;
		}
		myprint(&buffer[pos]);
	}
}

void _init(void) {
	realopen=dlsym(RTLD_NEXT,"open");
	realclose=dlsym(RTLD_NEXT,"close");
	reallseek=dlsym(RTLD_NEXT,"lseek");
	realread=dlsym(RTLD_NEXT,"read");
	myprint("debugfile initialising - reading config "CONFIGFILE"\n");
	readoptions();
}

int open(const char *pathname, int flags, ...) {
	int mode=0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start (ap,flags);
		mode=va_arg(ap,int);
		va_end(ap);
	}
	return open64(pathname,flags,mode);
}

int __open_2(const char *pathname, int flags) {
	return open64(pathname,flags);
}

int open64(const char *pathname,int flags,...) {
	//va_list ap;
	int fd;
	int mode=0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start (ap,flags);
		mode=va_arg(ap,int);
		va_end(ap);
	}
	if (strcmp(pathname,"debug")==0) {
		if (mydesc!=-1) {
			myprint("debugfile open - file already openend, can't open twice!\n");
			errno=ETXTBSY;
			return -1;
		}
		mydesc=realopen(filename,O_RDONLY);
		if (mydesc==-1) {
			perror("debugfile couldnt open source");
		} else {
			myprint("opening debug\n");
		}
		return mydesc;
	}
	fd=realopen(pathname,flags,mode);
	return fd;
}

int __open64_2(const char *pathname, int flags) {
	return open64(pathname,flags);
}

int close(int fd) {
	if (fd==mydesc && mydesc!=-1) {
		myprint("closing debug\n");
		mydesc=-1;
	}
	return realclose(fd);
}

off_t lseek(int filedes, off_t offset, int whence) {
	return lseek64(filedes,offset,whence);
}

off64_t lseek64(int filedes, off64_t offset, int whence) {
	off64_t newcurrent=0;
	if ( filedes==mydesc && mydesc!=-1) {
		if (whence==SEEK_SET) {
			myprint("seeking in debug: SEEK_SET to ");
			newcurrent=offset;
		} else if (whence==SEEK_CUR) {
			myprint("seeking in debug: SEEK_CUR to ");
			newcurrent=current+offset;
		} else if (whence==SEEK_END) {
			myprint("seeking in debug: SEEK_END to ");
			newcurrent=filesize+offset;
		} else {
			errno=EINVAL;
			return -1;
		}
		if (newcurrent>filesize) {
			errno=EINVAL;
			return -1;
		}
		current=newcurrent;
		myprintint(current);
		myprint("\n");
		reallseek(filedes,current,SEEK_SET);
		return current;
	}
	return reallseek(filedes,offset,whence);
}

ssize_t read(int fd,void *buf,size_t count) {
	ssize_t result;
	int count1;
	int block1,block2;
	int max=filesize;
	//myprint("read called\n");
	if ( fd==mydesc && mydesc!=-1) {
		result=count;
		myprint("reading from debug file: ");
		myprintint(count);
		myprint(" at position ");
		myprintint(current);

		if (current+count>filesize) {
			result=filesize-current;
			if (result<1) {
				myprint(" reads zero!\n");
				return 0;
			}
		}
		block1=current/blocksize;
		block2=(current+result)/blocksize;
		if (isinlist(slowsector,&slowsectorptr,&slowsectors,block1)) {
			delay(slowsectordelay);
		}
		if (isinlist(softerror,&softerrorptr,&softerrors,block1)) {
				delay(softerrordelay);
				if (softerrorcount[softerrorptr]++<softfailcount) {
					myprint(" simulated soft failure!\n");
					errno=EIO;
					return -1;
				} else {
					if (softerrorcount[softerrorptr]>softfailcount+1) {
						myprint(" simulated soft failure turned hard!\n");
						errno=EIO;
						return -1;
					}
					myprint(" simulated soft recovery:");
					softerrorcount[softerrorptr]-=2;
				}
		}
		if (isinlist(harderror,&harderrorptr,&harderrors,block1)) {
			delay(harderrordelay);
			myprint(" simulated hard failure!\n");
			errno=EIO;
			return -1;
		}
		for (count1=block1+1;count1<=block2;count1++) {
			if (isinlist(softerror,&softerrorptr,&softerrors,count1) && max>count1*blocksize) {
				max=count1*blocksize;
			}
			if (isinlist(harderror,&harderrorptr,&harderrors,count1) && max>count1*blocksize) {
				max=count1*blocksize;
			}
		}
		if (current+result>max) {
			myprint(" shrinks due to upcoming failure and ");
			result=max-current;
		}

		result= realread(fd,buf,result);
		current+=result;
		myprint(" reads ");
		myprintint(result);
		myprint(" bytes\n");
		return result;
	}
	return realread(fd,buf,count);
}
