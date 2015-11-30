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
#define INDIRECT_POINTERS 128

#define N_NUMBLOCKS 1024


struct block
{
  block_sector_t start;               /* First data sector. */
  off_t length;                       /* File size in bytes. */
};

struct indirect_first_level
{
  block_sector_t ptrs[INDIRECT_POINTERS];
};

struct indirect_second_level
{
  struct indirect_first_level* first_level[N_NUMBLOCKS];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    unsigned magic;
    block_sector_t start;
    off_t length;
    struct bitmap* bm;
    block_sector_t parent;
    bool dir;
    block_sector_t ptrs[INODE_POINTERS];
    uint32_t unsed_btyes[105];
    uint32_t direct_index;
    uint32_t first_indirect_index;
    uint32_t second_indirect_index;
    // struct indirect_first_level* first_level;
    // struct indirect_second_level* second_level;

  };

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
    off_t length;                       // length of file
    off_t read;                         // amount read
    unsigned direct;                    // index to first direct block
    unsigned first_indirect;            // index to first indirect
    unsigned second_indirect;           // index to second indirect
    block_sector_t ptrs[INODE_POINTERS]; // pointers to data blocks
  };
  
bool inode_new(struct inode_disk* inode_d);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t length, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < length){
    uint32_t index;
    uint32_t indirect_ptrs[INDIRECT_POINTERS];
    if(pos < BLOCK_SECTOR_SIZE * DIRECT_BLOCKS)
    { 
      return inode->ptrs[pos / BLOCK_SECTOR_SIZE];
    }
    else if (pos < BLOCK_SECTOR_SIZE *(DIRECT_BLOCKS + 
                  FIRST_INDIRECT_BLOCKS * INDIRECT_POINTERS))
    {
      pos = pos - BLOCK_SECTOR_SIZE * DIRECT_BLOCKS;
      index = pos / (BLOCK_SECTOR_SIZE * INDIRECT_POINTERS) + DIRECT_BLOCKS;
      block_read(fs_device, indirect_ptrs[index], &indirect_ptrs);
      pos = pos % (BLOCK_SECTOR_SIZE * INDIRECT_POINTERS);
      return indirect_ptrs[pos / BLOCK_SECTOR_SIZE];
    }
    else
    {
      block_read(fs_device, inode->ptrs[SECOND_INDIRECT_INDEX], &indirect_ptrs);
      pos = pos - BLOCK_SECTOR_SIZE * (DIRECT_BLOCKS + INDIRECT_POINTERS * FIRST_INDIRECT_BLOCKS);
      index = pos / (BLOCK_SECTOR_SIZE * INDIRECT_POINTERS);
      block_read(fs_device, indirect_ptrs[index], &indirect_ptrs);
      pos = pos % (BLOCK_SECTOR_SIZE * INDIRECT_POINTERS);
      return indirect_ptrs[pos / BLOCK_SECTOR_SIZE];
    }
  }
  else
    return -1;
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
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
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
    disk_inode->dir = dir;
    disk_inode->parent = ROOT_DIR_SECTOR;
    if(inode_new(disk_inode))
    {
      block_write (fs_device, sector, disk_inode);
      success = true; 
    } 
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
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
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  struct inode_disk id;
  block_read (fs_device, inode->sector, &id);
  inode->length = id.length;
  inode->read = id.length;
  inode->direct = id.direct_index;
  inode->first_indirect = id.first_indirect_index;
  inode->second_indirect = id.second_indirect_index;
  inode->parent = id.parent;
  inode->dir = id.dir;
  memcpy(&inode->ptrs, &id.ptrs, INODE_POINTERS*sizeof(block_sector_t));

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

  if(offset+ size > inode_length(inode))
  {
    if(!inode->dir)
    {
      //add lock
    }
    inode->length = inode_build(inode, offset + size);
    if(!inode->dir)
    {
      //add lock
    }
  }
  while (size > 0) 
  {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, inode_length(inode),  offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

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

bool inode_new(struct inode_disk* inode_d)
{
  struct inode new_inode;
  
  inode_d->direct_index = 0;
  inode_d->first_indirect_index = 0;
  inode_d->second_indirect_index = 0;
  memcpy(&inode_d->ptrs, &new_inode.ptrs, INODE_POINTERS * sizeof(block_sector_t));
  return true;
}

off_t inode_build(struct inode* inode, off_t length)
{
  char zeros[BLOCK_SECTOR_SIZE];
  unsigned sectors = bytes_to_sectors(length) - bytes_to_sectors(inode->length);
  if(sectors == 0)
  {
    return length;
  }
  while(inode->direct < FIRST_INDIRECT_INDEX)
  {
    free_map_allocate(1, &inode->ptrs[inode->direct]);
    block_write(fs_device, inode->ptrs[inode->direct], zeros);
    inode->direct += 1;
    sectors -= 1;
    if(sectors == 0)
    {
      return length;
    }
  }
  while(inode->direct < SECOND_INDIRECT_INDEX)
  {
    sectors = inode_build_indirect(inode, sectors);
    if(sectors == 0)
    {
      return length;
    }
  }
  if(inode->direct == SECOND_INDIRECT_INDEX)
  {
    sectors = inode_build_second_indirect(inode, sectors);
  }
  return length - sectors * BLOCK_SECTOR_SIZE;  
}

off_t inode_build_indirect(struct inode* inode, unsigned sectors)
{
  char zeros[BLOCK_SECTOR_SIZE];
  struct indirect_first_level fl_blocks;
  if(inode->first_indirect == 0)
  {
    free_map_allocate(1, &inode->ptrs[inode->direct]);
  }
  else
  {
    block_read(fs_device, inode->ptrs[inode->direct], zeros);
  }
  while(inode->first_indirect < INDIRECT_POINTERS)
  {
    free_map_allocate(1, &fl_blocks.ptrs[inode->first_indirect]);
    block_write(fs_device, fl_blocks.ptrs[inode->first_indirect], zeros);
    inode->first_indirect += 1;
    sectors -= 1;
    if(sectors == 0)
    {
      break;
    }
  }
  block_write(fs_device, inode->ptrs[inode->direct], &fl_blocks);
  if(inode->first_indirect == INDIRECT_POINTERS)
  {
    inode->first_indirect = 0;
    inode->direct += 1;
  }
  return sectors;
}

off_t inode_build_second_indirect(struct inode* inode, unsigned sectors)
{
  struct indirect_first_level sl_blocks;
  if(inode->second_indirect == 0 && inode->first_indirect == 0)
  {
    free_map_allocate(1, &inode->ptrs[inode->direct]);
  }
  else
  {
    block_read(fs_device, inode->ptrs[inode->direct], &sl_blocks);
  }
  while(inode->first_indirect < INDIRECT_POINTERS)
  {
    static char zeros[BLOCK_SECTOR_SIZE];
    struct  indirect_first_level fl_blocks;
    if(inode->second_indirect == 0)
    {
      free_map_allocate(1, &sl_blocks.ptrs[inode->first_indirect]);
    }
    else
    {
      block_read(fs_device, sl_blocks.ptrs[inode->first_indirect], &fl_blocks);
    }
    while(inode->second_indirect < INDIRECT_POINTERS)
    {
      free_map_allocate(1, &sl_blocks.ptrs[inode->second_indirect]);
      block_write(fs_device, sl_blocks.ptrs[inode->second_indirect], zeros);
      inode->second_indirect += 1;
      sectors -= 1;
      if(sectors == 0)
      {
        break;
      }
    }
    block_write(fs_device, sl_blocks.ptrs[inode->first_indirect], &sl_blocks);
    if(inode->second_indirect == INDIRECT_POINTERS)
    {
      inode->second_indirect = 0;
      inode->first_indirect += 1;
    }
    if(sectors == 0)
    {
      break;
    }
  }
  block_write(fs_device, inode->ptrs[inode->direct], &sl_blocks);
  return sectors;
}

bool inode_dir(const struct inode* inode)
{
  return inode->dir;
}
