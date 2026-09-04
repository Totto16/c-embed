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

#include "c-embed.h"
#include <dirent.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
  FILE *ms;      // Mapping Structure
  FILE *fs;      // Virtual Filesystem
  u_int32_t pos; // Current Position
} GlobalThings;

static void cembed(const char *const filename, const char *root_dir,
                   GlobalThings *things) {

  FILE *file = fopen(filename, "rb"); // Open the Embed Target File
  if (file == NULL) {
    printf("Failed to open file %s.", filename);
    return;
  }
  u_int32_t filename_hash = hash(filename);

  if (root_dir != NULL) {

    const char *filename_relative = filename;
    const size_t filename_len = strlen(filename);
    const size_t root_len = strlen(root_dir);
    if (filename_len < root_len) {
      fprintf(stderr, "Invalid file root: %s\n", root_dir);
      exit(2);
    }
    for (size_t i = 0; i < root_len; ++i) {
      if (filename[i] == root_dir[i]) {
        filename_relative++;
      }
    }

    filename_hash = hash(filename_relative);
  }

  fseek(file, 0, SEEK_END); // Define Map
  u_int32_t file_size = (u_int32_t)ftell(file);
  EMAP map = {.hash = filename_hash, .pos = things->pos, .size = file_size};
  rewind(file);

  char *buf = malloc(sizeof(char) * (map.size));
  if (buf == NULL) {
    printf("Memory error for file %s.", filename);
    return;
  }

  u_int32_t result = fread(buf, 1, map.size, file);
  if (result != map.size) {
    printf("Read error for file %s.", filename);
    return;
  }

  fwrite(&map, sizeof map, 1, things->ms); // Write Mapping Structure
  fwrite(buf, map.size, 1, things->fs);    // Write Virtual Filesystem

  free(buf);               // Free Buffer
  fclose(file);            // Close the File
  file = NULL;             // Reset the Pointer
  things->pos += map.size; // Shift the Index Position
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
        cembed(fullpath, root_dir, things);
      }

      else if (ent->d_type == CEMBED_DIRENT_DIR) {
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        iterdir(fullpath, root_dir, things);
      }
    }

    closedir(dir);

  }

  else {

    strcpy(fullpath, d);
    cembed(fullpath, root_dir, things);
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

typedef enum {
  architecture_elf64_x86_64 = 0,
} architecture;

typedef struct {
  architecture arch;
  bool relative;
  const char *output;
  const char *input;
} Settings;

static void iterdir_start(const Settings *const settings,
                          GlobalThings *things) {
  if (settings->relative) {

    if (is_directory(settings->input)) {
      iterdir(settings->input, settings->input, things);
      return;
    }

    fprintf(stderr, "Nort a directory, but requested relative mode %s\n",
            settings->input);
    exit(2);
  }

  iterdir(settings->input, NULL, things);
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

#define CEMBED_TMPDIR "cembed_tmp" // Temporary Directory

int main(int argc, char *argv[]) {

  if (argc <= 1) {
    fprintf(stderr, "Invalid amount of arguments: %d\n", argc);
    return 1;
  }

  static Settings settings = (Settings){.arch = architecture_elf64_x86_64,
                                        .relative = false,
                                        .output = NULL,
                                        .input = NULL};

  GlobalThings things = (GlobalThings){.ms = NULL, .fs = NULL, .pos = 0};

  for (size_t i = 1; i < (size_t)argc; i++) {
    const char *const arg = argv[i];
    if (strcmp(arg, "-r") == 0) {
      settings.relative = true;
    } else if (strcmp(arg, "-a") == 0) {
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

  if (settings.output == NULL) {
    fprintf(stderr, "Missing output\n");
    return 1;
  }

  char fmt[CEMBED_MAXPATH] = {};

  sprintf(fmt, "if [ ! -d %s ]; then mkdir %s; fi;", CEMBED_TMPDIR,
          CEMBED_TMPDIR);
  system_checked(fmt);

  // Build the Mapping Structure and Virtual File System

  things.ms = fopen("cembed.map", "wb");
  things.fs = fopen("cembed.fs", "wb");

  if (things.ms == NULL || things.fs == NULL) {
    printf("Failed to initialize map and filesystem. Check permissions.");
    return 0;
  }

  iterdir_start(&settings, &things);

  fclose(things.ms);
  fclose(things.fs);

  // Convert to Embeddable Symbols

  sprintf(fmt,
          "objcopy -I binary -O %s "
          "--redefine-sym _binary_cembed_map_start=cembed_map_start "
          "--redefine-sym _binary_cembed_map_end=cembed_map_end "
          "--redefine-sym _binary_cembed_map_size=cembed_map_size "
          "cembed.map cembed.map.o",
          arch_string(settings.arch));
  system_checked(fmt);

  sprintf(fmt, "mv cembed.map.o %s/cembed.map.o", CEMBED_TMPDIR);
  system_checked(fmt);
  system_checked("rm cembed.map");

  sprintf(fmt,
          "objcopy -I binary -O %s "
          "--redefine-sym _binary_cembed_fs_start=cembed_fs_start "
          "--redefine-sym _binary_cembed_fs_end=cembed_fs_end "
          "--redefine-sym _binary_cembed_fs_size=cembed_fs_size "
          "cembed.fs cembed.fs.o",
          arch_string(settings.arch));
  system_checked(fmt);

  sprintf(fmt, "mv cembed.fs.o %s/cembed.fs.o", CEMBED_TMPDIR);
  system_checked(fmt);
  system_checked("rm cembed.fs");

  sprintf(fmt, "ld -relocatable cembed_tmp/*.o -o %s", settings.output);
  system_checked(fmt);

  sprintf(fmt, "rm -rf %s", CEMBED_TMPDIR);
  system_checked(fmt);

  printf("Created final object file at: %s", settings.output);

  return 0;
}
