#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void) 
{
    printf("symlink: %d\n", symlink("oldpath" , "newpath"));
    printf("symlink: %d\n", readlink("pathname" ,"buf" , 3 ));

    exit(0);
} 

