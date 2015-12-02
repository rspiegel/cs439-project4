#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

//max file size 8MB
#define MAX_FILE 8388608

#define DIRECT_BLOCKS 10
#define FIRST_INDIRECT_BLOCKS 1
#define SECOND_INDIRECT_BLOCKS 1

#define DIRECT_INDEX 0
#define FIRST_INDIRECT_INDEX 10
#define SECOND_INDIRECT_INDEX 11

#define INODE_POINTERS 14

#define N_NUMBLOCKS 128

/*
struct block
{
  block_sector_t start;               
  off_t length;                       
};

struct indirect_first_level
{
  block_sector_t ptrs[N_NUMBLOCKS];
};

struct indirect_second_level
{
  struct indirect_first_level first_level[N_NUMBLOCKS];
};
*/


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  unsigned magic;
  block_sector_t start;
  off_t length;
  block_sector_t ptrs[DIRECT_BLOCKS];
  block_sector_t first_level_sector;  //pointer to indirect_first_level
  block_sector_t second_level_sector; //pointer tp indirect_second_level
  uint32_t unsed_btyes[113];
};

struct inode *inode_new(struct inode_disk *);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
{
  struct list_elem elem;              /* Element in inode list. */
  block_sector_t sector;              /* Sector number of disk location. */
  int open_cnt;                       /* Number of openers. */
  bool removed;                       /* True if deleted, false otherwise. */
  int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  struct inode_disk data;             /* Inode content. */
  block_sector_t parent;
  bool dir;
  off_t read;                         // amount read
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t length, off_t pos)
{
  printf("in byte_to_sector\n");

  ASSERT (inode != NULL);
  char buf[BLOCK_SECTOR_SIZE];
  // block_read(fs_device, inode->data.ptrs[i], buf);

  printf("\nlength: %d\npos: %d\n", length, pos);
  if (pos < length && length < MAX_FILE)
  {
    // ASSERT(0);
    uint32_t index, index_sec1, index_sec;
    uint32_t indirect_ptrs[N_NUMBLOCKS];
    if(pos < BLOCK_SECTOR_SIZE * DIRECT_BLOCKS)
    { 
      return inode->data.ptrs[pos / BLOCK_SECTOR_SIZE];
    }
    else if (pos < BLOCK_SECTOR_SIZE * (DIRECT_BLOCKS + 
                  (FIRST_INDIRECT_BLOCKS * N_NUMBLOCKS)))
    {
      pos = pos - (BLOCK_SECTOR_SIZE * DIRECT_BLOCKS); // get position in first_level
      index = pos / BLOCK_SECTOR_SIZE; // index is position / block_size

      block_sector_t buffer[BLOCK_SECTOR_SIZE/sizeof (block_sector_t)];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      block_read(fs_device,inode->data.first_level_sector,buffer);

      printf("in indirect level 1 byte_to_sector \n\
        pos: %d\nindex: %d\nbuffer[index]: %d\n\n\n", pos, index, buffer[index]);

      return buffer[index];
    }
    else
    {
      pos = pos - (BLOCK_SECTOR_SIZE * (DIRECT_BLOCKS + (N_NUMBLOCKS * FIRST_INDIRECT_BLOCKS)));
      index = pos / BLOCK_SECTOR_SIZE;
      index_sec1 = index / N_NUMBLOCKS; // which first_level to index into
      index_sec = index % N_NUMBLOCKS;  // what index in that first_level array

      block_sector_t second_buffer[BLOCK_SECTOR_SIZE/sizeof (block_sector_t)];
      memset(second_buffer,0,BLOCK_SECTOR_SIZE);
      block_read(fs_device,inode->data.second_level_sector,second_buffer);

      block_sector_t buffer[BLOCK_SECTOR_SIZE/sizeof (block_sector_t)];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      block_read(fs_device,second_buffer[index_sec1],buffer);

      printf("in indirect level 2 byte_to_sector \n\
        pos: %d\nindex: %d\nindex_sec1: %d\nindex_sec: %d\n\
        second_buffer[index_sec1]: %d\n\
        buffer[index_sec]: %d\n\n", pos, index, index_sec1, index_sec, second_buffer[index_sec1], buffer[index_sec]);

      return buffer[index_sec];
    }
  }
  else
  {
    // ASSERT(0);
    printf("error in byte_to_sector");
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  printf("%d\n", sizeof(struct inode_disk));
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    if(length > MAX_FILE)
    {
      disk_inode->length = MAX_FILE;
    }
    else
    {
      disk_inode->length = length;
    }
    disk_inode->magic = INODE_MAGIC;

    struct inode *i_new = inode_new(disk_inode);

    i_new->dir = dir;
    i_new->parent = ROOT_DIR_SECTOR;

    printf("inode_create just made the inode... \nlength: %d\n", inode_length(i_new));
    if (i_new != NULL)
    {
      printf("inode_create is about to write i_new to sector... \nsector: %d\n", sector);
      block_write (fs_device, sector, i_new);
      success = true;    
      //free disk_inode at inode_close if removed == true
    }
    else
    {
      free(disk_inode);
      success = false;
    }

    printf("inode_create is about to return... \nlength: %d\n", inode_length(i_new));
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  /*
  Gavin 5:01 11/30/15 --- 
    In the function description as well as the header, 
    it is suggested that SECTOR contains inodes, rather 
    than disc_inodes. If this is the case, it would lead 
    me to believe that the block_write in inode_create 
    would need to write inode_new rather than inode.
  */
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  // inode = malloc (sizeof *inode);
  // if (inode == NULL)
  //   return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  /*
  // this will just be segfaults
  struct inode_disk id;
  block_read (fs_device, inode->sector, &id);
  inode->length = id.length;
  inode->read = id.length;
  inode->parent = id.parent;
  inode->dir = id.dir;
  */

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }
      free (&inode->data);
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, inode_length(inode), offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
  {
    // printf("size: %d\n",size);
    /* Sector to write, starting byte offset within sector. */
    printf("\nin inode_write_at...\n");
    printf("inode->data.length: %d\n", inode_length(inode));
    //block_sector_t sector_idx = byte_to_sector (inode, inode_length(inode),  offset);
    block_sector_t sector_idx = byte_to_sector (inode, size,  offset);
    printf("sector_idx in write_at: \t%d\ninode_length: \t\t\t%d\n\n\n", sector_idx, inode_length(inode));
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;
    // printf("%d\n%d\n%d\n",inode_left,sector_left,min_left);
    // printf("%d\n",offset);

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        /* Write full sector directly to disk. */
        block_write (fs_device, sector_idx, buffer + bytes_written);
      }
    else 
      {
        /* We need a bounce buffer. */
        if (bounce == NULL) 
        {
          bounce = malloc (BLOCK_SECTOR_SIZE);
          if (bounce == NULL)
            break;
        }

         // If the sector contains data before or after the chunk
         //   we're writing, then we need to read in the sector
         //   first.  Otherwise we start with a sector of all zeros. 
        if (sector_ofs > 0 || chunk_size < sector_left) 
          block_read (fs_device, sector_idx, bounce);
        else
          memset (bounce, 0, BLOCK_SECTOR_SIZE);
        memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
        block_write (fs_device, sector_idx, bounce);
      }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  inode->read = inode_length(inode);
  free(bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

block_sector_t inode_parent(struct inode* inode)
{
  return inode->parent;
}

struct inode *
inode_new(struct inode_disk *inode_d)
{
  struct inode *new_inode;
  new_inode->data = *inode_d;
  new_inode->data.length = inode_build(new_inode,inode_d->length);
  printf("inode_new inode length: %d\n", inode_length(new_inode));
  return new_inode;
}

off_t 
inode_build(struct inode* inode, off_t length)
{
  printf("enter inode_build\n");

  char zeros[BLOCK_SECTOR_SIZE];
  int i = 0; 
  while (i < BLOCK_SECTOR_SIZE) 
  {
    zeros[i++] = 0;
  }

  int sectors = (int)bytes_to_sectors(length);
  // printf("length: %d\nsectors: %d\n", length, sectors);
  ASSERT(sectors); // sectors should never be zero

  printf("in inode_build \nsectors: %d\nlength: %d\n", sectors, length);

  for (i = 0; i < DIRECT_BLOCKS; i++)
  {
    free_map_allocate(1, &inode->data.ptrs[i]);
    block_write(fs_device, inode->data.ptrs[i], zeros);
    sectors--;
  }

  // printf("length: %d\nsectors: %d\n", length, sectors);
  if (sectors <= 0) 
  {
    printf("exiting inode_build \nsectors: %d\nlength: %d\n", sectors, length);
    return length;
  }
  else // if sectors remaining, move on to first_level
  {
  // ASSERT(0);
    sectors = inode_build_indirect(inode, sectors);
  }
  if (sectors <= 0) 
  {
    printf("exiting inode_build \nsectors: %d\nlength: %d\n", sectors, length);
    return length;
  }
  else // if sectors remaining, move on to second_level
  {
    sectors = inode_build_second_indirect(inode, sectors);
  }
  if (sectors <= 0) 
  {
    printf("exiting inode_build \nsectors: %d\nlength: %d\n", sectors, length);
    return length;
  }
  else
  {
    printf("exiting inode_build \nsectors: %d\nlength: %d\n", sectors, length - sectors * BLOCK_SECTOR_SIZE);
    return length - sectors * BLOCK_SECTOR_SIZE;
  }
}

off_t 
inode_build_indirect(struct inode* inode, unsigned sectors)
{
  printf("enter inode_build_indirect\n");

  char zeros[BLOCK_SECTOR_SIZE] = {0};
  int i = 0; 
  while (i < BLOCK_SECTOR_SIZE)
  {
    zeros[i++] = 0;
  }

  free_map_allocate(1, &inode->data.first_level_sector);
  block_sector_t block_buf[N_NUMBLOCKS];
  //printf("%d\n", inode->data.first_level_sector);

  printf("in indirect level 1 build \n\
        sectors: %d\n", sectors);

  for (i = 0; i < N_NUMBLOCKS; i++)
  {
    block_sector_t temp_block;
    free_map_allocate(1, &temp_block);
    block_write(fs_device, &temp_block, zeros);
    block_buf[i] = temp_block;
    sectors--;
  }
  block_write(fs_device, &inode->data.first_level_sector, block_buf);

  return sectors;
}

off_t 
inode_build_second_indirect(struct inode* inode, unsigned sectors)
{
  printf("enter inode_build_second_indirect\n");

  char zeros[BLOCK_SECTOR_SIZE];
  int i, j;
  i = 0;
  while (i < BLOCK_SECTOR_SIZE) 
  {
    zeros[i++] = 0;
  }

  printf("in indirect level 2 build \n\
        sectors: %d\n", sectors);

  free_map_allocate(1, &inode->data.second_level_sector);
  block_sector_t first_level_buf[N_NUMBLOCKS];
  //printf("%d\n", inode->data.second_level_sector);
  for (i = 0; i < N_NUMBLOCKS; i++)
  {
    free_map_allocate(1, &first_level_buf[i]);
    block_sector_t block_buf[N_NUMBLOCKS];
    for (j = 0; j < N_NUMBLOCKS; j++)
    {
      block_sector_t temp_block;
      free_map_allocate(1, &temp_block);
      block_write(fs_device, &temp_block, zeros);
      block_buf[j] = temp_block;
      sectors--;
    }
    block_write(fs_device, &first_level_buf[i], block_buf);
  }
  block_write(fs_device, &inode->data.second_level_sector, first_level_buf);
  return sectors;
}

bool inode_dir(const struct inode* inode)
{
  return inode->dir;
}
