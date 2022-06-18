#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}



// uint64
// sys_symlink(void)
// {
//   char target[MAXPATH], path[MAXARG];
//   if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0){
//     return -1;
//   }
//   //printf(“creating a sym link. Target(%s). Path(%s)\n”, target, path);

//   begin_op(ROOTDEV);
//   struct inode *ip = create(path, T_SYMLINK, 0, 0);
//   if(ip == 0){
//     end_op(ROOTDEV);
//     return -1;
//   }

//   int len = strlen(target);
//   writei(ip, 0, (uint64)&len, 0, sizeof(int));
//   writei(ip, 0, (uint64)target, sizeof(int), len + 1);
//   iupdate(ip);
//   iunlockput(ip);

//   end_op(ROOTDEV);
//   return 0;
// }