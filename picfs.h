/**
 * PICOS, PIC operating system.
 * Author: David Coss, PhD
 * Date: 1 Sept 2011
 * License: GNU Public License version 3.0 (see http://www.gnu.org)
 *
 * This file is the header for the filesystem used on PIC devices.
 */
#ifndef PICFS_H
#define PICFS_H

#include "fs.h"
#include "utils.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "arg.h"
#include "scheduler.h"

// Maximum number of open files.
// This determines the size of the file table in memory (FTAB)
#ifndef MAX_OPEN_FILES
#define MAX_OPEN_FILES 8
#endif

#ifndef MTAB_ENTRY_MAX
#define MTAB_ENTRY_MAX 6
#endif

#define PICFS_FILENAME_MAX (FS_BUFFER_SIZE-2)

typedef char file_handle_t;// file handle type
typedef unsigned int offset_t;
typedef char picos_dev_t;// id of physical device

typedef struct {
  picos_addr_t root_address;// address of first byte of mounted file system
  picos_dev_t device_id;
} mount_t;

// In order from least to most significant byte, the 
// data in SRAM for the file handle is:
// file's root inode, most significant byte of 
// next get offset (inode not byte address), and least significant byte
// of next get offset
typedef struct  {
  char first_inode, mount_point;
  picos_size_t inode_position;// index of inode in the file to be read next
} file_t;
#define FILE_HANDLE_SIZE sizeof(file_t)

enum {PICFS_SET, PICFS_CURR, PICFS_END,PICFS_REVERSE};
enum {DEV_SRAM, DEV_RAW_FILE, DEV_SDCARD, DEV_EEPROM, DEV_STDOUT};
#define ST_SIZE 0 // size is 2 bytes, big endian

volatile FS_Unit picfs_buffer[FS_BUFFER_SIZE];

signed char picfs_mount(unsigned int fs_addr, picos_dev_t dev);
signed char picfs_open(const char *name,char mount_point);
signed char picfs_close(file_handle_t fh);

/**
 * Dumps the buffer to the file
 *
 * NB: file handle currently ignore and the raw file of the fs is used.
 */
signed char picfs_dump(file_handle_t fh);

signed char picfs_load(file_handle_t fh);// Load a block of data from a file.

signed char picfs_stat(file_handle_t fh);
char picfs_is_open(file_handle_t fh); 
/**
 * Reads a file specified by "filename" and, depending on the
 * value of fileptr, writes it to:
 *
 * UPDATE
 *
 */
void cat_file(const char *filename, offset_t fileptr,char mount_point, picos_dev_t output_device);

// SRAM block assignments
// These are the addresses (unsigned int) for temporary storage.
#define SRAM_MTAB_ADDR 0 // First byte is number of devices mounted, After that is the mount table
#define SRAM_PICFS_WRITE_SWAP_ADDR (SRAM_MTAB_ADDR + MTAB_ENTRY_MAX*sizeof(mount_t))
#define SRAM_PICFS_OPEN_SWAP_ADDR (SRAM_PICFS_WRITE_SWAP_ADDR + FS_BUFFER_SIZE)// needs one FS_BUFFER_SIZE
#define SRAM_PICFS_ARG_SWAP_ADDR (SRAM_PICFS_OPEN_SWAP_ADDR + FS_BUFFER_SIZE) // needs PICOS_MAX_PROCESSES * ARG_SIZE
#define SRAM_PICFS_FTAB_ADDR (SRAM_PICFS_ARG_SWAP_ADDR + PICOS_MAX_PROCESSES * ARG_SIZE)
#define SRAM_PICFS_FILE_ADDR (SRAM_PICFS_FTAB_ADDR + (MAX_OPEN_FILES*sizeof(file_t)))// Beginning of location of files that are "cat"-ed to SRAM

#endif
