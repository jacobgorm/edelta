#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include <stdlib.h>
#include <zlib.h>

#ifdef __WIN32__
#include <windows.h>
#else
#include <netinet/in.h>
#endif

#include "version.h"
#include "edelta.h"


#define EDELTA_PATCH_VERSION 2

/*
 The EDelta executable differ. 

 Copyright (C) 2003-2006 Jacob Gorm Hansen <jacob@melon.dk>.
 Licensed under the GNU Public License.

 TODO:
	- we now clamp delta size to sizeof(int), check if this creates problems at
      start of file (offset <4)

	- somehow guess endianess of file and use correct one
 */


extern void sha1_digest(unsigned char*, unsigned char*, size_t);

int quiet=0;
int little_endian=0;

const int prefixlen = 32;
const int B = 11;
const int tolerance = sizeof(int);

delta_entry* deltas[0x10000];
hash_entry hashes[0x10000];
hash_match* matches;

gzFile res;


delta_entry* storedelta(int d, int width, int offset)
{
	const int min_elems = 64;
	unsigned short key;
	delta_entry* e;
	delta_entry** pe;
	int *es;

	key = width>2 ? (d&0xffff) ^ (d>>16) : d;

	e = deltas[key];
	pe = &(deltas[key]);

	while(e && (e->width!=width || e->delta != d)) pe=&(e->next), e=e->next;

	/* TODO we are calling malloc() for each new delta. Some highly 
	 * diverging files may result in slowness and use of too much memory */
	
	if(!e) 
	{
		e = (delta_entry*) malloc(sizeof(delta_entry) + sizeof(int)*min_elems);
		e->delta = d;
		e->width = width;
		e->max_elems = min_elems;
		e->num_elems=0;
		e->bytes_saved=0;
		e->next=0;

		*pe = e;
	}

	if(e->num_elems==e->max_elems)
	{
		e->max_elems *= 2;
		*pe = e = (delta_entry*) realloc(e, sizeof(delta_entry) + sizeof(int)*e->max_elems);
	}
	es = (int*)((char*)e + sizeof(delta_entry));

	es[e->num_elems++] = offset;

	return e;

}

inline unsigned int get_c(buffer* b, unsigned int offset)
{
	if(offset>=b->len) return 0;

	return b->string[offset];
}

inline unsigned int get_delta(buffer* base, unsigned int b_offset,
			buffer* version,unsigned int v_offset,int width)
{

	int i;
	int a=0,b=0;

	if(little_endian)
	{
		for(i=0; i<width; i++)
		{
			a = a<<8 | get_c(base,b_offset+(width-1)-i);
			b = b<<8 | get_c(version,v_offset+(width-1)-i);
		}
	}

	else
	{
		for(i=0; i<width; i++)
		{
			a = a<<8 | get_c(base,b_offset+i);
			b = b<<8 | get_c(version,v_offset+i);
		}
	}

	return b-a;

}

inline void set_c(buffer* b, unsigned int offset, unsigned char val)
{
	if(offset>=b->len) return;

	b->string[offset] = val;
}

inline int binary_search(int* first, int* last, int val)
{
	int half, *middle;
	int len = last-first;
	while (len > 0) 
	{
		half = len >> 1;
		middle = first;

		middle += half;

		if (*middle < val)
		{
			first = middle;
			++first;
			len = len - half - 1;
		} 
		else len = half;
	}

    return ((first != last) && (*first==val));


}

int intcmp(const void* va, const void* vb)
{
	int a = *((int*)va);
	int b = *((int*)vb);

	return a-b;
}

/* store n copies of the value in file, 1<= n <2^13 */

/* prefixes:
   0 == byte (7 bits)
   4 == 2 bytes (13 bits)
   5 == 3 bytes (21 bits)
   6 == 4 bytes (29 bits) 
   7 == 13 bits specifying RLE of next element
   */


/* this is to better compress 4-byte aligned offsets, as they are common */
inline int swap(int x)
{
	return ((x&3)<<7) | (  (x&0x180)>>7) | (x & 0xfffffe7c);
}





inline void storeanyint(unsigned int v, gzFile f)
{
	v = htonl(v);
	gzwrite(f, &v, sizeof(v));
}

inline unsigned int readanyint(gzFile f)
{
	int v;
	int s = gzread(f, &v, sizeof(v));

	if(s<=0)
	{
		fprintf(stderr,"only read %d elements\n",s);
		exit(-1);

	}
	return ntohl(v);
}

inline void storeint(unsigned int v, int n, gzFile f)
{
	int i,w;


	if(v<0x80) w=1;
	else if(v<0x2000) w=2, v |= (4<<13);
	else if(v<0x200000) w=3, v |= (5<<21);
	else if(v<0x20000000) w=4, v |= (6<<29);
	else fprintf(stderr,"oups out of range: storeint(%x)!\n",v), exit(-1);


	if(n>1)
	{
		if(n>2 || w>1)
		{
			unsigned short r = (7<<13) | n;
			gzputc(f,r>>8);
			gzputc(f,r);
			n=1;
		}
	}
	for(i=0; i<n; i++)
	{
		int j;

		for(j=w-1; j>=0; j--)
		{
			gzputc(f, v >> (j*8));
		}
	}

}

inline int readint(gzFile f)
{
	static unsigned int rept=0;
	static int v;
	int i;

	if(rept==0) /* rept is static */
	{
		int h;
		int w;
		v = 0;
		h = cgetc(f);

		if( (h>>5)==7 )
		{
			int c = cgetc(f);

			rept = (h & 0x1f) << 8 | c;
			--rept;

			h = cgetc(f);
		}

		if((h&0x80)==0)	v = h&0x7f;
		else 
		{
			w = (h>>5)-3;	/* remaining bytes to read */

			v = h & 0x1f;

			for(i=0; i<w; i++)
			{
				v = v<<8 | cgetc(f);
			}
		}


	}
	else --rept;

	return v;
}


inline int cgetc(gzFile f)
{
	int r= gzgetc(f);
	if(r==EOF) 
	{
		puts("cgetc EOF");
		exit(-1);
	}
	return r;
}


void store_hunk(buffer* base,buffer* version,
		unsigned int v_start, 
		unsigned int v_copy, 
		unsigned int b_copy, 
		unsigned int l,
		gzFile res) 
{
	static unsigned int v_last_start=0;
	static unsigned int v_last_copy=0;
	static unsigned int b_last_copy=0;

	storeint( v_start - v_last_start, 1, res);
	storeint( v_copy - v_last_copy ,  1, res);
	storeint( b_copy - b_last_copy ,  1, res);
	storeint( l,       1, res); 

#if 0
	printf("%d %d %d %d\n",
		v_start - v_last_start,
		v_copy - v_last_copy ,
		b_copy - b_last_copy , l);
#endif

	v_last_start = v_start;
	v_last_copy = v_copy; 
	b_last_copy = b_copy;
	gzwrite(res, version->string+v_start, v_copy-v_start);
}

int buffers_equal(buffer* a,int oa, buffer* b, int ob, int len)
{
	int i;

	if(a->len < oa+len) return 0;
	if(b->len < ob+len) return 0;

	for(i=0; i< len; i++)
	{
		if(get_c(a,oa+i) != get_c(b,ob+i))
		{
			return 0;
		}
	}
	return 1;

}

void diff(buffer* base, buffer* version)
{
	int pass;
	int i;
	int num_adds=0;
	int addsize=0;
	int num_deltas=0;
	unsigned int max_matches=0x4000;
	int *dead_ends = NULL;
	int num_dead_ends=0;
	matches = malloc(sizeof(hash_match) * max_matches);

	storeint(version->len, 1, res);

	for(pass=0; pass<2; pass++)
	{
		unsigned int h_d; /* =  1 << (prefixlen-1); */
		unsigned int h_b=0;
		unsigned int h_v=0;
		unsigned int v_start=0;
		unsigned int v_offset=0;
		unsigned int b_offset=0;
		unsigned int b_last_copy=0;
		unsigned short run=1;
		unsigned int num_matches=0;
		const unsigned int mask = sizeof(hashes)/sizeof(hash_entry)-1;

		for(h_d=i=1; i<prefixlen; i++)
		{
			h_d = B*h_d;
		}
		for(i=0; i<prefixlen; i++)
		{
			h_b =  B*h_b + get_c(base,i);
			h_v =  B*h_v + get_c(version,i);
		}
		memset(hashes, 0, sizeof(hashes));
		memset(deltas, 0, sizeof(deltas));


		for(;;)
		{
			typedef struct { buffer* current; unsigned int* h ; unsigned int* offset;} sss;
			sss files[] = {{ base, &h_b, &b_offset}, {version, &h_v, &v_offset}};

			for(i=0; i<2; i++) 
			{
				sss* f = &(files[i]);
				unsigned int* h = f->h;
				hash_entry* e = &(hashes[*h & mask]);

				int index = (e->run==run) ? e->first_index : 0;
				int prev = 0;

				while(index && matches[index].hash!=*h) prev=index, index = matches[index].next_index;
				/*while(index && bufcmp( f->current, *(f->offset), matches[index].file, matches[index].offset, prefixlen)==0) prev=index, index = matches[index].next_index; */

				if(index)
				{
					hash_match* m = &(matches[index]);
					if(m->file!=f->current &&
								buffers_equal( f->current, *(f->offset), m->file, m->offset, prefixlen))
					{
						int b_copy;
						int v_copy;
						int l = 0; /*prefixlen; */
						int faults = 0;
						
						if(m->file==base)
						{
							b_copy = m->offset;
							v_copy = v_offset;
						}
						else
						{
							b_copy = b_offset;
							v_copy = m->offset; 
						}

						int ahead=0;

						/* since our hash function does not allow for holes we better 
						   scan backwards to see if perhaps we missed something */


						/* I think it is strange that only looping until v_start
						 * gives better results than looping all the way back to 0 
						 *
						 * probably not...
						 *
						 * going further back will result in overlaps with other 
						 * matches
						 *
						 * ahead scanning further than b_last_copy will screw up 
						 * the delta-storing further below
						 *
						 * */

						while( (b_copy-ahead)>b_last_copy && (v_copy-ahead)>v_start)
						{
							++ahead;

							unsigned int c_b = get_c(base, b_copy-ahead);
							unsigned int c_v = get_c(version,v_copy-ahead);


							if( c_b != c_v)
							{
								faults++; 
							}
							else
							{
								if(faults)
								{
									int d = get_delta(base,b_copy-(ahead-1),version,v_copy-(ahead-1),faults);

									if(num_dead_ends && binary_search( dead_ends, dead_ends+num_dead_ends, d))
									{
										ahead=faults=0; /* EXP */
										/* ahead-=(faults+1); */
										break;
									}
									faults=0;
								}
							}
							
							if(faults>tolerance) 
							{
								ahead-=faults;
								break;
							}
						}
						/* TODO there is a problem here, in that the loop may end for lack of data rather than 
						 * because of too many errors, while the data at the extreme left is actually in the
						 * dead-ends list. Try and check for this case here:
						 */

						if(ahead && faults)
						{
							int d = get_delta(base,b_copy-ahead,version,v_copy-ahead,faults);
							/*if(!d) printf("d is 0 at line %d\n",__LINE__);*/
							if(num_dead_ends && binary_search( dead_ends, dead_ends+num_dead_ends, d))
								ahead -= (faults);
							
							/* fprintf(stderr,"ahead %d %d faults %d\n",ahead,b-a,faults); */
						}

						faults=0;

						b_copy-=ahead;
						v_copy-=ahead;

						delta_entry* pdelta=0;

						unsigned char* pb = base->string+b_copy;
						unsigned char* pv = version->string+v_copy;
						/* while(b_copy+l<base->len && v_copy+l<version->len) */
						while(v_copy+l<version->len)
						{
							unsigned int c_b = pb[l];
							unsigned int c_v = pv[l];

							if(c_b != c_v)
							{
								faults++;
							}

							else if(faults)
							{
								/* if we correctly match the endianess of the
								   platform for which the binary is compiled, we
								   get better results. In the future, this should
								   be detected from the elf header or similar.

								   Right now, big endian is assumed, TODO fix this.
								 
								 */

								int d = get_delta(base,b_copy+l-faults,version,v_copy+l-faults,faults);
								/*if(!d) printf("d is 0 at line %d\n",__LINE__);*/

								if(num_dead_ends==0 || (!binary_search( dead_ends, dead_ends+num_dead_ends, d)))
								{
									if(little_endian)
									{
										pdelta = storedelta(d, tolerance, v_copy+l-faults );
									}
									else
									{
										pdelta = storedelta(d, tolerance, v_copy+l-tolerance);
									}

									faults = 0;
									num_deltas++;
								}
								else
								{
									l -= faults;
									break;
								}

							}


							if(faults > tolerance) 
							{
								l -= (faults-1);
								break;
							}

							if(pdelta) pdelta->bytes_saved++;


							l++;


						}

						if(pass==1)
						{
							addsize+=v_copy-v_start;
							num_adds++;
							store_hunk(base,version,v_start, v_copy, b_copy, l, res);
						}
						b_last_copy = b_copy;

						v_offset = v_copy+l;
						b_offset = b_copy+l;

						/* need to calc all fresh hashes from new offsets */
						h_b = h_v = 0;
						for(i=0; i<prefixlen; i++)
						{
							h_b =  B*h_b + get_c(base,b_offset+i);
							h_v =  B*h_v + get_c(version,v_offset+i);
						}

						/* in case of overrun we need to forcefully reset the hash table */
						num_matches=0;
						if(++run==0)
						{
							memset(hashes, 0, sizeof(hashes));
							run=1;
						}
						v_start = v_offset;

						continue; /* skip rehash, we just made full hashes */
					}
				}
				else 
				{

					if(++num_matches >= max_matches)
					{
						max_matches *= 2;
						matches = (hash_match*) realloc(matches, sizeof(hash_match) * max_matches);
					}

					if(e->run!=run)
					{
						e->run = run;
						e->first_index = num_matches;
					}
					else matches[prev].next_index = index;

					matches[num_matches].hash = *h;
					matches[num_matches].file = f->current;
					matches[num_matches].offset = *(f->offset);
					matches[num_matches].next_index = 0;
				}

			}
			h_b = B*(h_b - h_d*get_c(base,b_offset)) + get_c( base, b_offset+prefixlen);
			h_v = B*(h_v - h_d*get_c(version,v_offset)) + get_c( version, v_offset+prefixlen);

			v_offset++;
			b_offset++;
			
			if(v_offset>=version->len)
			{
				if(pass==1) store_hunk(base,version,v_start, v_offset, b_offset, 0, res);

				break;

			}
		}


		if(pass==0)
		{
			int max_dead_ends=256;
			dead_ends = (int*) malloc(max_dead_ends*sizeof(int));
			for(i=0; i<sizeof(deltas)/sizeof(delta_entry*); i++)
			{
				delta_entry* e;
				if( ( e = deltas[i] ) )
				{
					do
					{
						if(e->bytes_saved<8)
						{
							if(num_dead_ends==max_dead_ends)
							{
								max_dead_ends*=2;
								dead_ends = (int*) realloc(dead_ends, max_dead_ends*sizeof(int));
							}
							dead_ends[num_dead_ends++] = e->delta;
						}

					} while( (e=e->next) );
				}
			}

			qsort(dead_ends, num_dead_ends, sizeof(int), intcmp);

		}
	}

	storeint( 0, 4, res);


	int num_distinct=0;

#if 1
	for(i=0; i<sizeof(deltas)/sizeof(delta_entry*); i++)
	{
		delta_entry* e;
		if( (e = deltas[i]) )
		{
			do
			{
				int* es = (int*)((char*)e + sizeof(delta_entry));
				int j;
				int sub=0;
				int d = e->delta;

				/* storeint(d<0 ? (((-d)<<1)|1) : (d<<1),1, res); */
				storeanyint(d,res);
				storeint(e->num_elems, 1, res);
				
				for(j=0; j<e->num_elems; j++)
				{
					int v = es[j]-sub;

					int rept=0;
					while(v == es[j+1]-es[j] && j<e->num_elems)
					{
						j++;
						rept++;
					}
					storeint(swap(v), rept+1, res);

					sub = es[j];
				}

				num_distinct++;

			} while( (e=e->next) );

		}
	}
#endif
	/* storeint( 0,1,res); */
	storeanyint( 0,res);

	if(!quiet) fprintf(stderr,"%d (%d bytes) adds, %d/%d deltas\n",num_adds,addsize, num_deltas, num_distinct);
}

void reconstruct(FILE* base,buffer* dest, gzFile patch)
{
	dest->len = readint(patch);
	dest->string = (unsigned char*)malloc(dest->len);

	int v_start = 0;
	int v_copy = 0;
	int b_copy = 0;

	while(1)
	{
		int v_start_delta = readint(patch);
		int v_copy_delta = readint(patch);
		int b_copy_delta = readint(patch);
		int l = readint(patch);
		v_start += v_start_delta;
		v_copy += v_copy_delta;
		b_copy += b_copy_delta;

		if(l==0 && v_start_delta==0 && v_copy_delta==0 && b_copy_delta==0) break;

		gzread(patch, dest->string+v_start, v_copy-v_start);
		fseek(base, b_copy, SEEK_SET);
		fread(dest->string+v_copy, l,1, base);
	}

	while(1)
	{
	  /*	unsigned int val; */
		int delta,width,num_elems;
		int offset=0, i;
		
		/*	if(! (val = readint(patch)) ) break; */

		if(! (delta = readanyint(patch))) break;
#if 0
		if(val&1) delta = -(val>>1);
		else delta = val>>1;
#endif
		
		width=tolerance;
		num_elems = readint(patch);

		for(i=0; i<num_elems; i++)
		{
			unsigned int a=0;
			unsigned char *pval;

			offset += swap(readint(patch));

			pval = dest->string+offset;

			int j;

			if(little_endian)
			{
				unsigned int d,w;
				for(d=(delta<0? -delta:delta),w=0; d; d>>=8,w++);

				for(j=0; j<width; j++)
				{
					a = (a<<8) | pval[(width-1)-j];
				}

				a += delta;

				for(j=0; j<width; j++)
				{
					pval[j] = a;
					a >>= 8;
				}
			}



			else /* big endian */
			{

				for(j=0; j<width; j++)
				{
					a = (a<<8) | pval[j];
				}

				a += delta;

				for(j=width-1; j>=0; j--)
				{
					pval[j] = a;
					a >>= 8;
				}
			}

		}
	}


}

void banner()
{
	fprintf(stderr,"\nEDelta executable differ, version " VERSION ".");
	fprintf(stderr,"\nCopyright 2003-2006 Jacob Gorm Hansen.\n\n");
	fprintf(stderr,"Licensed under the GNU Public License. See the file COPYING for details.\n\n");
}

void usage(char* appname)
{

	fprintf(stderr,"Usage: %s [-q] delta BASE VERSION [PATCH]\n",appname);
	fprintf(stderr,"  or : %s [-q] patch BASE VERSION [PATCH]\n\n",appname);
	fprintf(stderr,"  if [PATCH] is not specified it will be written to/read from stdout/in\n");
	exit(-1);

}

int main(int argc, char** argv)
{

	int i;
	char* appname = *argv++;

	if(argc>1 && !strcmp(*argv,"-q"))
	{
		quiet=1;
		++argv;
		--argc;
	}
	else banner();

	if(argc>1 && !strcmp(*argv,"-le"))
	{
		little_endian=1;
		++argv;
		--argc;
	}

	buffer buffers[2];

	if(argc>=4 && !strcmp(*argv,"delta"))
	{
		++argv;

		for(i=0; i<2; i++)
		{
			struct stat s;
			FILE* f;
			char* name = *argv++;
			stat(name,&s);

			buffers[i].string = (unsigned char*)malloc(s.st_size);
			buffers[i].len = s.st_size;
			f = fopen(name, "rb");
			if(!f)
			{
				fprintf(stderr,"file open error on %s\n", name);
				exit(-1);
			}
			fread(buffers[i].string, s.st_size, 1, f);
			fclose(f);
		}
		
		if(argc==4)
		{
			if(!quiet) fprintf(stderr,"(writing patch to stdout)\n\n");
			res = gzdopen(1,"wb");
		}
		else res = gzopen(*argv,"wb");

		gzsetparams(res,9,Z_DEFAULT_STRATEGY);

		if(!res)
		{
			fprintf(stderr,"output file open error on %s\n", *argv);
			exit(-1);
		}
		gzwrite(res,"edelta",6);
		gzputc(res,EDELTA_PATCH_VERSION);

		/* write out sha1 hashes of new and old version */
		for(i=0; i<2; i++)
		{
			unsigned char digest[20];

			sha1_digest(digest,buffers[i].string,buffers[i].len);
			gzwrite(res,digest,sizeof(digest));
		}

		diff(&(buffers[0]), &(buffers[1]));
		gzclose(res);
	}

	else if(argc>=4 && !strcmp(*argv,"patch"))
	{
		++argv;
		unsigned char digest[20];
		char digest1[20];
		char digest2[20];
		FILE *base,*version;
		gzFile patch;
		char* base_filename = *argv++;
		char* version_filename = *argv++;

		base = fopen(base_filename, "rb");

		if(argc==4)
		{
			if(!quiet) fprintf(stderr,"(reading patch from stdin)\n\n");
			patch = gzdopen(0,"rb");
		}
		else
		{
			patch = gzopen(*argv++, "rb");
		}
		if(!base || !patch)
		{
			puts("input file open error\n");
			exit(-1);
		}

		char header[6];
		gzread(patch,header,sizeof(header));
		if( strncmp(header,"edelta",6) || gzgetc(patch)!=EDELTA_PATCH_VERSION)
		{
			fprintf(stderr,"Unsupported patch version!\n");
			exit(-1);
		}

		gzread(patch,digest1,sizeof(digest1));
		gzread(patch,digest2,sizeof(digest2));

		reconstruct(base, &buffers[1], patch);
		fclose(base);
		gzclose(patch);

		version = fopen(version_filename, "wb+");
		sha1_digest(digest,buffers[1].string, buffers[1].len);
		if(memcmp(digest,digest2,sizeof(digest)))
		{
			puts("digest match error!");
			exit(-1);
		}
		fwrite(buffers[1].string, buffers[1].len, 1, version);
		fclose(version);
	}
	
	else usage(appname);


	return 0;
}
