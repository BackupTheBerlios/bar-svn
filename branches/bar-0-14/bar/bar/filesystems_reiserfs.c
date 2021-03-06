/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/filesystems_reiserfs.c,v $
* $Revision: 1.3 $
* $Author: torsten $
* Contents: Backup ARchiver ReiserFS file system plug in
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
#define REISERFS_SUPER_BLOCK_OFFSET (64*1024)
#define REISERFS_SUPER_MAGIC_STRING_V1 "ReIsErFs"
#define REISERFS_SUPER_MAGIC_STRING_V2 "ReIsEr2Fs"
#define REISERFS_SUPER_MAGIC_STRING_V3 "ReIsEr3Fs"
#define REISERFS_SUPER_MAGIC_STRING_V4 "ReIsEr4"

#define REISERFS_MAX_BLOCK_SIZE 8192

/***************************** Datatypes *******************************/
typedef struct
{
  uint   blockSize;                             // block size (1024, 2048, 4096, 8192)
  ulong  firstDataBlock;                        // first data block
  uint32 totalBlocks;                           // total number of blocks
  int    bitmapIndex;                           // index of currently read bitmap
  uchar  bitmapData[REISERFS_MAX_BLOCK_SIZE];   // bitmap block data
} REISERFSHandle;

typedef struct
{
  uint32 blockCount;
  uint32 freeBlocks;
  uint32 rootBlock;
  uint32 journal[8];
  uint16 blockSize;
  uint16 oidMaxSize;
  uint16 oidCurrentSize;
  uint16 state;
  char   magicString[12];
  uint32 hashFunctionCode;
  uint16 treeHeight;
  uint16 bitmapNumber;
  uint16 version;
  uint16 reserver0;
  uint32 inodeGeneration;
  uint32 flags;
  uchar  uuid[16];
  uchar  label[16];
  byte   unused0[88];
} ReiserSuperBlock ATTRIBUTE_PACKED;

/***************************** Variables *******************************/

/****************************** Macros *********************************/
#define REISERFS_BLOCK_TO_OFFSET(reiserFSHandle,block) ((block)*reiserFSHandle->blockSize)

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

LOCAL bool REISERFS_init(DeviceHandle *deviceHandle, REISERFSHandle *reiserFSHandle)
{
  ReiserSuperBlock reiserSuperBlock;

  assert(deviceHandle != NULL);
  assert(reiserFSHandle != NULL);

  /* read super-block */
  if (Device_seek(deviceHandle,REISERFS_SUPER_BLOCK_OFFSET) != ERROR_NONE)
  {
    return FALSE;
  }
  if (Device_read(deviceHandle,&reiserSuperBlock,sizeof(reiserSuperBlock),NULL) != ERROR_NONE)
  {
    return FALSE;
  }

  /* check if this a ReiserFS super block */
  if (   (strncmp(reiserSuperBlock.magicString,REISERFS_SUPER_MAGIC_STRING_V1,strlen(REISERFS_SUPER_MAGIC_STRING_V1)) != 0)
      && (strncmp(reiserSuperBlock.magicString,REISERFS_SUPER_MAGIC_STRING_V3,strlen(REISERFS_SUPER_MAGIC_STRING_V2)) != 0)
      && (strncmp(reiserSuperBlock.magicString,REISERFS_SUPER_MAGIC_STRING_V3,strlen(REISERFS_SUPER_MAGIC_STRING_V3)) != 0)
      && (strncmp(reiserSuperBlock.magicString,REISERFS_SUPER_MAGIC_STRING_V4,strlen(REISERFS_SUPER_MAGIC_STRING_V4)) != 0)
     )
  {
    return FALSE;
  }

  /* get file system block info */
  reiserFSHandle->totalBlocks    = LE32_TO_HOST(reiserSuperBlock.blockCount);
  reiserFSHandle->blockSize      = LE32_TO_HOST(reiserSuperBlock.blockSize);
  reiserFSHandle->bitmapIndex    = -1;

  /* validate data */
  if (   !(reiserFSHandle->blockSize >= 512)
      || !((reiserFSHandle->blockSize % 512) == 0)
      || !(reiserFSHandle->totalBlocks > 0)
     )
  {
    return FALSE;
  }

  return TRUE;
}

LOCAL void REISERFS_done(DeviceHandle *deviceHandle, REISERFSHandle *reiserFSHandle)
{
  assert(deviceHandle != NULL);
  assert(reiserFSHandle != NULL);

  UNUSED_VARIABLE(deviceHandle);
  UNUSED_VARIABLE(reiserFSHandle);
}

LOCAL bool REISERFS_blockIsUsed(DeviceHandle *deviceHandle, REISERFSHandle *reiserFSHandle, uint64 offset)
{
  uint32 block;
  uint   bitmapIndex;
  uint32 bitmapBlock;
  uint   index;

  assert(deviceHandle != NULL);
  assert(reiserFSHandle != NULL);

  /* calculate block */
  block = offset/reiserFSHandle->blockSize;

  if (block >= 17)
  {
    /* calculate bitmap index */
    assert(reiserFSHandle->blockSize != 0);
    bitmapIndex = block/(reiserFSHandle->blockSize*8);

    /* read correct block bitmap if needed */
    if (reiserFSHandle->bitmapIndex != bitmapIndex)
    {
      bitmapBlock = ((bitmapIndex > 0)?(uint32)bitmapIndex*(uint32)reiserFSHandle->blockSize*8:REISERFS_SUPER_BLOCK_OFFSET/reiserFSHandle->blockSize+1)*(uint32)reiserFSHandle->blockSize;
      if (Device_seek(deviceHandle,REISERFS_BLOCK_TO_OFFSET(reiserFSHandle,bitmapBlock)) != ERROR_NONE)
      {
        return TRUE;
      }
      if (Device_read(deviceHandle,reiserFSHandle->bitmapData,reiserFSHandle->blockSize,NULL) != ERROR_NONE)
      {
        return TRUE;
      }
      reiserFSHandle->bitmapIndex = bitmapIndex;
    }

    /* check if block is used */
    index = block-bitmapIndex*reiserFSHandle->blockSize*8;
    return ((reiserFSHandle->bitmapData[index/8] & (1 << index%8)) != 0);
  }
  else
  {
    return TRUE;
  }
}

#ifdef __cplusplus
  }
#endif

/* end of file */
