#define _FILE_OFFSET_BITS 64
// make off_t a 64 bit pointer on system that support it

#ifndef __linux__
// if we don't have linux, the used ioctrls will be different
// use a dummy read function that uses high lvl operations
size_t read_desperately(char* filename, int *fd, unsigned char* buffer,
			off_t position, size_t length,
			int seekable) {
	size_t retval;
	retval=read(*fd,buffer,length);
	return retval;
}
#else

#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <errno.h>
#include <stdio.h>


//--------------floppy-------------------------------
int is_floppy(int fd) {
	// attempt a drive reset, return true if succesfull
	if (ioctl(fd,FDRESET)>=0) {
		return 1;
	}
	// or at least supported (a "real" reset fails if not root)
	if (errno!=ENOTTY) return 1;
	return 0;
}
//--------------end of floppy stuff -----------------


//---------------CD/DVD READING ---------------------
// is_cd does a drive reset to test if the device is a cd drive
int is_cd(int fd) {
	// attempt a cdrom drive reset, return true if succesfull
	if (ioctl(fd,CDROMRESET)>=0) {
		return 1;
	}
	// or at least supported (a "real" reset fails if not root)
	if (errno!=ENOTTY) return 1;
	return 0;
}
// helper function to calculate a cd sector position
void lba_to_msf( off_t lba, struct cdrom_msf * msf) {
	//lba= (((msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0) * CD_FRAMES + msf->cdmsf_frame0 ) - CD_MSF_OFFSET;
	lba=lba+CD_MSF_OFFSET;
	//lba= ((msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0) * CD_FRAMES + msf->cdmsf_frame0;
	msf->cdmsf_frame0=lba % CD_FRAMES;
	lba=lba/CD_FRAMES;
	//lba= (msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0;
	msf->cdmsf_sec0=lba % CD_SECS;
	msf->cdmsf_min0=lba/CD_SECS;
	//lba= msf->cdmsf_min0;
}
// read raw mode sector from a cd
size_t read_from_cd(int fd, unsigned char* buffer, off_t position, size_t length) {

	unsigned char blockbuffer[CD_FRAMESIZE_RAWER];
	struct cdrom_msf *msf=(struct cdrom_msf*)blockbuffer;

	// the calculation silently assumes that the cd read
	// consists of a single session single data track 
	// in mode0 or mode1
	// TODO: read TOC for cd layout
	// and fix sector calculation accordingly
	off_t lba=position/CD_FRAMESIZE;
	off_t extra=position-(lba*CD_FRAMESIZE);
	size_t xlength=CD_FRAMESIZE-extra;

	if (xlength>length) xlength=length;
	lba_to_msf(lba,msf);
	if (ioctl(fd, CDROMREADRAW, msf) == -1) {
		return -1;
	}

	// TODO: read parity and LEC data and check for possible read errors
	
	//each physical cd sector has 12 bytes "sector lead in thingy"
	//and 4 bytes address (maybe one could really confuse cdrom drives
	//by putting a very similar structure within user data?)
	memcpy(buffer,(blockbuffer+extra+12+4),xlength);
	return xlength;

}
//--------------end of CD/DVD stuff -----------------

// tries to perform a low level read operation on a device
// it should already be seeked to the right position for normal read()
// possible results:
// return>0: amounts of bytes read, the device will be open and the internal
// seek pointer point to position+length
// return<0: error. the device will be open but in undefined condition
size_t read_desperately(char* filename, int *fd, unsigned char* buffer,
			off_t position, size_t length,
			int seekable) {
	// lets assume it has already been tried to make a normal read.
	size_t retval;
	
	if (is_cd(*fd)) {
		//cdroms have low level io ioctls available for us
		retval=read_from_cd(*fd,buffer,position,length);
		if (retval>=0) {
			lseek(*fd,position+retval,SEEK_SET);
			return retval;
		}
	} else if (is_floppy(*fd)) {
		//we use std read after a drive reset (done by is_floppy)
		retval=read(*fd,buffer,length);
		return retval;
	} else {
		// unsupported device, normal read
		retval=read(*fd,buffer,length);
		return retval;
	}
}
#endif //__linux__
