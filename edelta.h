#ifndef _EDELTA_H_
#define _EDELTA_H_

#define inline

/* type defs. */
typedef struct { 
	unsigned char* string; 
	unsigned int len; } buffer;

typedef unsigned short run_counter;

typedef struct { 
	unsigned int hash;
	int next_index;
	buffer* file; 
	unsigned int offset; 
} hash_match ;

typedef struct { 
	run_counter run; 
	int first_index;
} hash_entry;

typedef struct _delta_entry {
	int delta;
	int width;
	int dead_end;
	int num_elems;
	int max_elems;
	int bytes_saved;
	struct _delta_entry* next; } delta_entry;


/* function defs. */
delta_entry* storedelta(int d, int width, int offset);
inline unsigned int get_c(buffer* b, unsigned int offset);

inline unsigned int get_delta(buffer* base, unsigned int b_offset, buffer* version,unsigned int v_offset,int width);
inline void set_c(buffer* b, unsigned int offset, unsigned char val);
inline int binary_search(int* first, int* last, int val);
int intcmp(const void* va, const void* vb);
inline int swap(int x);
inline void storeanyint(unsigned int v, gzFile f);
inline unsigned int readanyint(gzFile f);
inline void storeint(unsigned int v, int n, gzFile f);
inline int readint(gzFile f);
inline int cgetc(gzFile f);

void store_hunk(buffer* base,buffer* version,
		unsigned int v_start, 
		unsigned int v_copy, 
		unsigned int b_copy, 
		unsigned int l,
		gzFile res);

int buffers_equal(buffer* a,int oa, buffer* b, int ob, int len);
void diff(buffer* base, buffer* version);
void reconstruct(FILE* base,buffer* dest, gzFile patch);
void banner();
void usage(char* appname);




#endif  /* _EDELTA_H_ */
