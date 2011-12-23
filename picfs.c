 /**
  * PICOS, PIC operating system.
  * Author: David Coss, PhD
  * Date: 21 Dec 2011
  * License: GNU Public License version 3.0 (see http://www.gnu.org)
  *
  * This file has the code behind the file system.
  */
 #include "picfs.h"
 #include "picfs_error.h"
 #include "sd.h"
 #include "io.h"
 #include "sram.h"

 #ifdef HAVE_CONFIG_H
 #include "config.h"
 #endif

 #include <string.h>


  extern void ftab_write(unsigned int address, unsigned char val);
  extern char ftab_read(unsigned int address);
  FS_Unit picfs_last_raw_block = 0;
 char picfs_fh_bitmap = 0xff;// max 8 files open. defined with MAX_OPEN_FILES

 #define ISOPEN(fh) ((fh & ~picfs_fh_bitmap) == 0)
 #define FS_BLOCK_SIZE FS_BUFFER_SIZE


// First four bytes are address within the devices of the beginning of the
// files system.
// Fifth byte is the device ID (see picfs.h for definitions)
 #define PICFS_MTAB_BITMAP_DEFAULT 0x3f// six entries in the table
 char picfs_mtab_bitmap = PICFS_MTAB_BITMAP_DEFAULT;

 void picfs_offset_addr(char *sd_addr, signed int offset)
 {
   long large_addr;
   large_addr = sd_addr[0];large_addr <<= 8;
   large_addr += sd_addr[1];large_addr <<= 8;
   large_addr += sd_addr[2];large_addr <<= 8;
   large_addr += sd_addr[3];

   large_addr += offset;

   sd_addr[3] = large_addr & 0xff;large_addr >>= 8;
   sd_addr[2] = large_addr & 0xff;large_addr >>= 8;
   sd_addr[1] = large_addr & 0xff;large_addr >>= 8;
   sd_addr[0] = large_addr & 0xff;
 }

static void device_read(unsigned int address, void *buffer, char size, dev_t dev)
{
  switch(dev)
    {
    case DEV_SDCARD:
      {
	char sd_addr[4];
	memset(sd_addr,0,4);
	picfs_getblock(sd_addr,(signed int)address);
	SD_read(sd_addr,buffer,size);
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

static void device_write(void *buffer, unsigned int address, char size, dev_t dev)
{
  switch(dev)
    {
    case DEV_SDCARD:
      {
	char sd_addr[4];
	memset(sd_addr,0,4);
	picfs_getblock(sd_addr,(signed int)address);
	SD_write(buffer,address,size);
	break;
      }
    case DEV_EEPROM:
      {
	char eeprom_addr = address[3], idx = 0;
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


void cat_file(const char *filename, offset_t fileptr, char mount_point,dev_t dev)
 {
   signed char retval;
   file_handle_t file;
   retval = picfs_open(filename,mount_point);
   if(retval < 0)
     {
       IO_puts("Could not open ");
       IO_puts(filename);
       IO_putd(error_code);
       error_code = 0;
     }
   file = (file_handle_t)retval;
   if(picfs_is_open(file))
     {
       while(picfs_read(file) == 0)
	 {
	   if(dev == DEV_SRAM)
	     {
	       SRAM_write(fileptr,(void*)picfs_buffer,FS_BUFFER_SIZE);
	       fileptr += FS_BUFFER_SIZE;
	     }
	   else if(dev == DEV_RAW_FILE)
	     picfs_write(0);
	   else if(dev == DEV_STDOUT)
	     IO_puts((char*)picfs_buffer);
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


signed char picfs_verify_fs(unsigned int sd_addr, dev_t dev)
 {
   char magic_numbers[FS_SuperBlock_block_size+1];
   memset(magic_numbers,0,FS_SuperBlock_block_size+1);

   device_read(sd_addr,&magic_numbers,FS_SuperBlock_block_size+1,dev);
   
   if(magic_numbers[0] != 0 && magic_numbers[1] != 0x6 && magic_numbers[2] != 0x29 && magic_numbers[3] != 0x83)
     {
       return error_return(PICFS_INVALID_FILESYSTEM);
     }
   if(magic_numbers[FS_SuperBlock_block_size] != FS_BLOCK_SIZE)
     return error_return( PICFS_INVALID_FILESYSTEM/*FS_BLOCK_SIZE */);

   return SUCCESS;
 }

 /**
  * Mounts the device onto the lowest available mount point.
  *
  * Returns the mount point if successful
  *
  * On error, error_code is set and -1 is returned
  */
signed char picfs_mount(unsigned int fs_addr, dev_t dev)
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
   
   SRAM_write(addr,&mount,sizeof(mount_t));
   
   return mtab_entry;
 }

 static signed char picfs_getdir(char *addr, char mount_point)
 {
   char mount_point_mask = 1 << mount_point;
   if((mount_point_mask & picfs_mtab_bitmap) != 0)
     return error_return(PICFS_EBADF);

   SRAM_read(mount_point*SIZE_picfs_mtab_entry + SRAM_MTAB_ADDR,addr,SIZE_picfs_mtab_entry);
   return 0;
 }

 signed char picfs_chdir(char mount_point)
 {
   char oldpwd[SDCARD_ADDR_SIZE];
   signed char retval = 0;
   memcpy(oldpwd,picfs_pwd,SDCARD_ADDR_SIZE);
   retval = picfs_getdir(picfs_pwd,mount_point);
   if(retval != SUCCESS)
     {
       memcpy(picfs_pwd,oldpwd,SDCARD_ADDR_SIZE);
       return -1;
     }
   return 0;
 }

 /**
  * Resolves a block id to its SD address
  */
static signed char picfs_getblock(char *addr, FS_Unit block_id)
 {
   unsigned int larger;
   char counter;
   if(addr == NULL)
     return error_return(PICFS_EINVAL);

   if(picfs_mtab_bitmap == PICFS_MTAB_BITMAP_DEFAULT)
     return error_return(PICFS_EINVAL);

   counter = 0;
   larger = picfs_pwd[SDCARD_ADDR_SIZE - 2];
   larger <<= 8;
   larger = picfs_pwd[SDCARD_ADDR_SIZE - 1];

   addr[0] = addr[1] = 0;
   if(0xffff - block_id*FS_BLOCK_SIZE < larger)
     addr[1] = 1;
   larger += block_id * FS_BLOCK_SIZE;
   addr[3] = larger & 0xff;
   larger &= 0xff00;
   addr[2] = larger >> 8;

   return SUCCESS;
 }

static char picfs_buffer_block(FS_Unit block_id, dev_t dev)
 {
   char addr[SDCARD_ADDR_SIZE];
   picfs_getblock(addr,block_id);
   device_read(addr,picfs_buffer,FS_BUFFER_SIZE,dev);
   return SUCCESS;
 }

 /**
  * Returns a file handle for a file with the 
  * specified name
  *
  * Upon error, the error code is returned as a negative value.
  */
signed char picfs_open(const char *name, char mount_point)
 {
   char addr[SDCARD_ADDR_SIZE], inode[SDCARD_ADDR_SIZE];
   char ch, pointer;
   char mount_point_mask = 1 << mount_point;
   dev_t dev;
   unsigned inty sram_addr;
   if(picfs_fh_bitmap == 0)
     return error_return(PICFS_ENFILE);

   if((mount_point_mask & picfs_mtab_bitmap) != 0)
     return error_return(PICFS_EBADF);

   // Get Device address and ID info
   sram_addr = mount_point*SIZE_picfs_mtab_entry;
   SRAM_read(sram_addr,inode,SDCARD_ADDR_SIZE);
   sram_addr += SIZE_picfs_mtab_entry;
   SRAM_read(sram_addr,&dev,sizeof(dev));

   picfs_offset_addr(inode,FS_SuperBlock_root_block);
   dev_read(inode,&ch,1,dev);
   if(ch == 0)
     return error_return(PICFS_NOENT);

   picfs_getblock(inode,ch); // now contains inode's address
   picfs_offset_addr(inode,FS_INode_pointers);// now contains the first pointer
   pointer = 0;
   for(;pointer < FS_INode_pointers;pointer++,picfs_offset_addr(inode,1))
     {
       char entrypos = 0;
       SRAM_write(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,FS_BUFFER_SIZE);
       device_read(inode,&ch,1,dev);//dir entry listing
       if(ch == 0)
	 break;
       picfs_getblock(addr,ch);
       while(entrypos < FS_BUFFER_SIZE)
	 {
	   device_read(addr,&ch,1,dev);
	   if(ch == 0)
	     break;
	   picfs_offset_addr(addr,1);entrypos++;
	   device_read(addr,(void*)picfs_buffer,ch,dev);// filename
	   picfs_buffer[ch] = 0;
	   picfs_offset_addr(addr,ch);entrypos += ch;
	   if(strcmp((char*)picfs_buffer,name) == 0)
	     {
	       signed char fh;
	       unsigned int ftab_addr;
	       SRAM_read(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,FS_BUFFER_SIZE);// restore swapped memory
	       device_read(addr,&ch,1,dev);// first inode in file
	       fh = picfs_get_free_fh();//file handle
	       if(fh < 0)
		 {
		   return error_return(PICFS_ENFILE);
		 }
	       ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
	       ftab_write(ftab_addr+FIRST_INODE,ch);
	       ftab_write(ftab_addr+MSB_INODE_OFFSET,0);
	       ftab_write(ftab_addr+LSB_INODE_OFFSET,0);
	       return (signed char)fh;
	     }
	   picfs_offset_addr(addr,1);entrypos++;
	 }
     }
    SRAM_read(SRAM_PICFS_OPEN_SWAP_ADDR,(void*)picfs_buffer,FS_BUFFER_SIZE);// restore swapped memory
   return error_return(PICFS_NOENT);

 }

signed char picfs_stat(file_handle_t fh)
 {
   unsigned int size = 0;
   char addr[SDCARD_ADDR_SIZE];
   char inode,val;
   dev_t dev;
   if(!ISOPEN(fh))
     return error_return(PICFS_EBADF);

   // Get Device info
   val = ftab_read(fh*FILE_HANDLE_SIZE+MOUNT_POINT+SRAM_PICFS_FTAB_ADDR);// mtab entry
   size = val*SIZE_picfs_mtab_entry + SRAM_PICFS_MTAB_ADDR;
   picfs_getblock(addr,size);
   SRAM_read(addr,

   
   inode = ftab_read(fh*FILE_HANDLE_SIZE+FIRST_INODE+SRAM_PICFS_FTAB_ADDR);
   picfs_getblock(addr,inode);
   while(inode != 0)
     {
       picfs_offset_addr(addr,FS_INode_indirect);
       device_read(addr,&val,1,dev);
       picfs_offset_addr(addr,-FS_INode_indirect);
       if(val == 0)
	 {
	   char pointer_counter = 1;
	   picfs_offset_addr(addr,FS_INode_size);// FS_INode_size
	   device_read(addr,&val,1,dev);
	   picfs_offset_addr(addr,-FS_INode_size);
	   size += val;
	   picfs_offset_addr(addr,FS_INode_pointers + 1);
	   for(;pointer_counter < FS_INODE_NUM_POINTERS;pointer_counter++)
	     {
	       device_read(addr,&val,1,dev);
	       if(val == 0)
		 break;
	       else
		 size += FS_BLOCK_SIZE;
	       picfs_offset_addr(addr,1);
	     }
	   break;
	 }
       size += FS_INODE_NUM_POINTERS*FS_BLOCK_SIZE;
       inode = val;
     }
   inode = 0;
   for(;inode < FS_BUFFER_SIZE;inode++)
     picfs_buffer[inode] = 0;

   picfs_buffer[ST_SIZE] = (char)((size >>8) & 0xff);
   picfs_buffer[ST_SIZE+1] = (char)(size & 0xff);

   return 0;
 }

signed char picfs_seek(file_handle_t fh, offset_t offset, char whence)
 {
   offset_t curr,size;
   unsigned int ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
   if(!ISOPEN(fh))
     return error_return(PICFS_EBADF);

   curr = picfs_stat(fh);
   if(curr != 0)
     return curr;
   size = picfs_buffer[ST_SIZE];
   size <<= 8;
   size |= picfs_buffer[ST_SIZE+1];

   curr = ftab_read(ftab_addr+MSB_INODE_OFFSET);
   curr <<= 8;
   curr |= ftab_read(ftab_addr+LSB_INODE_OFFSET);
   //check to see if offset(whence) is past EOF
   switch(whence)
     {
     case PICFS_SET:
       curr = offset;
       break;
     case PICFS_CURR:
       curr += offset;
       break;
     case PICFS_END:
       curr = size + offset;
       break;
     case PICFS_REVERSE:
       if(offset > size)
	 curr = 0;
       else
	 curr = size - offset;
     default:
       return error_return(PICFS_EINVAL);
     }
   return 0;
 }

 signed char picfs_write(file_handle_t fh)
 {
   char num_free,first_block,second_block;
   char addr[SDCARD_ADDR_SIZE], write_overflow[2];
   dev_t dev;
   
   if(!ISOPEN(fh))
     return error_return(PICFS_EBADF);
   
   dev = ftab_read(fh*FILE_HANDLE_SIZE + DEVICE_ID + SRAM_PICFS_FTAB_ADDR);
   
   // swap out the picfs_buffer
   SRAM_write(SRAM_PICFS_WRITE_SWAP_ADDR,(void*)picfs_buffer,FS_BUFFER_SIZE);
   device_read(picfs_pwd,(void*)picfs_buffer,FS_BLOCK_SIZE,dev);
   num_free = picfs_buffer[FS_SuperBlock_num_free_blocks];

   if(num_free < 2)
     return error_return(PICFS_ENOMEM);

   // If we have re-mounted the file system, figure out where the end of
   // the raw file linked list is
   if(picfs_last_raw_block == 0 && picfs_buffer[FS_SuperBlock_raw_file] != 0)
     {
       FS_Unit free_queue = picfs_buffer[FS_SuperBlock_free_queue]; 
       picfs_getblock(addr,picfs_buffer[FS_SuperBlock_raw_file]);
       device_read(addr,(void*)picfs_buffer,FS_BLOCK_SIZE,dev);
       while(picfs_buffer[1] != 0)
	 {
	   picfs_getblock(addr,picfs_buffer[1]);
	   picfs_last_raw_block = picfs_buffer[1];
	   device_read(addr,(void*)picfs_buffer,FS_INode_length,dev);
	 }

       picfs_buffer[FS_INode_pointers] = free_queue;
       device_read(picfs_pwd,(void*)picfs_buffer,FS_BLOCK_SIZE,dev);
     }

   //Get first raw block
   first_block = picfs_buffer[FS_SuperBlock_free_queue];
   picfs_getblock(addr,first_block);
   device_read(addr,(void*)picfs_buffer,FS_INode_length,dev);
   second_block = picfs_buffer[FS_INode_pointers];

   // Update free queue
   picfs_getblock(addr,second_block);
   device_read(addr,(void*)picfs_buffer,FS_INode_length,dev);
   num_free = picfs_buffer[FS_INode_pointers];// borrowing num_free to save memory
   device_read(picfs_pwd,(void*)picfs_buffer,FS_BLOCK_SIZE,dev);
   picfs_buffer[FS_SuperBlock_num_free_blocks] -= 2;
   picfs_buffer[FS_SuperBlock_free_queue] = num_free;
   if(picfs_last_raw_block == 0)
     picfs_buffer[FS_SuperBlock_raw_file] = first_block;
   device_write((void*)picfs_buffer,picfs_pwd,FS_BLOCK_SIZE,dev);

   // If raw has alread been written to, update the previous block so that it points to the first_block, continuing the linked list
   if(picfs_last_raw_block != 0)
     {
       picfs_getblock(addr,picfs_last_raw_block);
       device_read(addr,(void*)picfs_buffer,2,dev);
       picfs_buffer[1] = first_block;
       device_write((void*)picfs_buffer,addr,2,dev);
     }
   picfs_last_raw_block = second_block;

   //Save some data to the first block
   picfs_getblock(addr,first_block);
   SRAM_read(SRAM_PICFS_WRITE_SWAP_ADDR,(void*)picfs_buffer,FS_BUFFER_SIZE);// restore the swapped-out data
   memcpy(write_overflow,(void*)picfs_buffer+FS_BUFFER_SIZE-2,2);//save the last two.
   first_block = FS_BUFFER_SIZE-1;// shift the buffer
   for(;first_block > 1;first_block--)
       picfs_buffer[first_block] = picfs_buffer[first_block-2];
   picfs_buffer[0] = MAGIC_RAW;// add the raw file block header
   picfs_buffer[1] = second_block;
   device_write((void*)picfs_buffer,addr,FS_BUFFER_SIZE,dev);// write first block

   // write second raw block
   picfs_getblock(addr,second_block);
   memset((void*)picfs_buffer,0,FS_BUFFER_SIZE);
   picfs_buffer[0] = MAGIC_RAW;
   memcpy((void*)picfs_buffer+2,write_overflow,2);// add the last two back to the buffer
   device_write((void*)picfs_buffer,addr,FS_BUFFER_SIZE,dev);//write 6 to clean the pointer away

   return 0;
 }

 signed char picfs_read(file_handle_t fh)
 {
   char inode,ptr;
   unsigned int ftab_addr = fh*FILE_HANDLE_SIZE + SRAM_PICFS_FTAB_ADDR;
   offset_t nextnode;
   char addr[SDCARD_ADDR_SIZE];
   dev_t dev;
   
   //Is this file open?
   if(!ISOPEN(fh))
     return error_return(PICFS_EBADF);

   // Get the head inode. Is it legit?
   inode = ftab_read(ftab_addr+FIRST_INODE);
   if(inode == 0)
     return error_return(PICFS_EBADF);
   picfs_stat(fh);

   // get device
   dev = ftab_read(ftab_addr+DEVICE_ID);

   nextnode = ftab_read(ftab_addr + MSB_INODE_OFFSET);
   nextnode <<= 8;
   nextnode |= ftab_read(ftab_addr + LSB_INODE_OFFSET);
  
  ftab_write(ftab_addr+MSB_INODE_OFFSET,(char)(nextnode + 1>>8) & 0xff);
  ftab_write(ftab_addr+LSB_INODE_OFFSET,(char)((nextnode+1) & 0xff));
  while(nextnode >= FS_INODE_NUM_POINTERS)
    {
      picfs_getblock(addr,inode);
      picfs_offset_addr(addr,FS_INode_indirect);
      device_read(addr,&ptr,1,dev);
      if(ptr == 0)
	return error_return(PICFS_EOF);
      inode = ptr;
      nextnode -= FS_INODE_NUM_POINTERS;
    }
  
  picfs_getblock(addr,inode);
  ptr = (char)nextnode + FS_INode_pointers;
  picfs_offset_addr(addr,ptr);
  device_read(addr,&ptr,1,dev);
  if(ptr == 0)
    return error_return(PICFS_EOF);
  
  return picfs_buffer_block(ptr,dev);
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
  if(!ISOPEN(fh))
    return error_return(PICFS_EBADF);

  for(;idx<FILE_HANDLE_SIZE;idx++)
    ftab_write(ftab_addr+idx,0);
  
  picfs_free_handle(&picfs_fh_bitmap,fh);
  return 0;
}

char picfs_is_open(file_handle_t fh)
{
  if(ISOPEN(fh))
    return TRUE;
  return FALSE;
}
