/*
# c-embed
# embed virtual file systems into an c program
# - at build time
# - with zero dependencies
# - with zero code modifications
# - with zero clutter in your program
# author: nicholas mcdonald 2022
*/

#ifndef CEMBED
#define CEMBED

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include <string.h>

u_int32_t hash(const char * key){   // Hash Function: MurmurOAAT64
  u_int32_t h = 3323198485ul;
  for (;*key;++key) {
    h ^= *key;
    h *= 0x5bd1e995;
    h ^= h >> 15;
  }
  return h;
}

typedef size_t epos_t;

struct EMAP_S {     // Map Indexing Struct
  u_int32_t hash;
  u_int32_t pos;
  u_int32_t size;
};
typedef struct EMAP_S EMAP;

struct EFILE_S {    // Virtual File Stream
  char* pos;
  char* end;
  size_t size;
  int err;
};
typedef struct EFILE_S EFILE;

// Error Handling

#if defined(__UEFI__)
#define THREAD_LOCAL
#else
#define THREAD_LOCAL __thread
#endif


THREAD_LOCAL int eerrcode = 0;


#define ethrow(err) { (eerrcode = (err)); return NULL; }
#define eerrno (eerrcode)

#define EERRCODE_SUCCESS 0
#define EERRCODE_NOFILE 1
#define EERRCODE_NOMAP 2
#define EERRCODE_NULLSTREAM 3
#define EERRCODE_OOBSTREAMPOS 4
#define EERRCODE_INVALIDMODE 5
#define EERRCODE_INVALIARGUMENTS 6

const char* eerrstr(int e){
switch(e){
  case EERRCODE_SUCCESS: return "Success.";
  case EERRCODE_NOFILE: return "No file found.";
  case EERRCODE_NOMAP: return "Mapping stucture error.";
  case EERRCODE_NULLSTREAM: return "File stream pointer is NULL.";
  case EERRCODE_OOBSTREAMPOS: return "File stream pointer is out-of-bounds.";
  case EERRCODE_INVALIDMODE: return "Invalid mode";
  case EERRCODE_INVALIARGUMENTS: return "Invalid arguments";
  default: return "Unknown cembed error code.";
};
};

#define eerror(c) printf("%s: (%u) %s\n", c, eerrcode, eerrstr(eerrcode))

// File Useage

#ifndef CEMBED_BUILD

extern char cembed_map_start; // Embedded Indexing Structure
extern char cembed_map_end;
extern char cembed_map_size;

extern char cembed_fs_start;  // Embedded Virtual File System
extern char cembed_fs_end;
extern char cembed_fs_size;

EFILE* eopen(const char* file, const char* mode){

  if(strcmp(mode,"r") != 0){
    ethrow(EERRCODE_INVALIDMODE);
  }

  EMAP* map = (EMAP*)(&cembed_map_start);
  const char* end = &cembed_map_end;

  if( map == NULL || end == NULL ){
    ethrow(EERRCODE_NOMAP);
  }

  const u_int32_t key = hash((char*)file);
  while( ((char*)map != end) && (map->hash != key) ){
    map++;
  }

  if(map->hash != key){
    ethrow(EERRCODE_NOFILE);
  }

  EFILE* e = (EFILE*)malloc(sizeof(*e));
  e->pos = (&cembed_fs_start + map->pos);
  e->end = (&cembed_fs_start + (map->pos + map->size));
  e->size = map->size;

  return e;

}

void eclose(EFILE* e){
  free(e);
  e = NULL;
}

#define E_START(e) ((e)->end - (e)->size)

bool eeof(EFILE* e){
  if(e == NULL){
    (eerrcode = (EERRCODE_NULLSTREAM));
    return true;
  }
  if(e->end < e->pos){
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return true;
  }
  if(e->pos < E_START(e)){
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return true;
  }

  (eerrcode = (EERRCODE_SUCCESS));
  return (e->end == e->pos);
}

size_t eread(void* ptr, size_t size, size_t count, EFILE* stream){

  bool eof = eeof(stream);
  if(eerrcode != EERRCODE_SUCCESS){
    return 0;
  }

  if(eof){
    (eerrcode = (EERRCODE_SUCCESS));
    return 0;
  }

  if((size_t)(stream->end - stream->pos) < size*count){
    size_t scount = stream->end - stream->pos;
    memcpy(ptr, (void*)stream->pos, scount);
    stream->pos = stream->end;
    
    (eerrcode = (EERRCODE_SUCCESS));
    return (scount/size);
  }

  memcpy(ptr, (void*)stream->pos, size*count);
  
  (eerrcode = (EERRCODE_SUCCESS));
  return count;

}

int egetpos(EFILE* e, epos_t* pos){

  if(e->end <= e->pos){
    pos = NULL;
    return 1;
  }

  *pos = (epos_t)(e->end - e->pos);
  return 0;

}

char* egets ( char* str, int num, EFILE* stream ){

  if(eeof(stream))
    return NULL;

  for(int i = 0; i < num && !eeof(stream) && *(stream->pos) != '\r'; i++)
    str[i] = *(stream->pos++);

  return str;

}

int egetc ( EFILE* stream ){
  if(eeof(stream))
    return -1;
  return (int)(*(stream->pos++));
}

long int etell(EFILE* e){
  if(e->end < e->pos){
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return -1;
  }
  if(e->pos < E_START(e)){
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return -1;
  }

  (eerrcode = (EERRCODE_SUCCESS));
  return e->pos - E_START(e);
}

void erewind(EFILE* e){
  e->pos = (e->end - e->size);
}

int eseek ( EFILE* stream, long int offset, int origin ){

  if(origin == SEEK_SET){
    stream->pos = E_START(stream) + offset;
  }else if(origin == SEEK_CUR){
    stream->pos += offset;
  }else if(origin == SEEK_END){
    stream->pos = stream->end + offset;
  }else{
    (eerrcode = (EERRCODE_INVALIARGUMENTS));
    return -1;
  }

  if(stream->end < stream->pos || etell(stream)  < 0){
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return -1;
  }

  (eerrcode = (EERRCODE_SUCCESS));
  return 0;

}

// Preprocessor Translation

#ifdef CEMBED_TRANSLATE
#define FILE EFILE
#define fopen eopen
#define fclose eclose
#define feof eeof
#define fgets egets
#define fgetc egetc
#define perror eerror
#define fread eread
#define fseek eseek
#define ftell etell
#define rewind erewind
#endif

#endif
#endif
