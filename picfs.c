/**
 * PICOS, PIC operating system.
 * Author: David Coss, PhD
 * Date: 21 Dec 2011
 * License: GNU Public License version 3.0 (see http://www.gnu.org)
 *
 * This file has the code behind the file system.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "picos/picfs.h"
#include "picos/picfs_error.h"
#include "picos/sd.h"
#include "picos/io.h"
#include "picos/sram.h"

#include <string.h>
#include <stdbool.h>

FS_Unit picfs_last_raw_block = 0;
char picfs_fh_bitmap = 0xff;// max 8 files open. defined with MAX_OPEN_FILES

// First four bytes are address within the devices of the beginning of the
// files system.
// Fifth byte is the device ID (see picfs.h for definitions)
#define PICFS_MTAB_BITMAP_DEFAULT 0x3f// six entries in the table
char picfs_mtab_bitmap = PICFS_MTAB_BITMAP_DEFAULT;

static void device_read(unsigned int address, void *buffer, char size, picos_dev_t dev)
{
  switch(dev)
    {
    case DEV_SDCARD:
      {
	SD_read(address,buffer,size);
	break;
      }
    case DEV_EEPROM:
      {
	char idx = 0;
	for(;idx < size;idx++)
	  ((char*)buffer)[idx] = eeprom_read((char)address+idx);
	break;
      }
    case DEV_SRAM:
      {
	SRAM_read(address,buffer,size);
	break;
      }
    default:
      break;
    }
}

static void device_write(void *buffer, unsigned int address, char size, picos_dev_t dev)
{
  switch(dev)
    {
    case DEV_SDCARD:
      SD_write(address,buffer,size);
      break;
    case DEV_EEPROM:
      {
	char eeprom_addr = (address & 0xff), idx = 0;
	for(;idx < size;idx++)
	  eeprom_write((char)address+idx,((char*)buffer)[idx]);
	break;
      }
    case DEV_SRAM:
      SRAM_write(address,buffer,size);
      break;
    default:
      break;
    }
}


void cat_file(const char *filename, offset_t fileptr, char mount_point, picos_dev_t output_device)
{
  signed char retval;
  file_handle_t file;
  mount_t mount;
  retval = picfs_open(filename,mount_point);
  if(retval < 0)
    {
      IO_puts("Could not open ");
      IO_puts(filename);
      IO_putd(error_code);
      error_code = 0;
      return;
    }
  file = (file_handle_t)retval;
  if(picfs_is_open(file))
    {
      SRAM_read(mount_point*sizeof(mount_t)+SRAM_MTAB_ADDR,&mount,sizeof(mount_t));
      while(picfs_load(file) == 0)
	{
	  switch(output_device)
	    {
	    case DEV_SRAM:
	      SRAM_write(fileptr,(void*)picfs_buffer,mount.block_size);
	      fileptr += mount.block_size;
	      break;
	    case DEV_RAW_FILE:
	      picfs_dump(file);
	      break;
	    case DEV_STDOUT:
	      IO_puts((char*)picfs_buffer);
	      break;
	    default:
	      break;
	    }
	}

      // This is not an unexpected even. We leave the while loop when an EOF is reached.
      if(error_code == PICFS_EOF)
	error_code = 0;

      picfs_close(file);
    }
  else
    {
      IO_puts("Could not open file");
    }
}

static char ISOPEN(file_handle_t fh)
{
  fh = 1 << fh;
  return ((fh & picfs_fh_bitmap) == 0);
}

static signed char picfs_get_free_handle( char *bitmap )
{
  signed char new_fh = 0,new_fh_mask = 1;
  if(bitmap == NULL)
    return error_return(PICFS_EINVAL);

  if(*bitmap == 0)
    return error_return(PICFS_ENFILE);

  while(new_fh_mask > 0)
    {
      if((new_fh_mask & *bitmap) != 0)
	break;
      new_fh_mask <<= 1;
      new_fh++;
    }

  *bitmap &= ~((char)new_fh_mask);

  return new_fh;
}
#define picfs_get_free_fh() picfs_get_free_handle(&picfs_fh_bitmap)
#define picfs_get_free_mtab_entry() picfs_get_free_handle(&picfs_mtab_bitmap)


signed char picfs_verify_fs(unsigned int sd_addr, picos_dev_t dev)
{
  char magic_numbers[FS_SuperBlock_block_size+1];
  memset(magic_numbers,0,FS_SuperBlock_block_size+1);

  device_read(sd_addr,&magic_numbers,FS_SuperBlock_block_size+1,dev);
   
  if(magic_numbers[0] != 0 || magic_numbers[1] != 0x6 || magic_numbers[2] != 0x29 || magic_numbers[3] != 0x82)
    {
      return error_return(PICFS_INVALID_FILESYSTEM);
    }
  if(magic_numbers[FS_SuperBlock_block_size] > FS_BUFFER_SIZE)
    return error_return( PICFS_INVALID_FILESYSTEM);

  return SUCCESS;
}

signed char picfs_umount(char mount_point)
{
  char mount_point_mask = 1 << mount_point;
  mount_t mount;
  unsigned int addr;
  
  if((mount_point_mask & picfs_mtab_bitmap) != 0)
    return PICFS_NOENT;

  if(curr_dir == mount_point)
    return PICFS_BUSY;

  addr = sizeof(mount_t) * mount_point + SRAM_MTAB_ADDR;
  memset(&mount,0,sizeof(mount_t));
  SRAM_write(addr,&mount,sizeof(mount_t));

  picfs_mtab_bitmap |= mount_point_mask;

  return SUCCESS;

}

/**
 * Mounts the device onto the lowest available mount point.
 *
 * Returns the mount point if successful
 *
 * On error, error_code is set and -1 is returned
 */
signed char picfs_mount(unsigned int fs_addr, picos_dev_t dev)
{
  signed char mtab_entry;
  unsigned int addr;
  mount_t mount;
  
  if(picfs_mtab_bitmap == 0)
    return error_return(PICFS_ENFILE);

  mtab_entry = picfs_get_free_mtab_entry();
  if(mtab_entry < 0)
    return -1;// error_code already set by previous function

  addr = sizeof(mount_t) * mtab_entry + SRAM_MTAB_ADDR;

  if(picfs_verify_fs(fs_addr,dev) != 0)
    {
      void picfs_free_handle(char *bitmap, file_handle_t fh);
      picfs_free_handle(&picfs_mtab_bitmap,mtab_entry);
      return -1;// error_code already set by previous function
    }

  mount.root_address = fs_addr;
  mount.device_id = dev;

  device_read(fs_addr + FS_SuperBlock_block_size,&mount.block_size,1,dev);
  
  SRAM_write(addr,&mount,sizeof(mount_t));

  return mtab_entry;
}

static signed char picfs_get_mtab(mount_t *mtab_entry, char mount)
{
  char mount_mask = 1;
  mount_mask <<= mount;

  if((mount_mask  & picfs_mtab_bitmap) != 0)
    return error_return(PICFS_EBADF);
  
  SRAM_read(mount*sizeof(mount_t)+SRAM_MTAB_ADDR,mtab_entry,sizeof(mount_t));
  return SUCCESS;
}

/**
 * Resolves a block to its physical address on the mounted device.
 */
static signed char picfs_getblock(picos_addr_t *addr, char mount, FS_Unit block_id)
{
  mount_t mtab_entry;
  picos_addr_t block_addr;
  signed char retval = SUCCESS;

  if(addr == NULL || picfs_mtab_bitmap == PICFS_MTAB_BITMAP_DEFAULT)
    return error_return(PICFS_EINVAL);

  retval = picfs_get_mtab(&mtab_entry,mount);
  
  if(retval != SUCCESS)
    return retval;
    
  *addr = mtab_entry.root_address + block_id * mtab_entry.block_size;
    
  return retval;
}

static signed char picfs_buffer_block(char mount, FS_Unit block_id)
{
  picos_addr_t address;
  signed char retval = SUCCESS;
  mount_t mtab_entry;
  
  // get address of block on the device
  retval = picfs_get_mtab(&mtab_entry,mount);
  if(retval != SUCCESS)
    return retval;
  address  = mtab_entry.root_address + block_id * mtab_entry.block_size;
  
  device_read(address,(void*)picfs_buffer,mtab_entry.block_size,mtab_entry.device_id);
  
  return retval;
}

/**
 * Returns a file handle for a file with the 
 * specified name
 *
 * Upon error, the error code is returned as a negative value.
 */
signed char picfs_open(const char *name, char mount_point)
{
  signed char retval = SUCCESS;
  char ch, pointer;
  char mount_point_mask = 1 << mount_point;
  mount_t mount;
  picos_dev_t dev;
  picos_addr_t sram_addr,addr, inode;

  if(picfs_fh_bitmap == 0)
    return error_return(PICFS_ENFILE);

  if((mount_point_mask & picfs_mtab_bitmap) != 0)
    return error_return(PICFS_EBADF);

  // Get Device address and ID info
  sram_addr = mount_point*sizeof(mount_t) + SRAM_MTAB_ADDR;
  SRAM_read(sram_addr,&mount,sizeof(mount_t));
  dev = mount.device_id;

  inode = mount.root_address + FS_SuperBlock_root_block;
  device_read(inode,&ch,1,mount.device_id);
  if(ch == 0)
    return error_return(PICFS_NOENT);
  
  retval = picfs_getblock(&inode,mount_point,ch); // now contains inode's address
  if(retval != SUCCESS)
    return retval;
  
  inode += FS_INode_pointers;// now contains the first pointer
  pointer = 0;
  for(;pointer < FS_INode_pointers;pointer++,inode++)
    {
      char entrypos = 0;
      SRAM_write(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,mount.block_size);
      device_read(inode,&ch,1,dev);//dir entry listing
      if(ch == 0)
	break;
      retval = picfs_getblock(&addr,mount_point,ch);
      if(retval != SUCCESS)
	return retval;
      
      while(entrypos < mount.block_size)
	{
	  device_read(addr,&ch,1,dev);
	  if(ch == 0)
	    break;
	  addr++;
	  entrypos++;
	  device_read(addr,(void*)picfs_buffer,ch,dev);// filename
	  picfs_buffer[ch] = 0;
	  addr += ch;
	  entrypos += ch;
	  if(strcmp((char*)picfs_buffer,name) == 0)
	    {
	      signed char fh;
	      unsigned int ftab_addr;
	      file_t ftab_entry;
	      SRAM_read(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,mount.block_size);// restore swapped memory
	      device_read(addr,&ch,1,dev);// first inode in file
	      fh = picfs_get_free_fh();//file handle
	      if(fh < 0)
		{
		  return error_return(PICFS_ENFILE);
		}
	      ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
	      ftab_entry.first_inode = ch;
	      ftab_entry.mount_point = mount_point;
	      ftab_entry.inode_position = 0;
	      ftab_write(ftab_addr,&ftab_entry,sizeof(file_t));
	      return (signed char)fh;
	    }
	  addr++;
	  entrypos++;
	}
    }
  SRAM_read(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,mount.block_size);// restore swapped memory
  
  return error_return(PICFS_NOENT);

}

signed char picfs_stat(file_handle_t fh)
{
  unsigned int size = 0;
  picos_addr_t addr;
  char val;
  file_t file;
  mount_t mount;
  picos_dev_t dev;
  char inode;
  signed char retval = SUCCESS;
  picos_size_t number_of_pointers;
  
  if(!ISOPEN(fh) || fh == 0xff)
    return error_return(PICFS_EBADF);

  // Get File info
  ftab_read(fh*sizeof(file_t)+SRAM_PICFS_FTAB_ADDR,&file,sizeof(file_t));// mtab entry
  SRAM_read(file.mount_point*sizeof(mount_t)+SRAM_MTAB_ADDR,&mount,sizeof(mount_t));
  dev = mount.device_id;
  number_of_pointers = mount.block_size - FS_INode_pointers;

  // Get address of first inode
  inode = file.first_inode;
  retval = picfs_getblock(&addr,file.mount_point,file.first_inode);
  if(retval != SUCCESS)
    return retval;
  
  while(inode != 0)
    {
      addr += FS_INode_indirect;
      device_read(addr,&val,1,dev);
      addr = addr - FS_INode_indirect;
      
      if(val == 0)
	{
	  char pointer_counter = 1;
	  addr += FS_INode_size;// FS_INode_size
	  device_read(addr,&val,1,dev);
	  addr = addr - FS_INode_size;
	  size += val;
	  addr += FS_INode_pointers + 1;
	  for(;pointer_counter < number_of_pointers;pointer_counter++)
	    {
	      device_read(addr,&val,1,dev);
	      if(val == 0)
		break;
	      else
		size += mount.block_size;
	      addr++;
	    }
	  break;
	}
      size += number_of_pointers*mount.block_size;
      inode = val;
    }
  inode = 0;
  for(;inode < mount.block_size;inode++)
    picfs_buffer[inode] = 0;

  picfs_buffer[ST_SIZE] = (char)((size >>8) & 0xff);
  picfs_buffer[ST_SIZE+1] = (char)(size & 0xff);

  return 0;
}

signed char picfs_dump(file_handle_t fh)
{
  char num_free,first_block,second_block;
  char write_overflow[2];
  picos_addr_t addr;
  file_t file;
  picos_dev_t dev;
  mount_t mount;
   
  if(!ISOPEN(fh) || fh == 0xff)
    return error_return(PICFS_EBADF);
   
  ftab_read(fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR,&file,sizeof(file_t));
  SRAM_read(file.mount_point*sizeof(mount_t) + SRAM_MTAB_ADDR,&mount,sizeof(mount_t));
  dev = mount.device_id;
  
  // swap out the picfs_buffer
  SRAM_write(SRAM_PICFS_WRITE_SWAP_ADDR,(void*)picfs_buffer,mount.block_size);

  // Load Super Block. It is needed for the inode of the raw file.
  device_read(mount.root_address,(void*)picfs_buffer,mount.block_size,mount.device_id);
  num_free = picfs_buffer[FS_SuperBlock_num_free_blocks];

  if(num_free < 2)
    return error_return(PICFS_ENOMEM);

  // If we have re-mounted the file system, figure out where the end of
  // the raw file linked list is
  if(picfs_last_raw_block == 0 && picfs_buffer[FS_SuperBlock_raw_file] != 0)
    {
      FS_Unit free_queue = picfs_buffer[FS_SuperBlock_free_queue]; 
      mount_t mount;
      SRAM_read(file.mount_point*sizeof(mount_t)+SRAM_MTAB_ADDR,&mount,sizeof(mount_t));
      picfs_getblock(&addr,file.mount_point,picfs_buffer[FS_SuperBlock_raw_file]);
      device_read(addr,(void*)picfs_buffer,mount.block_size,mount.device_id);
      while(picfs_buffer[1] != 0)
	{
	  picfs_getblock(&addr,file.mount_point,picfs_buffer[1]);
	  picfs_last_raw_block = picfs_buffer[1];
	  device_read(addr,(void*)picfs_buffer,FS_INode_length,mount.device_id);
	}

      picfs_buffer[FS_INode_pointers] = free_queue;
      device_read(mount.root_address,(void*)picfs_buffer,mount.block_size,mount.device_id);
    }

  //Get first raw block
  first_block = picfs_buffer[FS_SuperBlock_free_queue];
  picfs_getblock(&addr,file.mount_point,first_block);
  device_read(addr,(void*)picfs_buffer,FS_INode_length,dev);
  second_block = picfs_buffer[FS_INode_pointers];

  // Update free queue
  picfs_getblock(&addr,file.mount_point,second_block);
  device_read(addr,(void*)picfs_buffer,FS_INode_length,dev);
  num_free = picfs_buffer[FS_INode_pointers];// borrowing num_free to save memory
  device_read(mount.root_address,(void*)picfs_buffer,mount.block_size,dev);
  picfs_buffer[FS_SuperBlock_num_free_blocks] -= 2;
  picfs_buffer[FS_SuperBlock_free_queue] = num_free;
  if(picfs_last_raw_block == 0)
    picfs_buffer[FS_SuperBlock_raw_file] = first_block;
  device_write((void*)picfs_buffer,mount.root_address,mount.block_size,dev);

  // If raw has alread been written to, update the previous block so that it points to the first_block, continuing the linked list
  if(picfs_last_raw_block != 0)
    {
      picfs_getblock(&addr,file.mount_point,picfs_last_raw_block);
      device_read(addr,(void*)picfs_buffer,2,dev);
      picfs_buffer[1] = first_block;
      device_write((void*)picfs_buffer,addr,2,dev);
    }
  picfs_last_raw_block = second_block;

  //Save some data to the first block
  picfs_getblock(&addr,file.mount_point,first_block);
  SRAM_read(SRAM_PICLANG_RAW_FILE_BUFFER,(void*)picfs_buffer,mount.block_size);// load raw buffer
  memcpy(write_overflow,(void*)picfs_buffer+mount.block_size-2,2);//save the last two.
  first_block = mount.block_size-1;// shift the buffer
  for(;first_block > 1;first_block--)
    picfs_buffer[first_block] = picfs_buffer[first_block-2];
  picfs_buffer[0] = MAGIC_RAW;// add the raw file block header
  picfs_buffer[1] = second_block;
  device_write((void*)picfs_buffer,addr,mount.block_size,dev);// write first block

  // write second raw block
  picfs_getblock(&addr,file.mount_point,second_block);
  memset((void*)picfs_buffer,0,mount.block_size);
  picfs_buffer[0] = MAGIC_RAW;
  memcpy((void*)picfs_buffer+2,write_overflow,2);// add the last two back to the buffer
  device_write((void*)picfs_buffer,addr,mount.block_size,dev);//write 6 to clean the pointer away

  // Swap picfs_buffer back
  SRAM_read(SRAM_PICFS_WRITE_SWAP_ADDR,(void*)picfs_buffer,mount.block_size);

  return SUCCESS;
}

signed char picfs_load(file_handle_t fh)
{
  char inode,ptr;
  unsigned int ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
  offset_t nextnode;
  picos_addr_t addr;
  file_t file;
  mount_t mount;
  picos_size_t number_of_pointers;
   
  //Is this file open?
  if(!ISOPEN(fh) || fh == 0xff)
    return error_return(PICFS_EBADF);

  // Get the head inode. Is it legit?
  ftab_read(ftab_addr,&file,sizeof(file_t));
  inode = file.first_inode;
  if(inode == 0)
    return error_return(PICFS_EBADF);
  picfs_stat(fh);

  // get device info
  SRAM_read(file.mount_point*sizeof(mount_t)+SRAM_MTAB_ADDR,&mount,sizeof(mount_t));
  nextnode = file.inode_position;
  number_of_pointers = mount.block_size - FS_INode_pointers;

  // update file info
  file.inode_position++;
  ftab_write(ftab_addr,&file,sizeof(file_t));
  
  while(nextnode >= number_of_pointers)
    {
      picfs_getblock(&addr,file.mount_point,inode);
      addr += FS_INode_indirect;
      device_read(addr,&ptr,1,mount.device_id);
      if(ptr == 0)
	return error_return(PICFS_EOF);
      inode = ptr;
      nextnode -= number_of_pointers;
    }
  
  picfs_getblock(&addr,file.mount_point,inode);
  addr += nextnode + FS_INode_pointers;
  device_read(addr,&ptr,1,mount.device_id);
  if(ptr == 0)
    return error_return(PICFS_EOF);
  
  return picfs_buffer_block(file.mount_point,ptr);
}

signed char picfs_select_block(file_handle_t fh, picos_size_t block_id)
{
  file_t file;
  unsigned int ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;

  //Is this file open?
  if(!ISOPEN(fh) || fh == 0xff)
    return error_return(PICFS_EBADF);

  ftab_read(ftab_addr,&file,sizeof(file_t));
  file.inode_position = block_id;
  ftab_write(ftab_addr,&file,sizeof(file_t));
  
  return 0;
}

void picfs_free_handle(char *bitmap, file_handle_t fh)
{
  char mask = 1 << fh;
  if(bitmap != NULL)
    *bitmap |= mask;
}

signed char picfs_close(file_handle_t fh)
{
  unsigned int ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
  char idx = 0;
  if(!ISOPEN(fh) || fh == 0xff)
    return error_return(PICFS_EBADF);

  picfs_free_handle(&picfs_fh_bitmap,fh);
  return SUCCESS;
}

char picfs_is_open(file_handle_t fh)
{
  if(ISOPEN(fh) && fh != 0xff)
    return true;
  return false;
}

signed char picfs_chdir(char mount_point)
{
  curr_dir = mount_point;
  return SUCCESS;
}

signed char picfs_readdir(char mount_point, picos_size_t block_id)
{
  signed char retval = SUCCESS;
  char ch, pointer;
  char mount_point_mask = 1 << mount_point;
  mount_t mount;
  picos_dev_t dev;
  picos_addr_t sram_addr,addr, inode;

  if(picfs_fh_bitmap == 0)
    return error_return(PICFS_ENFILE);

  if((mount_point_mask & picfs_mtab_bitmap) != 0)
    return error_return(PICFS_EBADF);

  // Get Device address and ID info
  sram_addr = mount_point*sizeof(mount_t) + SRAM_MTAB_ADDR;
  SRAM_read(sram_addr,&mount,sizeof(mount_t));
  dev = mount.device_id;

  inode = mount.root_address + FS_SuperBlock_root_block;
  device_read(inode,&ch,1,mount.device_id);
  if(ch == 0)
    return error_return(PICFS_NOENT);
  
  retval = picfs_getblock(&inode,mount_point,ch); // now contains inode's address
  if(retval != SUCCESS)
    return retval;
  
  inode += FS_INode_pointers;// now contains the first pointer
  pointer = 0;
  for(;pointer < FS_INode_pointers;pointer++,inode++)
    {
      char entrypos = 0;
      device_read(inode,&ch,1,dev);//dir entry listing
      if(ch == 0)
	break;
      retval = picfs_getblock(&addr,mount_point,ch);
      if(retval != SUCCESS)
	return retval;
      
      if(pointer == block_id)
	{
	  device_read(addr,&ch,1,dev);
	  if(ch == 0)
	    break;
	  device_read(addr,(void*)picfs_buffer,mount.block_size,dev);// filename
	  
	  pointer = 0;
	  while(pointer < mount.block_size)
	    {
	      ch = picfs_buffer[pointer];
	      if(ch == 0)
		return SUCCESS;
	      picfs_buffer[pointer] = ' ';
	      pointer += ch + 1;
	      picfs_buffer[pointer] = ' ';
	      pointer ++;
	    }

	  return SUCCESS;
	}
    }

  return error_return(PICFS_EOF);

}

void lsmount()
{
  char *outbuf = (char*)picfs_buffer;
  mount_t curr;
  char mount_point, mount_mask;
  picos_size_t addr;

  if(outbuf == NULL)
    return;

  mount_point = 0;
  if(FS_BUFFER_SIZE < 13*MTAB_ENTRY_MAX + 1)
    {
      if(FS_BUFFER_SIZE > 17)
	strcat(outbuf,"Buffer to small.");
      return;
    }
  
  memset(outbuf,0,FS_BUFFER_SIZE*sizeof(char));
  for(;mount_point < MTAB_ENTRY_MAX;mount_point++)
    {
      mount_mask = 1 << mount_point;
      if((mount_mask & picfs_mtab_bitmap) != 0)
	continue;

      addr = sizeof(mount_t) * mount_point + SRAM_MTAB_ADDR;
      memset(&curr,0,sizeof(mount_t));
      SRAM_read(addr,&curr,sizeof(mount_t));
      
      *outbuf = '/';outbuf++;
      hex_to_word(outbuf,mount_point);outbuf += 2;
      *outbuf = ' ';outbuf++;
      strcat(outbuf,"/dev/");outbuf += 5;
      hex_to_word(outbuf,curr.device_id);outbuf += 2;
      *outbuf = '\n';outbuf++;
    }
  *outbuf = 0;
}


