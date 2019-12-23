#ifndef mikmod_loader_h
#define mikmod_loader_h

extern const uint8_t haiku_s3m_bin[];
extern const uint32_t haiku_s3m_bin_size;

typedef struct _MY_MEMREADER {
	MREADER core;
	const void *buffer;
	long len;
	long pos;
} MY_MEMREADER;

MREADER *new_mikmod_mem_reader(const void *buffer, long len);
void delete_mikmod_mem_reader(MREADER* reader);

#endif
