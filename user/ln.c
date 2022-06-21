#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <stddef.h>

int
main(int argc, char *argv[])
{
  if(argc != 3 && argc != 4){
    fprintf(2, "Usage: ln [command] old new\n");
    exit(1);
  }

  if(argc == 4){
    char s_command[] = "-s";

    if(strcmp(argv[1], s_command) == 0){
      if(symlink(argv[2], argv[3]) < 0)
        fprintf(2, "symlink %s %s: failed\n", argv[2], argv[3]);
      exit(0);
    }
    else {
      fprintf(2, "commad %s: not found\n", argv[1]);
      exit(0);
    }
  }

  if(link(argv[1], argv[2]) < 0)
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}
