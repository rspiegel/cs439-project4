#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = false;
  char* file = filesys_get_file(name);
  if(!strcmp(file, ".") && !strcmp(file, ".."))
  { 
    success = (dir != NULL
                && free_map_allocate (1, &inode_sector)
                && inode_create (inode_sector, initial_size, false)
                && dir_add (dir, name, inode_sector));
  }
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(file);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

char* filesys_get_file(const char* file_path)
{
  char file_path_copy[strlen(file_path) + 1];
  strlcpy(file_path_copy, file_path, strlen(file_path) + 1);

  char* ptr;
  char* token = strtok_r(file_path_copy, "/", &ptr);
  char* last_token = "";
  while(token != NULL)
  {
    last_token = token;
    token = strtok_r(NULL, "/", &ptr);
  }

  char* file[strlen(last_token) + 1];
  strlcpy(file, last_token, strlen(last_token) + 1);
  return file;
}

bool filesys_change_dir(const char* dir_name)
{
  struct dir* dir = filesys_get_dir(dir_name);
  char* file_name = filesys_get_file(dir_name);
  struct inode* inode = NULL;

  if(dir != NULL)
  {
    if(strcmp(file_name, "..") == 0)
    {
      if(!dir_super(&inode, dir))
      {
        free(file_name);
        return false;
      }
    }
    else if((dir_root(dir) && strlen(file_name) == 0) || strcmp(file_name, ".") == 0)
    {
      thread_current()->current_dir = dir;
      free(file_name);
      return true;
    }
    else
    {
      dir_lookup(dir, file_name, &inode);
    }
  }
  dir_close(dir);
  free(file_name);
  dir = dir_open(inode);
  if(dir)
  {
    dir_close(thread_current()->current_dir);
    thread_current()->current_dir = dir;
    return true;
  }
  return false;
}

char* filesys_get_dir(const char* file_path)
{
  char str[strlen(file_path) + 1];
  memcpy(str, file_path, strlen(file_path) + 1);

  char* ptr;
  char* next = NULL;
  char* token = strtok_r(str, "/", &ptr);

  struct dir* dir;
  if(str[0] == '/' && !thread_current()->current_dir)
  {
    dir = dir_open_root();
  }
  else
  {
    dir = dir_reopen(thread_current()->current_dir);
  }

  if(token)
  {
    next = strtok_r(NULL, "/", &ptr);
  }
  while(next != NULL)
  {
    if(strcmp(token, ".") != 0)
    {
      struct inode* inode;
      if(strcmp(token, "..") == 0)
      {
        if(!dir_super(&inode, dir))
        {
          return NULL;
        }
      }
      else
      {
        if(!dir_lookup(dir, token, &inode))
        {
          return NULL;
        }
      }
      if(inode_dir(&inode))
      {
        dir_close(dir);
        dir = dir_open(inode);
      }
      else
      {
        inode_close(inode);
      }
    }
    token = next;
    next = strtok_r(NULL, "/", &ptr);
  }
  return dir;
}