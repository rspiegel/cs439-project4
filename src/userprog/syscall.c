#include "userprog/syscall.h"

static void syscall_handler (struct intr_frame *);

static file_pointer file_array[128];
static struct semaphore mutex;

struct list all_list;

static int readcount;

void
syscall_init (void) 
{  
  //otis driving
  sema_init(&mutex, 1);
  readcount=0;

  int i = 0;
  for (; i<128; i++)
  {
    sema_init(&file_array[i].resource, 1);
    file_array[i].file = NULL;
    file_array[i].name = palloc_get_page (0);
    strlcpy(file_array[i].name,"",1);
    file_array[i].open_flag =0;
  }

  file_array[0].file = NULL;
  file_array[0].name = "STDIN_FILENO";
  file_array[0].open_flag = 1;
  file_array[1].file = NULL;
  file_array[1].name = "STDOUT_FILENO";
  file_array[1].open_flag = 1;

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
    //ryan driving
    int* myesp = (int*)f->esp;
    if(!is_good_ptr(myesp)) {
      exit(-1);
    }

    int code = *myesp++;

    switch(code)
    {
      case SYS_HALT:
        halt();
        break;
      case SYS_EXIT:
      {
        int arg1 = *myesp++;
        f->eax = arg1;
        exit(arg1);
        break;
      }
      case SYS_EXEC:
      {
        char* arg1 = (char*)*myesp++;
        f->eax = exec(arg1);
        break;
      }
      case SYS_WAIT:
      {
        pid_t arg1 = (pid_t)*myesp++;
        f->eax = wait(arg1);
        break;
      }
      case SYS_CREATE:
      {
        char* arg1 = (char*)*myesp++;
        unsigned arg2 = (unsigned)*myesp++;
        f->eax = create(arg1,arg2);
        break;
      }
      case SYS_REMOVE:
      {
        char* arg1 = (char*)*myesp++;
        f->eax = remove(arg1);
        break;
      }
      // gavin driving
      case SYS_OPEN:
      {
        char* arg1 = (char*)*myesp++;
        f->eax = open(arg1);
        break;
      }
      case SYS_FILESIZE:
      {
        int arg1 = *myesp++;
        f->eax = filesize(arg1);
        break;
      }
      case SYS_READ:
      {
        int arg1 = *myesp++;
        void* arg2 = (void*)*myesp++;
        unsigned arg3 = (unsigned)*myesp++;
        f->eax = read(arg1,arg2,arg3);
        break;
      }
      case SYS_WRITE:
      {
        int arg1 = *myesp++;
        void* arg2 = (void*)*myesp++;
        unsigned arg3 = (unsigned)*myesp++;
        f->eax = write(arg1,arg2,arg3);
        break;
      }
      // billy driving
      case SYS_SEEK:
      {
        int arg1 = *myesp++;
        unsigned arg2 = (unsigned)*myesp++;
        seek(arg1,arg2);
        break;
      }
      case SYS_TELL:
      {
        int arg1 = *myesp++;
        f->eax = tell(arg1);
        break;
      }
      case SYS_CLOSE:
      {
        int arg1 = *myesp++;
        close(arg1);
        break;
      }
      default:
        exit(-1);
    }
}

void halt(void)
{
  shutdown_power_off();
}

//ryan driving
void exit(int status)
{
  if(status<0)
    status = -1;
  struct thread *current = thread_current();
  current->exit_status = status;
  printf("%s: exit(%d)\n", current->name, current->exit_status);
  thread_exit();
}

pid_t exec(const char* cmd_line)
{
  if(!is_good_ptr(cmd_line))
  {
    sema_up(&mutex);
    exit(-1);
  }
  
  pid_t pid = process_execute(cmd_line);

  sema_down(&thread_current()->exec_sema);
  if(!thread_current()->child_load_success)
  {
    return -1;
  }

  return pid;
}

//gavin driving
int wait(pid_t pid)
{ 
  int status = (int)process_wait(pid);

  struct thread *child = list_contains_pid(&all_list, pid, 2);

  return status;
}

bool create(const char* file, unsigned initial_size)
{
  sema_down(&mutex);
  if(!is_good_ptr(file))
  {
    sema_up(&mutex);
    exit(-1);
    return 0;
  }
  bool result = filesys_create(file, initial_size);

  sema_up(&mutex);

  return result;
}

//otis driving
bool remove(const char* file)
{
  sema_down(&mutex);
  if(!is_good_ptr(file))
  {
    sema_up(&mutex);
    return 0;
  }

  bool result = filesys_remove(file);

  sema_up(&mutex);

  return result;
}

//billy driving
int open(const char* file)
{
  sema_down(&mutex);
  if(!is_good_ptr(file))
  {
    sema_up(&mutex);
    exit(-1);
    return -1;
  }

  int i = 2;
  for (; i<128; i++) 
  {
    if (!file_array[i].file)
    {
      if ((file_array[i].file = filesys_open(file)) == NULL)
      {
        sema_up(&mutex);
        return -1;
      }
      else 
      {
        strlcpy(file_array[i].name, file, strlen(file));
        file_array[i].open_flag=1;
        file_array[i].owner = thread_current()->tid;
        sema_up(&mutex);
        return i;
      }
    }
  }

  sema_up(&mutex);

  return -1;
}

void close(int fd)
{
  sema_down(&mutex);

  if (fd < 2 || fd > 128 || file_array[fd].open_flag==0)
  {
   sema_up(&mutex);
    exit(-1);
  }
  if(thread_current()->tid != file_array[fd].owner){
    sema_up(&mutex);
    return;
  }
  file_deny_write(file_array[fd].file);

  file_close(file_array[fd].file);
  file_array[fd].open_flag=0;
  strlcpy(file_array[fd].name,"",1);
  file_array[fd].file = NULL;
  
  sema_up(&mutex);
}

//ryan driving 
int filesize(int fd)
{
  sema_down(&mutex);

  if (fd < 0 || fd > 128 || file_array[fd].open_flag==0)
  {
   sema_up(&mutex);
    return -1;
  }

  int result = -1;
  if (strcmp(file_array[fd].name,"")) 
  {
    result = (int)file_length(file_array[fd].file);
  }

  sema_up(&mutex);

  return result;
}

//gavin driving
int read(int fd, void* buffer, unsigned size)
{  
  sema_down(&mutex);
  if (fd == 1 || fd < 0 || fd > 128 || file_array[fd].open_flag==0)
  {
    sema_up(&mutex);
    return -1;
  }
  readcount++;
  if(readcount==1)
    sema_down(&file_array[fd].resource);
  sema_up(&mutex);

  int result = -1;
  if(!is_good_ptr(buffer))
  {
    sema_down(&mutex);
    readcount--;
    if(readcount==0)
      sema_up(&file_array[fd].resource);
    sema_up(&mutex);
    exit(-1);
    return result;
  }
  if (strcmp(file_array[fd].name,"")) 
  {
    result = file_read(file_array[fd].file, buffer, size);
  }

  // billy driving
  sema_down(&mutex);
  readcount--;
  if(readcount==0)
    sema_up(&file_array[fd].resource);
  sema_up(&mutex);

  return result;
}

int write(int fd, const void* buffer, unsigned size)
{
  sema_down(&mutex);
  if (fd < 1 || fd > 128 || file_array[fd].open_flag==0)
  {
    sema_up(&mutex);
    exit(-1);
    return -1;
  }
  sema_down(&file_array[fd].resource);
  int result = -1;
  if(!is_good_ptr(buffer))
  {
    sema_up(&file_array[fd].resource);
    sema_up(&mutex);
    exit(-1);
    return result;
  }
  if (fd == 1) 
  {
    if (size <= 300)
    {
      putbuf(buffer, (size_t)size);
      result = (int)size;
    }
    else
    {
      result = 0; 
    }
  }
  else if (strcmp(file_array[fd].name,"")) 
  {
    result = file_write(file_array[fd].file, buffer, size);
  }
  sema_up(&file_array[fd].resource);
  sema_up(&mutex);
	return result;
}

//otis driving
void seek(int fd, unsigned position)
{
  sema_down(&mutex);

  if (fd < 0 || fd > 128 || file_array[fd].open_flag==0)
  {
    sema_up(&mutex);
    return;
  }

  if (strcmp(file_array[fd].name,"")) 
  {
  	file_seek(file_array[fd].file, position);
  }

  sema_up(&mutex);
}

//gavin driving
unsigned tell(int fd)
{
	sema_down(&mutex);

  if (fd < 0 || fd > 128 || file_array[fd].open_flag==0)
  {
    sema_up(&mutex);
    return -1;
  }

  unsigned result = NULL;
  if (strcmp(file_array[fd].name,"")) 
  {
   	result = file_tell(file_array[fd].file);
  }
 	sema_up(&mutex);

 	return result;
}

//billy driving
bool is_good_ptr(void* ptr)
{
  if(is_kernel_vaddr((char *)ptr) || ptr==NULL)
  {
    // exit(-1);
    return false;
  }
  else if(pagedir_get_page(thread_current()->pagedir, ptr) == NULL)
  {
    // exit(-1);
    return false;
  }
  return true;
}
