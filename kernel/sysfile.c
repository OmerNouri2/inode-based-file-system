//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

#define MAX_DEREFERENCE 31

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){ 
    dp->nlink++; 
    iupdate(dp);
    
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}
// version 4.3



uint64
sys_open(void)
{
  char path[MAXPATH];
  int max_d = MAX_DEREFERENCE;
  int fd, omode;
  struct inode *ip;
  int n;
  struct file *f;
  
  
  

  //printf("enter open!\n");
  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_SYMLINK && omode!= O_NOFOLLOW){
        if ((ip = deref_link(ip, &max_d)) == 0)
        {
        end_op();
        return -1;
        }
    }
    if(ip->type == T_DIR && omode != O_RDONLY && omode!= O_NOFOLLOW){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}


uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  int max_d = MAX_DEREFERENCE;
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){  
    end_op();
    return -1;
  }
  ilock(ip);


    int count = 0;
    while (ip->type == T_SYMLINK && count < max_d) {
      int len = 0;
      readi(ip, 0, (uint64)&len, 0, sizeof(int));

      if(len > MAXPATH)
        panic("open: corrupted symlink inode");

      readi(ip, 0, (uint64)path, sizeof(int), len + 1);
      iunlockput(ip);
      if((ip = namei(path)) == 0){
        end_op();
        return -1;
      }
      ilock(ip);
      count++;
    }
    iunlock(ip);
   
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}


uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int index;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(index=0;; index++){
    if(index >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*index, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[index] = 0;
      break;
    }
    argv[index] = kalloc();
    if(argv[index] == 0)
      goto bad;
    if(fetchstr(uarg, argv[index], PGSIZE) < 0)
      goto bad;
  }
  
  int ret = exec(path, argv);

  for(index = 0; index < NELEM(argv) && argv[index] != 0; index++)
    kfree(argv[index]);

  return ret;

 bad:
  for(index = 0; index < NELEM(argv) && argv[index] != 0; index++)
    kfree(argv[index]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; 
  struct file *rf, *wf;
  int fd_first, fd_sec;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd_first = -1;
  if((fd_first = fdalloc(rf)) < 0 || (fd_sec = fdalloc(wf)) < 0){
    if(fd_first >= 0)
      p->ofile[fd_first] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd_first, sizeof(fd_first)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd_first), (char *)&fd_sec, sizeof(fd_sec)) < 0){
    p->ofile[fd_first] = 0;
    p->ofile[fd_sec] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}


uint64
sys_symlink(void)
{
  char path[MAXPATH];
  char buf[MAXPATH];
  memset(buf, 0, sizeof(buf));
  
  if(argstr(0, buf, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0){
    return -1;
  }
  
  struct inode *ip;

  begin_op();
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0){
    end_op();
    return -1;
  }

  if(writei(ip, 0, (uint64)buf, 0, MAXPATH) != MAXPATH){
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}



int readlink(const char *pathname, char *buf, int bufsize)
{
  struct inode *ip; 
                  
  
  if ((ip = namei((char*)pathname)) == 0)
    return -1;
  ilock(ip);

  if (!ip->symlink)
  {
    //printf("111111111111\n");
    iunlock(ip);
    return -1;
  }

  if (ip->symlink)
  {
    strncpy(buf, (char*)ip->addrs, bufsize+1);

    struct proc *p = myproc();
    if(copyout(p->pagetable, (uint64)ip->addrs, buf, bufsize) < 0){      
     // printf("dddddddddddd : %s\n",buf);
      iunlock(ip);
      return -1;
    }
    
    iunlock(ip);
 
    return 0;
  }
  iunlock(ip);
  return -1;
}


uint64
sys_readlink(void)
{
  char pathname[MAXPATH];
  uint64 buf;
  int bufsize;

  if(argstr(0,pathname,MAXPATH) < 0 || argaddr(1,&buf) < 0 || argint(2,&bufsize) < 0){
    return -1;
  }
  struct inode *ip;
  begin_op();
  if ((ip = namei(pathname)) == 0){ 
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_SYMLINK || ip->size > bufsize){
    iunlock(ip);
    end_op();
    return -1;
  }

  char buffer[bufsize];
  int ret = readi(ip, 0, (uint64)buffer, 0, bufsize);
  struct proc *p = myproc();

  if (copyout(p->pagetable, buf, buffer, bufsize) < 0){

    iunlock(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  //printf("Reached");
  end_op();
  return ret;
}