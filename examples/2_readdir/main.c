#include <c-embed.h>

void add_seperator(char *path) {
  size_t len = strlen(path);

  if (len == 0) {
    strcat(path, "/");
    return;
  }

  if (path[len - 1] == '/') {
    return;
  }

  strcat(path, "/");
}

#define EMAXPATH 512
#define INDENT_WIDTH 2

void iterdir(const char *const dirname, size_t indent) {

  char *fullpath = (char *)malloc(EMAXPATH * sizeof(char));

  EFILE *eFile = eopen(dirname, "d");
  if (eFile == NULL) {
    eerror("Error opening directory");
    exit(1);
  }

  while (true) {

    edirent ent;

    int result = ereaddir(eFile, &ent);

    if (result == EREADDIR_FINISHED) {
      break;
    }

    if (result != EERRCODE_SUCCESS) {
      eerror("Error reading dir");
      exit(1);
    }

    if (ent.type == EMAP_ENTRY_TYPE_FILE) {
      strcpy(fullpath, dirname);
      add_seperator(fullpath);
      strcat(fullpath, ent.name);
      printf("%*sFILE: %s\n", (int)(indent * INDENT_WIDTH), "", ent.name);
    } else if (ent.type == EMAP_ENTRY_TYPE_DIR) {
      strcpy(fullpath, dirname);
      add_seperator(fullpath);
      strcat(fullpath, ent.name);
      printf("%*sFOLDER: %s/\n", (int)(indent * INDENT_WIDTH), "", ent.name);
      iterdir(fullpath, indent + 1);
    } else {
      strcpy(fullpath, dirname);
      add_seperator(fullpath);
      strcat(fullpath, ent.name);
      fprintf(stderr, "Ignored entry of type %d: %s\n", ent.type, fullpath);
    }
  }

  eclose(eFile);
  free(fullpath);
}

void iterdir_start(const char *const dirname) {

  const size_t indent = 0;
  printf("%*sFOLDER: %s\n", (int)(indent * INDENT_WIDTH), "", dirname);
  iterdir(dirname, indent + 1);
}

int main(void) {

  EFILE *eFile = eopen("/", "r");

  if (eFile == NULL) {
    eerror("Error opening directory");
    return 1;
  }

  int type = estreamtype(eFile);

  if (type != EMAP_ENTRY_TYPE_DIR) {
    fprintf(stderr, "Invalid type for the root directory: %d\n", type);
    return 1;
  }

  eclose(eFile);

  iterdir_start("/");
}
