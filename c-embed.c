/*
# c-embed
# embed virtual file systems into an c program
# - at build time
# - with zero dependencies
# - with zero code modifications
# - with zero clutter in your program
# author: nicholas mcdonald 2022
*/

#define CEMBED_BUILD

#define _POSIX_C_SOURCE 200809L
#include <string.h>

#include "c-embed.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>

typedef hash_t HASH_VEC_ITEM;

typedef struct {
  size_t size;
  size_t capacity;
  HASH_VEC_ITEM *items;
} HashVec;

typedef DirEntryDynamic FILE_VEC_ITEM;

typedef struct {
  size_t size;
  size_t capacity;
  FILE_VEC_ITEM *items;
} FileVec;

typedef struct {
  hash_t hash;
  FileVec files;
} DirVecEntry;

typedef DirVecEntry DIR_VEC_ITEM;

typedef struct {
  size_t size;
  size_t capacity;
  DIR_VEC_ITEM *items;
} DirectoryVec;

#define INITIAL_VEC_CAPACITY 8
#define VEC_CAPACITY_MULT 2

static void hash_vec_init(HashVec *vec) {

  HASH_VEC_ITEM *items = malloc(sizeof(HASH_VEC_ITEM) * INITIAL_VEC_CAPACITY);

  assert(items != NULL);

  *vec = (HashVec){.size = 0, .capacity = INITIAL_VEC_CAPACITY, .items = items};
}

static void hash_vec_destroy(HashVec *vec) { free(vec->items); }

static void hash_vec_add(HashVec *vec, HASH_VEC_ITEM item) {

  if (vec->size == vec->capacity) {
    size_t new_capacity = vec->capacity * VEC_CAPACITY_MULT;
    HASH_VEC_ITEM *new_items =
        realloc(vec->items, sizeof(HASH_VEC_ITEM) * new_capacity);

    assert(new_items != NULL);

    vec->capacity = new_capacity;
    vec->items = new_items;
  }

  vec->items[vec->size] = item;

  ++(vec->size);
}

static void file_vec_init(FileVec *vec) {

  FILE_VEC_ITEM *items = malloc(sizeof(FILE_VEC_ITEM) * INITIAL_VEC_CAPACITY);

  assert(items != NULL);

  *vec = (FileVec){.size = 0, .capacity = INITIAL_VEC_CAPACITY, .items = items};
}

static void file_vec_destroy(FileVec *vec) {

  for (size_t i = 0; i < vec->size; ++i) {
    FILE_VEC_ITEM *item = &(vec->items[i]);

    free((void *)item->name);
  }

  free(vec->items);
}

static void file_vec_add(FileVec *vec, FILE_VEC_ITEM item) {

  if (vec->size == vec->capacity) {
    size_t new_capacity = vec->capacity * VEC_CAPACITY_MULT;
    FILE_VEC_ITEM *new_items =
        realloc(vec->items, sizeof(FILE_VEC_ITEM) * new_capacity);

    assert(new_items != NULL);

    vec->capacity = new_capacity;
    vec->items = new_items;
  }

  vec->items[vec->size] = item;

  ++(vec->size);
}

static void dir_vec_init(DirectoryVec *vec) {

  DIR_VEC_ITEM *items = malloc(sizeof(DIR_VEC_ITEM) * INITIAL_VEC_CAPACITY);

  assert(items != NULL);

  *vec = (DirectoryVec){
      .size = 0, .capacity = INITIAL_VEC_CAPACITY, .items = items};
}

static void dir_vec_destroy(DirectoryVec *vec) {
  for (size_t i = 0; i < vec->size; ++i) {
    DIR_VEC_ITEM *item = &(vec->items[i]);

    file_vec_destroy(&(item->files));
  }

  free(vec->items);
}

static void dir_vec_add(DirectoryVec *vec, DIR_VEC_ITEM item) {

  if (vec->size == vec->capacity) {
    size_t new_capacity = vec->capacity * VEC_CAPACITY_MULT;
    DIR_VEC_ITEM *new_items =
        realloc(vec->items, sizeof(DIR_VEC_ITEM) * new_capacity);

    assert(new_items != NULL);

    vec->capacity = new_capacity;
    vec->items = new_items;
  }

  vec->items[vec->size] = item;

  ++(vec->size);
}

typedef struct {
  FILE *ms;      // Mapping Structure
  FILE *fs;      // Virtual Filesystem
  u_int32_t pos; // Current Position
  HashVec hash_vec;
  DirectoryVec dir_vec;
} GlobalThings;

static void assert_hash_is_unique(HASH_VEC_ITEM hash_value,
                                  HashVec *const hash_vec) {

  for (size_t i = 0; i < hash_vec->size; ++i) {
    HASH_VEC_ITEM item = hash_vec->items[i];

    if (item == hash_value) {

      fprintf(stderr, "Duplicate hash detected: %u == %u\n", item, hash_value);
      exit(3);
    }
  }

  hash_vec_add(hash_vec, hash_value);
}

static void assert_dir_is_unique(DIR_VEC_ITEM dir_value,
                                 DirectoryVec *const dir_vec) {

  for (size_t i = 0; i < dir_vec->size; ++i) {
    DIR_VEC_ITEM item = dir_vec->items[i];

    if (item.hash == dir_value.hash) {

      fprintf(stderr, "Duplicate dir detected: %u == %u\n", item.hash,
              dir_value.hash);
      exit(3);
    }
  }

  dir_vec_add(dir_vec, dir_value);
}

static void assert_name_matches(const char *const parent_directory,
                                const char *const entry_name,
                                const char *const whole_name) {

  if (parent_directory == NULL) {
    if (entry_name != NULL) {
      fprintf(stderr, "names don't match (%d): %s != %s/%s\n", __LINE__,
              whole_name, parent_directory, entry_name);
      exit(3);
    }
    return;
  }

  size_t parent_len = strlen(parent_directory);
  size_t entry_len = strlen(entry_name);
  size_t whole_len = strlen(whole_name);

  if (parent_len + entry_len + 1 != whole_len) {
    fprintf(stderr, "names don't match (%d): %s != %s/%s\n", __LINE__,
            whole_name, parent_directory, entry_name);
    exit(3);
  }

  assert(whole_len > parent_len);

  if (strncmp(parent_directory, whole_name, parent_len) != 0) {
    fprintf(stderr, "names don't match (%d): %s != %s/%s\n", __LINE__,
            whole_name, parent_directory, entry_name);
    exit(3);
  }

  if (whole_name[parent_len] != '/') {
    fprintf(stderr, "names don't match (%d): %s != %s/%s\n", __LINE__,
            whole_name, parent_directory, entry_name);
    exit(3);
  }

  assert(whole_len - parent_len > entry_len);

  if (strncmp(entry_name, whole_name + parent_len + 1, entry_len) != 0) {
    fprintf(stderr, "names don't match (%d): %s != %s/%s\n", __LINE__,
            whole_name, parent_directory, entry_name);
    exit(3);
  }
}

static void add_file_to_dir(const char *parent_directory,
                            hash_t parent_dir_hash, hash_t entry_hash,
                            const char *entry_name,
                            DirectoryVec *const dir_vec) {

  for (size_t i = 0; i < dir_vec->size; ++i) {
    DIR_VEC_ITEM *item = &(dir_vec->items[i]);

    if (item->hash == parent_dir_hash) {
      FILE_VEC_ITEM file_entry = {.properties =
                                      (DirEntryProps){.hash = entry_hash},
                                  .name = strdup(entry_name)};
      file_vec_add(&(item->files), file_entry);
      return;
    }
  }

  fprintf(stderr, "No such dir found: %s hash: %u\n", parent_directory,
          parent_dir_hash);
  exit(3);
}

static hash_t get_hash_relative(const char *const entry, const char *root_dir) {
  if (root_dir == NULL) {
    fprintf(stderr, "Invalid root: %s\n", root_dir);
    exit(2);
  }

  if (entry == NULL) {
    return hash("/");
  }

  const char *entry_relative = entry;
  const size_t entry_len = strlen(entry);
  const size_t root_len = strlen(root_dir);
  if (entry_len < root_len) {
    fprintf(stderr, "Invalid file root: %s\n", root_dir);
    exit(2);
  }
  for (size_t i = 0; i < root_len; ++i) {
    if (entry[i] == root_dir[i]) {
      entry_relative++;
    }
  }

  return hash(entry_relative);
}

static void cembed(const char *const filename, const char *root_dir,
                   GlobalThings *things, const char *parent_directory,
                   bool is_dir, const char *entry_name) {

  hash_t entry_hash = get_hash_relative(filename, root_dir);

  assert_hash_is_unique(entry_hash, &(things->hash_vec));

  if (is_dir) {

    DirVecEntry dir_entry = (DirVecEntry){.hash = entry_hash, .files = {}};
    file_vec_init(&(dir_entry.files));

    assert_dir_is_unique(dir_entry, &(things->dir_vec));

  } else {

    FILE *file = fopen(filename, "rb"); // Open the Embed Target File
    if (file == NULL) {
      fprintf(stderr, "Failed to open file %s.", filename);
      exit(4);
    }
    fseek(file, 0, SEEK_END); // Define Map
    u_int32_t file_size = (u_int32_t)ftell(file);
    rewind(file);

    EMAP_ENTRY entry = NEW_EMAP_ENTRY_FILE(file_size);

    char *buf = malloc(sizeof(char) * file_size);
    if (buf == NULL) {
      fprintf(stderr, "Memory error for file %s.", filename);
      exit(4);
    }

    u_int32_t result = fread(buf, 1, file_size, file);
    if (result != file_size) {
      fprintf(stderr, "Read error for file %s.", filename);
      exit(4);
    }

    EMAP map = {
        .hash = entry_hash,
        .pos = things->pos,
        .entry = entry,
    };

    fwrite(&map, sizeof(map), 1,
           things->ms); // Write Mapping Structure
    fwrite(buf, file_size, 1,
           things->fs); // Write Virtual Filesystem

    free(buf);                // Free Buffer
    fclose(file);             // Close the File
    things->pos += file_size; // Shift the Index Position
  }

  hash_t parent_dir_hash = get_hash_relative(parent_directory, root_dir);

  assert_name_matches(parent_directory, entry_name, filename);

  if (parent_directory != NULL) {
    add_file_to_dir(parent_directory, parent_dir_hash, entry_hash, entry_name,
                    &(things->dir_vec));
  }
}

#define CEMBED_DIRENT_FILE 8
#define CEMBED_DIRENT_DIR 4
#define CEMBED_MAXPATH 512

static void iterdir(const char *const d, const char *root_dir,
                    GlobalThings *things) {

  char *fullpath = (char *)malloc(CEMBED_MAXPATH * sizeof(char));

  struct dirent *ent;

  DIR *dir = opendir(d);

  if (dir != NULL) {

    while ((ent = readdir(dir)) != NULL) {

      if (strcmp(ent->d_name, ".") == 0) {
        continue;
      }
      if (strcmp(ent->d_name, "..") == 0) {
        continue;
      }

      if (ent->d_type == CEMBED_DIRENT_FILE) {
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        cembed(fullpath, root_dir, things, d, false, ent->d_name);
      } else if (ent->d_type == CEMBED_DIRENT_DIR) {
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        cembed(fullpath, root_dir, things, d, true, ent->d_name);
        iterdir(fullpath, root_dir, things);
      } else {
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        fprintf(stderr, "Ignored entry of type %d: %s\n", ent->d_type,
                fullpath);
      }
    }

    closedir(dir);

  }

  else {

    fprintf(stderr, "Couldn't open dir: %s -> %s\n", d, strerror(errno));
    exit(2);
  }

  free(fullpath);
}

static bool is_directory(const char *file) {
  DIR *dir = opendir(file);
  if (dir != NULL) {
    closedir(dir);
    return true;
  }

  return false;
}

void process_dirs(GlobalThings *things) {
  for (size_t i = 0; i < things->dir_vec.size; ++i) {
    const DIR_VEC_ITEM *dir = &(things->dir_vec.items[i]);

    size_t dir_size = 0;

    for (size_t i = 0; i < dir->files.size; ++i) {
      const FILE_VEC_ITEM *file = &(dir->files.items[i]);

      cookie_t entry_cookie = DIR_ENTRY_COOKIE;
      fwrite(&entry_cookie, sizeof(cookie_t), 1, things->fs);
      dir_size += sizeof(cookie_t);

      fwrite(&(file->properties), sizeof(DirEntryProps), 1, things->fs);
      dir_size += sizeof(DirEntryProps);

      const size_t name_size = strlen(file->name);

      fwrite(file->name, name_size, 1, things->fs);

      char null_seperator = '\0';
      fwrite(&null_seperator, 1, 1, things->fs);

      dir_size += name_size + 1;
    }

    EMAP_ENTRY entry = NEW_EMAP_ENTRY_DIR(dir_size);

    EMAP map = {
        .hash = dir->hash,
        .pos = things->pos,
        .entry = entry,
    };

    fwrite(&map, sizeof(map), 1,
           things->ms); // Write Mapping Structure

    things->pos += dir_size; // Shift the Index Position
  }
}

typedef enum {
  architecture_elf64_x86_64 = 0,
} architecture;

typedef struct {
  architecture arch;
  const char *output;
  const char *input;
  const char *tmp_dir;
} Settings;

static void iterdir_start(const Settings *const settings,
                          GlobalThings *things) {

  if (is_directory(settings->input)) {
    cembed(settings->input, settings->input, things, NULL, true, NULL);
    iterdir(settings->input, settings->input, things);

    process_dirs(things);
    return;
  }

  fprintf(stderr, "Not a directory: %s\n", settings->input);
  exit(2);
}

void system_checked(const char *command) {

  int result = system(command);

  if (result != 0) {
    fprintf(stderr, "system() failed with %d: %s\n", result, command);
    exit(1);
  }
}

static const char *arch_string(architecture arch) {
  switch (arch) {
  case architecture_elf64_x86_64:
    return "elf64-x86-64";
  default:
    return "<unknown>";
  };
}

int main(int argc, char *argv[]) {

  if (argc <= 1) {
    fprintf(stderr, "Invalid amount of arguments: %d\n", argc);
    return 1;
  }

  static Settings settings = (Settings){
      .arch = architecture_elf64_x86_64,
      .output = NULL,
      .input = NULL,
      .tmp_dir = "cembed_tmp",
  };

  GlobalThings things = (GlobalThings){
      .ms = NULL, .fs = NULL, .pos = 0, .hash_vec = {}, .dir_vec = {}};
  hash_vec_init(&(things.hash_vec));
  dir_vec_init(&(things.dir_vec));

  for (size_t i = 1; i < (size_t)argc; i++) {
    const char *const arg = argv[i];
    if (strcmp(arg, "-a") == 0) {
      if ((i + 1) >= (size_t)argc) {
        fprintf(stderr, "Missing argument after %s\n", arg);
        return 1;
      }
      const char *const next_arg = argv[i + 1];
      ++i;

      if (strcmp(next_arg, "elf64-x86-64") == 0) {
        settings.arch = architecture_elf64_x86_64;
      } else {
        fprintf(stderr, "Invalid architecture %s\n", next_arg);
        return 1;
      }
    } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
      if ((i + 1) >= (size_t)argc) {
        fprintf(stderr, "Missing argument after %s\n", arg);
        return 1;
      }
      const char *const next_arg = argv[i + 1];
      ++i;

      settings.output = next_arg;
    } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--temp") == 0) {
      if ((i + 1) >= (size_t)argc) {
        fprintf(stderr, "Missing argument after %s\n", arg);
        return 1;
      }
      const char *const next_arg = argv[i + 1];
      ++i;

      settings.tmp_dir = next_arg;
    } else {
      if (settings.input == NULL) {
        settings.input = arg;
      } else {
        fprintf(stderr, "Too much arguments %s\n", arg);
        return 1;
      }
    }
  }

  if (settings.input == NULL) {
    fprintf(stderr, "Missing input\n");
    return 1;
  }
  size_t input_len = strlen(settings.input);

  if (input_len == 0) {
    fprintf(stderr, "Invalid input: %s\n", settings.input);
    return 1;
  }

  if (settings.input[input_len - 1] == '/') {
    fprintf(stderr, "Invalid input, trailing /: %s\n", settings.input);
    return 1;
  }

  if (settings.output == NULL) {
    fprintf(stderr, "Missing output\n");
    return 1;
  }

  char fmt[CEMBED_MAXPATH] = {};

  sprintf(fmt, "if [ ! -d %s ]; then mkdir %s; fi;", settings.tmp_dir,
          settings.tmp_dir);
  system_checked(fmt);

  // Build the Mapping Structure and Virtual File System

  sprintf(fmt, "%s/cembed.map", settings.tmp_dir);
  things.ms = fopen(fmt, "wb");
  sprintf(fmt, "%s/cembed.fs", settings.tmp_dir);
  things.fs = fopen(fmt, "wb");

  if (things.ms == NULL || things.fs == NULL) {
    fprintf(stderr,
            "Failed to initialize map and filesystem. Check permissions.\n");
    return 1;
  }

  iterdir_start(&settings, &things);

  fflush(things.ms);
  fclose(things.ms);

  fflush(things.fs);
  fclose(things.fs);
  hash_vec_destroy(&(things.hash_vec));
  dir_vec_destroy(&(things.dir_vec));

  // Convert to Embeddable Symbols

  sprintf(fmt,
          "cd %s && objcopy -I binary -O %s "
          "--redefine-sym _binary___cembed_map_start=cembed_map_start "
          "--redefine-sym _binary___cembed_map_end=cembed_map_end "
          "--redefine-sym _binary___cembed_map_size=cembed_map_size "
          "./cembed.map ./cembed.map.o",
          settings.tmp_dir, arch_string(settings.arch));
  system_checked(fmt);

  sprintf(fmt, "rm %s/cembed.map", settings.tmp_dir);
  system_checked(fmt);

  sprintf(fmt,
          "cd %s && objcopy -I binary -O %s "
          "--redefine-sym _binary___cembed_fs_start=cembed_fs_start "
          "--redefine-sym _binary___cembed_fs_end=cembed_fs_end "
          "--redefine-sym _binary___cembed_fs_size=cembed_fs_size "
          "./cembed.fs ./cembed.fs.o",
          settings.tmp_dir, arch_string(settings.arch));
  system_checked(fmt);

  sprintf(fmt, "rm %s/cembed.fs", settings.tmp_dir);
  system_checked(fmt);

  sprintf(fmt, "ld -relocatable %s/*.o -o %s", settings.tmp_dir,
          settings.output);
  system_checked(fmt);

  sprintf(fmt, "rm -rf %s", settings.tmp_dir);
  system_checked(fmt);

  fprintf(stdout, "Created final object file at: %s\n", settings.output);

  return 0;
}
