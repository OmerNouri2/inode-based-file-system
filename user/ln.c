#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include <stddef.h>

int
main(int argc, char *argv[])
{

  if (argc != 3 && (argc != 4 || (argc == 4 && !strcmp(argv[2], "-s"))))
  {
    fprintf(2, "Usage: ln old new\n");
    fprintf(2, "or\n");
    fprintf(2, "Usage for symbolic link: ln -s old new\n");
    exit(-1);
  }
  if (argc == 3)
  {
    if (link(argv[1], argv[2]) < 0)
      fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  }
  else
  {
    if (symlink(argv[2], argv[3]) < 0)
      fprintf(2, "symlink %s %s: failed\n", argv[2], argv[3]);
  }


  // if(argc != 3){
  //   fprintf(2, "Usage: ln old new\n");
  //   exit(1);
  // }
  // if(link(argv[1], argv[2]) < 0)
  //   fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
   exit(0);
}


