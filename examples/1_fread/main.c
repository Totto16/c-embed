#define CEMBED_IMPLEMENTATION
#include <c-embed.h>

void write_impl(void *buf, size_t size);

int main(void) {

  EFILE *eFile = eopen("/data2/data2.txt", "r");

  if (eFile == NULL) {
    eerror("Error opening file");
    return 1;
  }

  int pos = eseek(eFile, 0, SEEK_END);
  if (pos < 0) {
    eerror("Error seeking file");
    return 1;
  }

  long int size = etell(eFile);
  if (size < 0) {
    eerror("Error getting file pos");
    return 1;
  }

  char *buf = (char *)malloc(size);
  if (buf == NULL) {
    fprintf(stderr, "Error: OOM\n");
    return 1;
  }

  pos = eseek(eFile, 0, SEEK_SET);
  if (pos < 0) {
    eerror("Error unseeking file");
    return 1;
  }

  size_t count = eread(buf, 1, size, eFile);

  if (count != (size_t)size) {
    fprintf(stderr, "Error: read returned: %lu\n", count);
    return 1;
  }

  write_impl(buf, size);

  free(buf);

  eclose(eFile);
}

#include <unistd.h>

void write_impl(void *buf, size_t size) { write(STDOUT_FILENO, buf, size); }
