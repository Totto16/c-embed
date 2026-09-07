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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef u_int32_t hash_t;

hash_t hash(const char *key) { // Hash Function: MurmurOAAT64
  hash_t h = 3323198485ul;
  for (; *key; ++key) {
    h ^= *key;
    h *= 0x5bd1e995;
    h ^= h >> 15;
  }
  return h;
}

typedef size_t epos_t;

typedef struct EMAP_ENTRY_FILE_S {
  u_int32_t file_size;
} __attribute__((packed)) EMAP_ENTRY_FILE;

typedef struct EMAP_ENTRY_DIR_S {
  u_int32_t file_array_size;
} __attribute__((packed)) EMAP_ENTRY_DIR;

#define EMAP_ENTRY_TYPE_FILE 0
#define EMAP_ENTRY_TYPE_DIR 1

typedef struct EMAP_ENTRY_S {
  u_int8_t type;
  union {
    EMAP_ENTRY_FILE file;
    EMAP_ENTRY_DIR dir;
  } data;
} __attribute__((packed)) EMAP_ENTRY;

#define NEW_EMAP_ENTRY_FILE(size)                                              \
  ((EMAP_ENTRY){.type = EMAP_ENTRY_TYPE_FILE,                                  \
                .data = {.file = (EMAP_ENTRY_FILE){.file_size = (size)}}})

#define NEW_EMAP_ENTRY_DIR(size)                                               \
  ((EMAP_ENTRY){.type = EMAP_ENTRY_TYPE_DIR,                                   \
                .data = {.dir = (EMAP_ENTRY_DIR){.file_array_size = (size)}}})

typedef struct EMAP_S { // Map Indexing Struct
  hash_t hash;
  u_int32_t pos;
  EMAP_ENTRY entry;
} __attribute__((packed)) EMAP;

typedef u_int8_t byte_t;

typedef struct EFILE_S { // Virtual File Stream
  byte_t *pos;
  byte_t *end;
  u_int32_t size;
  EMAP_ENTRY entry;
} EFILE;

typedef struct edirent_s {
  u_int8_t type;
  const char *name;
} edirent;

typedef struct {
  hash_t hash;
} DirEntryProps;

typedef struct {
  DirEntryProps properties;
  const char *name;
} DirEntryDynamic;

// ([...('DirC').split("").map(a=>a.charCodeAt(0).toString(16)),
// "0x"].reverse().join(""))
#define DIR_ENTRY_COOKIE 0x43726944 ///< 'DirC'

typedef u_int32_t cookie_t;

// Error Handling

#if defined(__UEFI__)
#define THREAD_LOCAL
#else
#define THREAD_LOCAL __thread
#endif

THREAD_LOCAL int eerrcode = 0;

#define ethrow(err)                                                            \
  {                                                                            \
    (eerrcode = (err));                                                        \
    return NULL;                                                               \
  }
#define eerrno (eerrcode)

#define EERRCODE_SUCCESS 0
#define EERRCODE_NOFILE 1
#define EERRCODE_NOMAP 2
#define EERRCODE_NULLSTREAM 3
#define EERRCODE_OOBSTREAMPOS 4
#define EERRCODE_INVALID_MODE 5
#define EERRCODE_INVALID_ARGUMENTS 6
#define EERRCODE_INTERNAL_ERROR 7
#define EERRCODE_IS_DIRECTORY 8
#define EERRCODE_IS_FILE 9

const char *eerrstr(int e) {
  switch (e) {
  case EERRCODE_SUCCESS:
    return "Success.";
  case EERRCODE_NOFILE:
    return "No file found.";
  case EERRCODE_NOMAP:
    return "Mapping stucture error.";
  case EERRCODE_NULLSTREAM:
    return "File stream pointer is NULL.";
  case EERRCODE_OOBSTREAMPOS:
    return "File stream pointer is out-of-bounds.";
  case EERRCODE_INVALID_MODE:
    return "Invalid mode";
  case EERRCODE_INVALID_ARGUMENTS:
    return "Invalid arguments";
  case EERRCODE_INTERNAL_ERROR:
    return "Internal error";
  case EERRCODE_IS_DIRECTORY:
    return "is a directory";
  case EERRCODE_IS_FILE:
    return "is a file";
  default:
    return "Unknown cembed error code.";
  };
}

int eerrno_to_errno(int eerrno) {
  switch (eerrno) {
  case EERRCODE_SUCCESS:
    return 0;
  case EERRCODE_NOFILE:
    return ENOENT;
  case EERRCODE_NOMAP:
    return ENODEV;
  case EERRCODE_NULLSTREAM:
    return EINVAL;
  case EERRCODE_OOBSTREAMPOS:
    return EINVAL;
  case EERRCODE_INVALID_MODE:
    return EINVAL;
  case EERRCODE_INVALID_ARGUMENTS:
    return EINVAL;
  case EERRCODE_INTERNAL_ERROR:
    return EINVAL;
  case EERRCODE_IS_DIRECTORY:
    return EISDIR;
  case EERRCODE_IS_FILE:
    return EBADF;
  default:
    return EINVAL;
  };
}

#define eerror(c) printf("%s: (%u) %s\n", c, eerrcode, eerrstr(eerrcode))

// File Useage

#ifndef CEMBED_BUILD

extern byte_t cembed_map_start; // Embedded Indexing Structure
extern byte_t cembed_map_end;
extern byte_t cembed_map_size;

extern byte_t cembed_fs_start; // Embedded Virtual File System
extern byte_t cembed_fs_end;
extern byte_t cembed_fs_size;

EFILE *eopen(const char *file, const char *mode) {

  int required_type = -1;

  if (strcmp(mode, "r") != 0) {
    if (strcmp(mode, "d") == 0) {
      required_type = EMAP_ENTRY_TYPE_DIR;
    } else if (strcmp(mode, "f") == 0) {
      required_type = EMAP_ENTRY_TYPE_FILE;
    } else {
      ethrow(EERRCODE_INVALID_MODE);
    }
  }

  EMAP *map = (EMAP *)(&cembed_map_start);
  const byte_t *end = &cembed_map_end;

  if (map == NULL || end == NULL) {
    ethrow(EERRCODE_NOMAP);
  }

  const u_int32_t key = hash((char *)file);
  while (((byte_t *)map != end) && (map->hash != key)) {
    map++;
  }

  if (map->hash != key) {
    ethrow(EERRCODE_NOFILE);
  }

  if (required_type >= 0) {
    if (required_type != map->entry.type) {
      ethrow(EERRCODE_INVALID_MODE);
    }
  }

  EFILE *e = (EFILE *)malloc(sizeof(*e));
  e->pos = (&cembed_fs_start + map->pos);
  e->entry = map->entry;

  if (map->entry.type == EMAP_ENTRY_TYPE_FILE) {
    e->size = map->entry.data.file.file_size;
  } else if (map->entry.type == EMAP_ENTRY_TYPE_DIR) {
    e->size = map->entry.data.dir.file_array_size;
  } else {
    free(e);
    ethrow(EERRCODE_INTERNAL_ERROR);
  }

  e->end = (&cembed_fs_start + (map->pos + e->size));

  return e;
}

void eclose(EFILE *e) {
  free(e);
  e = NULL;
}

#define E_START(e) ((e)->end - (e)->size)

int estreamtype(EFILE *e) {
  if (e == NULL) {
    return -1;
  }

  if (e->entry.type == EMAP_ENTRY_TYPE_FILE) {
    return EMAP_ENTRY_TYPE_FILE;
  } else if (e->entry.type == EMAP_ENTRY_TYPE_DIR) {
    return EMAP_ENTRY_TYPE_DIR;
  } else {
    return -1;
  }
}

bool eeof(EFILE *e) {
  if (e == NULL) {
    (eerrcode = (EERRCODE_NULLSTREAM));
    return true;
  }
  if (e->end < e->pos) {
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return true;
  }
  if (e->pos < E_START(e)) {
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return true;
  }

  (eerrcode = (EERRCODE_SUCCESS));
  return (e->end == e->pos);
}

size_t eread(void *ptr, size_t size, size_t count, EFILE *stream) {

  if (stream->entry.type != EMAP_ENTRY_TYPE_FILE) {
    (eerrcode = (EERRCODE_IS_DIRECTORY));
    return 0;
  }

  bool eof = eeof(stream);
  if (eerrcode != EERRCODE_SUCCESS) {
    return 0;
  }

  if (eof) {
    (eerrcode = (EERRCODE_SUCCESS));
    return 0;
  }

  if ((size_t)(stream->end - stream->pos) < size * count) {
    size_t scount = stream->end - stream->pos;
    memcpy(ptr, (void *)stream->pos, scount);
    stream->pos = stream->end;

    (eerrcode = (EERRCODE_SUCCESS));
    return (scount / size);
  }

  memcpy(ptr, (void *)stream->pos, size * count);

  stream->pos = stream->pos + (size * count);

  (eerrcode = (EERRCODE_SUCCESS));
  return count;
}

#define EREADDIR_FINISHED -1

int ereaddir(EFILE *stream, edirent *ent) {
  if (stream->entry.type != EMAP_ENTRY_TYPE_DIR) {
    return EERRCODE_IS_FILE;
  }

  if (stream->end == stream->pos) {
    return EREADDIR_FINISHED;
  }

  // The data stored inside the dir entry is dynamically sized
  //  it is like this:
  //  (<cookie_t> cookie | <DirEntryProps> props | char * ... )*

  size_t remaining_size = (size_t)(stream->end - stream->pos);

  if (remaining_size < sizeof(cookie_t)) {
    return EERRCODE_INTERNAL_ERROR;
  }

  remaining_size -= sizeof(cookie_t);
  cookie_t *entry_cookie = (cookie_t *)stream->pos;
  stream->pos += sizeof(cookie_t);

  if (*entry_cookie != DIR_ENTRY_COOKIE) {
    return EERRCODE_INTERNAL_ERROR;
  }

  if (remaining_size < sizeof(DirEntryProps)) {
    return EERRCODE_INTERNAL_ERROR;
  }
  remaining_size -= sizeof(DirEntryProps);
  DirEntryProps *properties = (DirEntryProps *)stream->pos;
  stream->pos += sizeof(DirEntryProps);

  DirEntryProps props = *properties;

  // find entry based on hash
  EMAP *map = (EMAP *)(&cembed_map_start);
  const byte_t *end = &cembed_map_end;

  if (map == NULL || end == NULL) {
    return EERRCODE_NOMAP;
  }

  while (((byte_t *)map != end) && (map->hash != props.hash)) {
    map++;
  }

  if (map->hash != props.hash) {
    return EERRCODE_NOFILE;
  }

  if (remaining_size < 1) {
    return EERRCODE_INTERNAL_ERROR;
  }

  const char *const entry_name = (const char *)stream->pos;

  for (size_t i = 0; remaining_size != 0; --remaining_size, ++i) {
    char value = entry_name[i];

    if (value == '\0') {
      stream->pos += i + 1;
      break;
    }
  }

  *ent = (edirent){.name = entry_name, .type = map->entry.type};
  return EERRCODE_SUCCESS;
}

int egetpos(EFILE *e, epos_t *pos) {

  if (e->end <= e->pos) {
    pos = NULL;
    return 1;
  }

  *pos = (epos_t)(e->end - e->pos);
  return 0;
}

char *egets(char *str, int num, EFILE *stream) {

  if (stream->entry.type != EMAP_ENTRY_TYPE_FILE) {
    return NULL;
  }

  if (eeof(stream)) {
    return NULL;
  }

  for (int i = 0; i < num && !eeof(stream) && *(stream->pos) != '\r'; i++) {
    str[i] = *(stream->pos++);
  }

  return str;
}

int egetc(EFILE *stream) {
  if (stream->entry.type != EMAP_ENTRY_TYPE_FILE) {
    return -1;
  }

  if (eeof(stream)) {
    return -1;
  }
  return (int)(*(stream->pos++));
}

long int etell(EFILE *e) {
  if (e->end < e->pos) {
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return -1;
  }
  if (e->pos < E_START(e)) {
    (eerrcode = (EERRCODE_OOBSTREAMPOS));
    return -1;
  }

  (eerrcode = (EERRCODE_SUCCESS));
  return e->pos - E_START(e);
}

void erewind(EFILE *e) { e->pos = (e->end - e->size); }

int eseek(EFILE *stream, long int offset, int origin) {

  if (stream->entry.type != EMAP_ENTRY_TYPE_FILE) {
    (eerrcode = (EERRCODE_INVALID_ARGUMENTS));
    return -1;
  }

  if (origin == SEEK_SET) {
    stream->pos = E_START(stream) + offset;
  } else if (origin == SEEK_CUR) {
    stream->pos += offset;
  } else if (origin == SEEK_END) {
    stream->pos = stream->end + offset;
  } else {
    (eerrcode = (EERRCODE_INVALID_ARGUMENTS));
    return -1;
  }

  if (stream->end < stream->pos || etell(stream) < 0) {
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
