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
#include <string.h>

#define CEMBED_FILE "c-embed.o"       // Output File
#define CEMBED_TMPDIR "cembed_tmp"    // Temporary Directory
#define CEMBED_ARCH "elf64-x86-64"    // Target Architecture

FILE* ms = NULL;    // Mapping Structure
FILE* fs = NULL;    // Virtual Filesystem
FILE* file = NULL;  // Embed Target File Pointer
u_int32_t pos = 0;  // Current Position

void cembed(const char* const filename, const char* root_dir){

  file = fopen(filename, "rb");  // Open the Embed Target File
  if(file == NULL){
    printf("Failed to open file %s.", filename);
    return;
  }
  u_int32_t filename_hash = hash(filename);

  if(root_dir != NULL){
    
    const char* filename_relative = filename;
    const size_t filename_len = strlen(filename);
    const size_t root_len = strlen(root_dir);
    if(filename_len < root_len){
      fprintf(stderr, "Invalid file root: %s\n",root_dir);
      exit(2);
    }
    for(size_t i = 0; i < root_len; ++i){
      if(filename[i] == root_dir[i]){
        filename_relative++;
      }
    }

    filename_hash = hash(filename_relative);
  }


  fseek(file, 0, SEEK_END);     // Define Map
  EMAP map = {filename_hash, pos, (u_int32_t)ftell(file)};
  rewind (file);

  char* buf = malloc(sizeof(char)*(map.size));
  if(buf == NULL){
    printf("Memory error for file %s.", filename);
    return;
  }

  u_int32_t result = fread(buf, 1, map.size, file);
  if(result != map.size){
    printf("Read error for file %s.", filename);
    return;
  }

  fwrite(&map, sizeof map, 1, ms);  // Write Mapping Structure
  fwrite(buf, map.size, 1, fs);     // Write Virtual Filesystem

  free(buf);        // Free Buffer
  fclose(file);     // Close the File
  file = NULL;      // Reset the Pointer
  pos += map.size;  // Shift the Index Position

}

#define CEMBED_DIRENT_FILE 8
#define CEMBED_DIRENT_DIR 4
#define CEMBED_MAXPATH 512

void iterdir(const char* const d, const char* root_dir){

  char* fullpath = (char*)malloc(CEMBED_MAXPATH*sizeof(char));

  DIR *dir;
  struct dirent *ent;
  dir = opendir(d);
  
  if (dir != NULL) {

    while ((ent = readdir(dir)) != NULL) {

      if(strcmp(ent->d_name, ".") == 0) continue;
      if(strcmp(ent->d_name, "..") == 0) continue;

      if(ent->d_type == CEMBED_DIRENT_FILE){
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        cembed(fullpath, root_dir);
      }

      else if(ent->d_type == CEMBED_DIRENT_DIR){
        strcpy(fullpath, d);
        strcat(fullpath, "/");
        strcat(fullpath, ent->d_name);
        iterdir(fullpath, root_dir);
      }

    }

    closedir(dir);

  }

  else {

    strcpy(fullpath, d);
    cembed(fullpath, root_dir);

  }

  free(fullpath);

}

static bool is_directory(const char* file){
  DIR *dir = opendir(file);
  if (dir != NULL) {
    closedir(dir);
    return true;
  }

  return false;
}

static void iterdir_start(const char* const d){

  if(is_directory(d)){
    iterdir(d, d);
    return;
  }

  iterdir(d, NULL);


}

void system_checked(const char *command){

  int result = system(command);

  if(result != 0){
    fprintf(stderr, "system() failed with %d: %s\n", result, command);
    exit(1);
  }

}

#define system system_checked

int main(int argc, char* argv[]){

  char fmt[CEMBED_MAXPATH];

  if(argc <= 1){
    fprintf(stderr, "Invalid amount of arguments: %d\n", argc);
    return 1;
  }

  sprintf(fmt, "if [ ! -d %s ]; then mkdir %s; fi;", CEMBED_TMPDIR, CEMBED_TMPDIR);
  system(fmt);

  // Build the Mapping Structure and Virtual File System

  ms = fopen("cembed.map", "wb");
  fs = fopen("cembed.fs", "wb");

  if(ms == NULL || fs == NULL){
    printf("Failed to initialize map and filesystem. Check permissions.");
    return 0;
  }

  for(int i = 1; i < argc; i++)
    iterdir_start(argv[i]);

  fclose(ms);
  fclose(fs);

  // Convert to Embeddable Symbols

  sprintf(fmt, "objcopy -I binary -O %s "\
          "--redefine-sym _binary_cembed_map_start=cembed_map_start "\
          "--redefine-sym _binary_cembed_map_end=cembed_map_end "\
          "--redefine-sym _binary_cembed_map_size=cembed_map_size "\
          "cembed.map cembed.map.o", CEMBED_ARCH);
  system(fmt);

  sprintf(fmt, "mv cembed.map.o %s/cembed.map.o", CEMBED_TMPDIR);
  system(fmt);
  system("rm cembed.map");

  sprintf(fmt, "objcopy -I binary -O %s "\
          "--redefine-sym _binary_cembed_fs_start=cembed_fs_start "\
          "--redefine-sym _binary_cembed_fs_end=cembed_fs_end "\
          "--redefine-sym _binary_cembed_fs_size=cembed_fs_size "\
          "cembed.fs cembed.fs.o", CEMBED_ARCH);
  system(fmt);

  sprintf(fmt, "mv cembed.fs.o %s/cembed.fs.o", CEMBED_TMPDIR);
  system(fmt);
  system("rm cembed.fs");

  sprintf(fmt, "ld -relocatable cembed_tmp/*.o -o %s", CEMBED_FILE);
  system(fmt);

  sprintf(fmt, "rm -rf %s", CEMBED_TMPDIR);
  system(fmt);

  printf("%s", CEMBED_FILE);

  return 0;

}
