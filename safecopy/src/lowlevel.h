
// tries to perform a low level read operation on a device
// possible results:
// return>0: amounts of bytes read, the device will be open and the internal
// seek pointer point to position+length
// return<0: error. the device will be open but in undefined condition
size_t read_deperately(char* filename, int *fd, unsigned char* buffer,
			off_t position, size_t length,
			int seekable);

