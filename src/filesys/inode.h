#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t, off_t);
off_t inode_write_at (struct inode *, const void *, off_t, off_t);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);


//New Functions
block_sector_t inode_parent(struct inode *);
off_t inode_build(struct inode*, off_t);
off_t inode_build_indirect(struct inode*, unsigned);
off_t inode_build_second_indirect(struct inode*, unsigned);
bool inode_dir(const struct inode*);
#endif /* filesys/inode.h */
