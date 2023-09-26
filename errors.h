#define ERROR_NONE 0
#define ERROR_FILE_NOT_FOUND 1
#define ERROR_INVALID_DATA 2
#define ERROR_UPDATE_INIT_FAILED 3
#define ERROR_BITSTREAM_OPEN 4
#define ERROR_UPDATE_PROGRESS_FAILED 5
#define ERROR_KICKSTART_UPLOAD 6
#define ERROR_UPDATE_FAILED 7
#define ERROR_READ_BITSTREAM_FAILED 8
#define ERROR_DIRECT_ACCESS_INTERNAL 10

extern unsigned char Error;

void ErrorMessage(const char *message, unsigned char code);
void FatalError(unsigned long error);
