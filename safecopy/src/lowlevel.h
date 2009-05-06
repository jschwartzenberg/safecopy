
// tries to perform a low level read operation on a device
// possible results:
// return>0: amounts of bytes read, the device will be open and the internal
// seek pointer point to position+length
// return<0: error. the device will be open but in undefined condition
off_t read_deperately(char* filename, int *fd, unsigned char* buffer,
			off_t position, off_t length,
			int seekable, int recovery, int syncmode);

// tries to get some information from the low level driver
off_t lowlevel_filesize(char* filename, off_t filesize);
off_t lowlevel_blocksize(char* filename, off_t blocksize);
int lowlevel_canseek();
