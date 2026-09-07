
#define CEMBED_IMPLEMENTATION
#define CEMBED_TRANSLATE
#include <c-embed.h>

int main(void) {

  FILE *eFile = fopen("/data2/data2.txt", "r");

  char buffer[100] = {' '};

  if (eFile == NULL) {
    perror("Error opening file");
    return 1;
  }

  while (!feof(eFile)) {
    if (fgets(buffer, 100, eFile) == NULL)
      break;
    fputs(buffer, stdout);
  }

  fclose(eFile);
}
