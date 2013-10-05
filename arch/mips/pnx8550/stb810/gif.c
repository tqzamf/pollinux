/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2006 Koninklijke Philips Electronics N.V.
 * All rights reserved
 *
 */

#include <linux/stddef.h>
#include <linux/string.h>
/***********************************************************
 * Types and defines:
 **********************************************************/

#define PH_STB_OK 0
#define PH_STB_ERR_FILE_TRUNCATED -1
#define PH_STB_ERR_FILE_WRONG_FORMAT -2
#define PH_STB_ERR_NULL_PARAMETER -3

#define CLAMP_VALUE(value, high) if ((value)<0) (value)=0; if ((value)>(high)) (value)=(high)
#define DBG_PRINT(a)

/***********************************************************
 * Global data:
 **********************************************************/

/***********************************************************
 * Internal Functions:
 **********************************************************/

/******************* 
* LOCAL MACROS     *
********************/

#define PH_STB_GIF_INPUT_BUFFER_SIZE     (16*1024)

#define PH_STB_GIF_EXTENSION_HEADER_BYTE (0x21)
#define PH_STB_GIF_FULL_ALPHA            (0xFF000000)
#define PH_STB_GIF_TRANSPARENT_MASK      (0x00FFFFFF)
#define PH_STB_GIF_COLOUR_MAP_MASK       (0x80)
#define PH_STB_GIF_INTERLACED_MASK       (0x40)
#define PH_STB_GIF_COLOUR_DEPTH_MASK     (0x07)
#define PH_STB_GIF_TERMINATOR            (0x3B)
#define PH_STB_GIF_TRANSPARENCY_CODE     (0xF9)
#define PH_STB_GIF_TRANSPARENCY_MASK     (0x01)
#define PH_STB_GIF_UNDRAW_MASK           (0x1C)
#define PH_STB_GIF_RESTORE_BACKGROUND    (0x08)
#define PH_STB_GIF_CODE_SIZE_LIMIT       (12)
#define PH_STB_GIF_MAX_TABLE_SIZE        (4096)
#define PH_STB_GIF_COLOUR_MAP_SIZE       (256)
#define PH_STB_GIF_BIT_BUFFER_SIZE       (256)
#define PH_STB_GIF_SIGNATURE_LENGTH      (6)

#define PH_STB_GIF_GET_BYTE_VALUE(a) \
    phStbGif_getByteVariable = phStbGif_getByteValue(a); \
    if (phStbGif_getByteVariable==-1) { return PH_STB_ERR_FILE_TRUNCATED; }

#define PH_STB_GIF_CHECK(a) \
{ \
    int err; \
    err = a; \
    if (err != PH_STB_OK) \
    { \
        return err; \
    } \
}

/******************* 
* LOCAL TYPEDEFS   *
********************/

typedef struct _phStbGif_Input_t
{
    unsigned int    noOfBytesLeft;    /* No of bytes left in the Buffer */
    const unsigned char * pCurrentByte;     /* pointer to the current byte accessed from the buffer */
}phStbGif_Input_t, * pphStbGif_Input_t;

typedef struct
{
   int previous;
   int first;
   int output;
   int length;
} phStbGif_DataEntry_t;

typedef struct
{
   int width;
   int height;
   int backgroundColour;
   int aspectRatio;
   int colourDepth;
   int flags;
} phStbGif_GlobalProperties_t;

typedef struct
{
   int header;
   int positionX;
   int positionY;
   int width;
   int height;
   int colourDepth;
   int flags;
   int interlaced;
   int decodePass;
   int outputX;
   int outputY;
} phStbGif_LocalProperties_t;

/******************* 
* STATIC DATA      *
********************/

static phStbGif_Input_t gpphStbGifInput;

/* Variables to hold the global and local image properties */
static phStbGif_GlobalProperties_t phStbGif_globalProperties;
static phStbGif_LocalProperties_t  phStbGif_localProperties;

/* Variables used to obtain data bytes */
static unsigned char phStbGif_putByte;
static int  phStbGif_usePutByte = 0;
static int phStbGif_getByteVariable;

/* Variables used to obtain variable bit length data codes */
static unsigned char phStbGif_bitData[PH_STB_GIF_BIT_BUFFER_SIZE];
static int phStbGif_currentBit;
static int phStbGif_currentByte;
static int phStbGif_maxBytes = 0;
static int phStbGif_dataOK = 0;

/* Variables used to hold the colour map information */
static int phStbGif_globalColourMap[PH_STB_GIF_COLOUR_MAP_SIZE];
static int phStbGif_localColourMap[PH_STB_GIF_COLOUR_MAP_SIZE];
static int  phStbGif_hasTransparency;
static unsigned char phStbGif_transparentColour;
static int phStbGif_duration;
static int  phStbGif_doFillBackground;

/* Pointer to the decode buffer */
static unsigned int * phStbGif_dataBuffer = NULL;
static int phStbGif_dataBufferWidth;
static int phStbGif_dataBufferHeight;
static int phStbGif_dataBufferOffsetX;
static int phStbGif_dataBufferOffsetY;

/* Variables used to keep track of the decoded pixel sequences */
static phStbGif_DataEntry_t phStbGif_pixelLookup[PH_STB_GIF_MAX_TABLE_SIZE];
static int             phStbGif_thisCode;
static int             phStbGif_lastCode;
static int             phStbGif_codeSize;
static int             phStbGif_initialCodeSize;
static int             phStbGif_nextAvailable;
static int             phStbGif_pixelOutput[PH_STB_GIF_MAX_TABLE_SIZE];

static const unsigned char phStbGif_bitMask[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
static const unsigned char phStbGif_andMask[] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };
static const int phStbGif_shiftMask[] = { 0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080,
                                          0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000,
                                          0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000,
                                          0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000 };

/********************** 
* FUNCTION PROTOTYPES *
***********************/

/*******************************************************************************
*@begin
* NAME: phStbGif_readFunc
*       Replacement read function used during the GIF decoding
*       
* Returns : None
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/

static int phStbGif_readFunc(unsigned char* data, unsigned int length)
{
    if (gpphStbGifInput.noOfBytesLeft>=length)
    {
        /* Enough bytes in buffer to fill GIF buffer */
        memcpy(data, gpphStbGifInput.pCurrentByte, length);
        gpphStbGifInput.pCurrentByte  += length;
        gpphStbGifInput.noOfBytesLeft -= length;
    }
    else
    {
        /* Error while trying to get more data - give up quick! */
        return PH_STB_ERR_FILE_TRUNCATED;
    }
    return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_initLookup
*       Initialises the the pixel sequence lookup table
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* colourDepth   IN      Colour depth of image
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static void phStbGif_initLookUp(int colourDepth)
{
   int i;
   for(i=0; i<PH_STB_GIF_MAX_TABLE_SIZE; i++)
   {
      if (i<colourDepth)
      {
         phStbGif_pixelLookup[i].previous = -1;
         phStbGif_pixelLookup[i].first = i;
         phStbGif_pixelLookup[i].output = i;
         phStbGif_pixelLookup[i].length = 1;
      }
      else
      {
         phStbGif_pixelLookup[i].previous = -1;
         phStbGif_pixelLookup[i].first = -1;
         phStbGif_pixelLookup[i].output = -1;
         phStbGif_pixelLookup[i].length = -1;
      }
   }
}

/*******************************************************************************
*@begin
* NAME: phStbGif_convertLocalToGlobalPosition
*       Converts a local pixel position to a global pixel position
*       
* Returns : Pointer to the pixel location within the output buffer
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static unsigned int *phStbGif_convertLocalToGlobalPosition(void)
{
    int globalX;
    int globalY;

    globalX = phStbGif_localProperties.positionX+phStbGif_localProperties.outputX+phStbGif_dataBufferOffsetX;
    globalY = phStbGif_localProperties.positionY+phStbGif_localProperties.outputY+phStbGif_dataBufferOffsetY;

    /* Clip */
    CLAMP_VALUE(globalX, (phStbGif_dataBufferWidth-1));
    CLAMP_VALUE(globalY, (phStbGif_dataBufferHeight-1));
    return phStbGif_dataBuffer+(globalY*phStbGif_dataBufferWidth)+globalX;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_FillBackground
*       Local function used to flag pixels that should be set to the background colour
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* pColourMap    IN      Pointer to colour map (Palette)
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/

static void phStbGif_fillBackground(int * pColourMap)
{
    int i;
    int j;
    for(i=0; i<phStbGif_globalProperties.height; i++)
    {
        unsigned int *ptr = phStbGif_dataBuffer + ((i+phStbGif_dataBufferOffsetY)*phStbGif_dataBufferWidth)+phStbGif_dataBufferOffsetX;
        for(j=0; j<phStbGif_globalProperties.width; j++)
        {
            /* Change the alpha value (slightly) for the pixel */
            *ptr &=0xFEFFFFFF;
            ptr++;
        }
    }
}

/*******************************************************************************
*@begin
* NAME: phStbGif_FillBackgroundFinish
*       Local function used to fill the image with the background colour
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* pColourMap    IN      Pointer to colour map (Palette)
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static void phStbGif_fillBackgroundFinish(int * pColourMap)
{
    int i;
    int j;
    for(i=0; i<phStbGif_globalProperties.height; i++)
    {
        unsigned int *ptr = phStbGif_dataBuffer + ((i+phStbGif_dataBufferOffsetY)*phStbGif_dataBufferWidth)+phStbGif_dataBufferOffsetX;
        for(j=0; j<phStbGif_globalProperties.width; j++)
        {
            if ((*ptr & 0x01000000) == 0)
            {
                *ptr = pColourMap[phStbGif_globalProperties.backgroundColour];
            }
            ptr++;
        }
    }
}

/*******************************************************************************
*@begin
* NAME: phStbGif_outputPixel
*       Writes a single pixel value to the output buffer
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* pColourMap    IN      Pointer to colour map (Palette)
* which         IN      Index into the colour map
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static inline void phStbGif_outputPixel(int * pColourMap, int which)
{
    unsigned int *pOutput;

    pOutput = phStbGif_convertLocalToGlobalPosition();

    if (pColourMap[which]&PH_STB_GIF_FULL_ALPHA)
    {
        *pOutput = pColourMap[which];
    }
    phStbGif_localProperties.outputX++;
    if (phStbGif_localProperties.outputX >= phStbGif_localProperties.width)
    {
        phStbGif_localProperties.outputX = 0;
        /* Check for interlaced GIF images */
        if (phStbGif_localProperties.interlaced)
        {
            switch(phStbGif_localProperties.decodePass)
            {
                case(2) : phStbGif_localProperties.outputY += 4;
                          break;
                case(3) : phStbGif_localProperties.outputY += 2;
                          break;
                default : phStbGif_localProperties.outputY += 8;
                          break;
            }
            if (phStbGif_localProperties.outputY >= phStbGif_localProperties.height)
            {
                phStbGif_localProperties.decodePass++;
                switch(phStbGif_localProperties.decodePass)
                {
                    case(1) : phStbGif_localProperties.outputY = 4;
                              break;
                    case(2) : phStbGif_localProperties.outputY = 2;
                              break;
                    case(3) : phStbGif_localProperties.outputY = 1;
                              break;
                    default : phStbGif_localProperties.outputY = 0;
                              break;
                }
            }
        }
        else
        {
            phStbGif_localProperties.outputY++;
        }
    }
}

/*******************************************************************************
*@begin
* NAME: phStbGif_addEntry
*       Adds a pixel sequence entry to the lookup table
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* which         IN      ID of the new entry to be created
* sourceEntry   IN      ID of the entry to be used as the basis for the new entry
* append        IN      pixel value to be appended to the start of the source entry
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static inline void phStbGif_addEntry(int which, int sourceEntry, int append)
{
   phStbGif_pixelLookup[which].previous = sourceEntry;
   phStbGif_pixelLookup[which].first = phStbGif_pixelLookup[sourceEntry].first;
   phStbGif_pixelLookup[which].output = phStbGif_pixelLookup[phStbGif_pixelLookup[append].first].output;
   phStbGif_pixelLookup[which].length = phStbGif_pixelLookup[sourceEntry].length+1;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_getByteValue
*       Gets multiple bytes of data from the input stream
*       
* Returns : Data bytes shifted into a single 32 bit value
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* bytes         IN      Number of data bytes to be read
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_getByteValue(int bytes)
{
   int i;
   unsigned int value = 0;
   unsigned char buffer[1];

   for(i=0; i<bytes; i++)
   {
      /* Check to see if there is a returned byte to be used */
      if (phStbGif_usePutByte)
      {
          buffer[0] = phStbGif_putByte;
          phStbGif_usePutByte = 0;
      }
      else
      {
          if (phStbGif_readFunc(buffer, 1)!=PH_STB_OK)
          {
              return -1;
          }
      }
      value |= ((int)buffer[0]<<(i*8));
   }
   return value;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_putByteValue
*       Returns one byte of data to the read buffer
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* byte          IN      Data byte to be returned to the buffer
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static void phStbGif_putByteValue(unsigned char byte)
{
   phStbGif_putByte = byte;
   phStbGif_usePutByte = 1;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_getMoreData
*       Refills the bit code data buffer with the next section of GIF data
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* pLength   OUT Number of bytes read in
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_getMoreData(unsigned int * pLength)
{
   *pLength = PH_STB_GIF_GET_BYTE_VALUE(1);
   if (*pLength!=0)
   {
      int i;
      for(i=0; i<*pLength; i++)
      {
         phStbGif_bitData[i] = PH_STB_GIF_GET_BYTE_VALUE(1);
      }
      phStbGif_maxBytes = *pLength;
      phStbGif_currentByte = 0;
      phStbGif_dataOK = 1;
   }
   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_getBitValue
*       Returns the next compressed code
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* bits          IN      Number of bit of data in the code 
* pValue    OUT output value
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_getBitValue(int bits, int * pValue)
{
   int i;
   *pValue = 0;
   
   for(i=0; i<bits; i++)
   {
      int remainingBits = bits - i;
      if (phStbGif_currentBit==0)
      {
          phStbGif_currentByte++;
          if (phStbGif_currentByte == phStbGif_maxBytes)
          {
              unsigned int length;
              PH_STB_GIF_CHECK(phStbGif_getMoreData(&length));
              if (length==0)
              {
                  phStbGif_dataOK = 0;
                  return PH_STB_OK;
              }
          }
      }
      if (phStbGif_currentBit == 0)
      {
          if (remainingBits <= 8)
          {
              *pValue |= (phStbGif_bitData[phStbGif_currentByte] & phStbGif_andMask[remainingBits]) << i;
              phStbGif_currentBit = remainingBits%8;
              return PH_STB_OK;
          }
          else
          {
              *pValue |= phStbGif_bitData[phStbGif_currentByte] << i;
              i+=7;
          }
      }
      else
      {
          if (phStbGif_bitData[phStbGif_currentByte]&phStbGif_bitMask[phStbGif_currentBit])
          {
             *pValue |= phStbGif_shiftMask[i];
          }
          phStbGif_currentBit++;
      }
      phStbGif_currentBit %= 8;
   }
   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_decodeSequence
*       Goes through the lookup table to produce a sequence of pixel codes
*       
* Returns : pointer to the start of the list of pixel codes
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* code          IN      Code to decode the pixel sequence for
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static inline int * phStbGif_decodeSequence(int code)
{
   int i;
   int which = code;
   for(i=0; i<phStbGif_pixelLookup[code].length; i++)
   {
       phStbGif_pixelOutput[phStbGif_pixelLookup[code].length-i-1] = phStbGif_pixelLookup[which].output;
       which = phStbGif_pixelLookup[which].previous;
   }
   return phStbGif_pixelOutput;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_resetCodeSize
*       Resets the decoder back to the initial settings
*       
* Returns : void
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* colourDepth   IN      Colour depth of image
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static void phStbGif_resetCodeSize(int colourDepth)
{
    phStbGif_codeSize = phStbGif_initialCodeSize;
    phStbGif_nextAvailable = phStbGif_shiftMask[phStbGif_initialCodeSize]+2;
    phStbGif_initLookUp(colourDepth);
    phStbGif_thisCode = -1;
    phStbGif_lastCode = -1;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_decodeRasterBlock
*       Decodes a block of pixel information
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* colourDepth   IN      Colour depth of image
* pColourMap    IN      Pointer to colour map (Palette)
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_decodeRasterBlock(int colourDepth, int * pColourMap)
{
    while(1)
    {
        /* Read in the next compressed data code */
        PH_STB_GIF_CHECK(phStbGif_getBitValue(phStbGif_codeSize+1, &phStbGif_thisCode));

        /* Check that there was enough data to continue */
        if (phStbGif_dataOK)
        {

            /* Check to see if the code is the 'ClearCode' */
            if (phStbGif_thisCode == phStbGif_shiftMask[phStbGif_initialCodeSize])
            {
                /* Reset the lookup table to its original values */
                phStbGif_resetCodeSize(colourDepth);
            }
            else
            /* Check to see if the code is a 'FinishCode' */
            if (phStbGif_thisCode == phStbGif_shiftMask[phStbGif_initialCodeSize]+1)
            {
                /* Finish this block of pixels */
                return PH_STB_OK;
            }
            else
            {
                /* Check to see if this is the first code that we have read in */
                if (phStbGif_lastCode==-1)
                {
                    /* Output the pixel associated with this code */
                    phStbGif_outputPixel(pColourMap, phStbGif_thisCode);
                }
                else
                {
                    int * sequence;
                    int i;
                    /* Check to see if the code is in the data dictionary */
                    if (phStbGif_thisCode>=phStbGif_nextAvailable)
                    {
                        /* No it is not! - add an entry using the data associated with  */
                        /* the last code and append the first pixel of the last code to */
                        /* make a new entry                                             */
                        phStbGif_addEntry(phStbGif_nextAvailable, phStbGif_lastCode, phStbGif_pixelLookup[phStbGif_lastCode].first);
                    }
                    else
                    {
                        /* Yes it is! - add an entry using the data associated with    */
                        /* the last code and append the first pixel of the new code to */
                        /* make a new entry                                            */
                        phStbGif_addEntry(phStbGif_nextAvailable, phStbGif_lastCode, phStbGif_pixelLookup[phStbGif_thisCode].first);
                    }
                    /* As the pixels are added to the look up table in reverse order - */
                    /* unwind the sequence of pixels to allow them to be written out.  */
                    sequence = phStbGif_decodeSequence(phStbGif_thisCode);
                    for(i=0; i<phStbGif_pixelLookup[phStbGif_thisCode].length; i++)
                    {
                        /* Output the pixel associated with this code */
                        phStbGif_outputPixel(pColourMap, sequence[i]);
                    }
                    /* Increment the number of used data entries */
                    phStbGif_nextAvailable++;
                    /* Check to see if we need to increase the number of bits in the codes */
                    if (phStbGif_nextAvailable>=phStbGif_shiftMask[phStbGif_codeSize+1])
                    {
                       phStbGif_codeSize++;
                       /* Check to see if we have reached the code size limit */
                       if (phStbGif_codeSize==PH_STB_GIF_CODE_SIZE_LIMIT)
                       {
                          /* Read the 12 bit clear code */
                          PH_STB_GIF_CHECK(phStbGif_getBitValue(phStbGif_codeSize, &phStbGif_thisCode));
                          /* Reset the lookup table to its original values */
                          phStbGif_resetCodeSize(colourDepth);
                       }
                    }
                }
                /* Save the current code */
                phStbGif_lastCode=phStbGif_thisCode;
            }
        }
        else
        {
            /* Run out of data - get out quick */
            return PH_STB_OK;
        }
    }
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readSignature
*       Reads in the file signature information
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readSignature(void)
{
   int i;
   unsigned char signature[PH_STB_GIF_SIGNATURE_LENGTH];

   for(i=0; i<PH_STB_GIF_SIGNATURE_LENGTH; i++)
   {
      signature[i] = PH_STB_GIF_GET_BYTE_VALUE(1);
   }
   if ((memcmp("GIF87a", signature, PH_STB_GIF_SIGNATURE_LENGTH) == 0) ||
       (memcmp("GIF89a", signature, PH_STB_GIF_SIGNATURE_LENGTH) == 0))
   {
       return PH_STB_OK;
   }
   return PH_STB_ERR_FILE_WRONG_FORMAT;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readExtensionBlocks
*       Reads in extension block fields
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readExtensionBlocks(void)
{
   unsigned char header;
   unsigned char data[256];
   unsigned char functionCode;
   unsigned char length;

   while(1)
   {
      int i;

      header = PH_STB_GIF_GET_BYTE_VALUE(1);
      /* Check to see if an extension block header byte has been read */
      if (header!=PH_STB_GIF_EXTENSION_HEADER_BYTE)
      {
         phStbGif_putByteValue(header);
         return PH_STB_OK;
      }

      /* Read the function code information and associated data */
      functionCode = PH_STB_GIF_GET_BYTE_VALUE(1);
      do
      {
          length = PH_STB_GIF_GET_BYTE_VALUE(1);
          for(i=0; i<length; i++)
          {
             data[i] = PH_STB_GIF_GET_BYTE_VALUE(1);
          }

          /* Check to see if the function code corresponds to transparency */
          if (functionCode == PH_STB_GIF_TRANSPARENCY_CODE)
          {
              if (length == 4) 
              {
                  if (data[0]&PH_STB_GIF_TRANSPARENCY_MASK)
                  {
                      phStbGif_hasTransparency = 1;
                      phStbGif_transparentColour = data[3];
                  }
                  phStbGif_duration = (data[2]<<8)+data[1];
                  phStbGif_doFillBackground = ((data[0]&PH_STB_GIF_UNDRAW_MASK)==PH_STB_GIF_RESTORE_BACKGROUND);
              }
          }
      }while(length);
      /* For Comments associated with the image check for code 254 here */
   } 
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readGlobalDescriptor
*       Reads in a global descriptor field
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readGlobalDescriptor(void)
{
   phStbGif_globalProperties.width = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_globalProperties.height = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_globalProperties.flags = PH_STB_GIF_GET_BYTE_VALUE(1);
   phStbGif_globalProperties.colourDepth = phStbGif_globalProperties.flags&PH_STB_GIF_COLOUR_DEPTH_MASK;
   phStbGif_globalProperties.backgroundColour = PH_STB_GIF_GET_BYTE_VALUE(1);
   phStbGif_globalProperties.aspectRatio = PH_STB_GIF_GET_BYTE_VALUE(1);
   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readLocalDescriptor
*       Reads in a local descriptor field
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readLocalDescriptor(void)
{
   phStbGif_localProperties.header = PH_STB_GIF_GET_BYTE_VALUE(1);
   if (phStbGif_localProperties.header == PH_STB_GIF_TERMINATOR)
   {
      phStbGif_putByteValue(phStbGif_localProperties.header);
      return PH_STB_OK;
   }
   phStbGif_localProperties.positionX = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_localProperties.positionY = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_localProperties.width = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_localProperties.height = PH_STB_GIF_GET_BYTE_VALUE(2);
   phStbGif_localProperties.flags = PH_STB_GIF_GET_BYTE_VALUE(1);
   phStbGif_localProperties.colourDepth = phStbGif_localProperties.flags&PH_STB_GIF_COLOUR_DEPTH_MASK;
   phStbGif_localProperties.interlaced = phStbGif_localProperties.flags&PH_STB_GIF_INTERLACED_MASK;
   phStbGif_localProperties.outputX = 0;
   phStbGif_localProperties.outputY = 0;
   phStbGif_localProperties.decodePass = 0;
   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readColourMap
*       Reads in a colour map (Palette)
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* numColours    IN      Number of colours in the colour map (palette)
* pColourMap    IN    Pointer to a storage location for the colour map
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readColourMap(int numColours, int * pColourMap)
{
   int i;
   for(i=0; i<numColours; i++)
   {
      int red;
      int green;
      int blue;
      red   = PH_STB_GIF_GET_BYTE_VALUE(1);
      green = PH_STB_GIF_GET_BYTE_VALUE(1);
      blue  = PH_STB_GIF_GET_BYTE_VALUE(1);

      pColourMap[i] = PH_STB_GIF_FULL_ALPHA|(red<<16)|(green<<8)| blue;
   }
   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_readRasterDataBlock
*       Decodes a block of raster (pixel) data
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* colourDepth   IN      Colour depth of image
* pColourMap    IN      Pointer to colour map (Palette)
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
static int phStbGif_readRasterDataBlock(int colourDepth, int * pColourMap)
{
   int length;
   unsigned int maxEntries;
   /* In the specification it states that black and white images (which have a colour depth of 1) */
   /* should be treated as images with a colour depth of 2 !!!                                    */
   if (colourDepth==1)
   {
       colourDepth=2;
   }
   /* Read in the initial code size */
   phStbGif_codeSize = PH_STB_GIF_GET_BYTE_VALUE(1);
   if (phStbGif_codeSize == PH_STB_GIF_TERMINATOR)
   {
      phStbGif_putByteValue(phStbGif_codeSize);
      return PH_STB_OK;
   }
   /* In the specification it states that the code size information should equal the colour depth */
   /* ... so in the case that an invalid code size is read in use the colour depth.               */
   if (phStbGif_codeSize>=PH_STB_GIF_CODE_SIZE_LIMIT)
   {
       phStbGif_codeSize = colourDepth;
   }
   phStbGif_initialCodeSize = phStbGif_codeSize;
   maxEntries = phStbGif_shiftMask[colourDepth+1];

   /* Calculate the next available data entry */
   phStbGif_nextAvailable = phStbGif_shiftMask[phStbGif_codeSize]+2;

   /* Initialise the data entry look up table */
   phStbGif_initLookUp(maxEntries);

   do
   {
      phStbGif_currentBit = 0;
      phStbGif_currentByte = 0;
      phStbGif_maxBytes = 1;
      /* Decode a block of pixels */
      PH_STB_GIF_CHECK(phStbGif_decodeRasterBlock(maxEntries, pColourMap));

      /* Read the finishing code */
      length = PH_STB_GIF_GET_BYTE_VALUE(1);
      if (length!=0)
      {
         phStbGif_putByteValue(length);
      }
   }while(length!=0);

   return PH_STB_OK;
}

/*******************************************************************************
*@begin
* NAME: phStbGif_Decode
*       Decodes a bitmap supplied in GIF format.
*       
* Returns : PH_STB_OK if successful - else error
* 
* Parameter     Flow    Description
* ------------------------------------------------------------------------------
* filename      IN      Image filename
* pSetup        IN      Pointer to set up information - holding decode 
*                       information
* 
* Externals     Flow    Usage
* ------------------------------------------------------------------------------
* 
* Additional information:
*
*@end
*******************************************************************************/
int phStbGif_Decode( const unsigned char* data, int length, unsigned int* pOutput, int width, int height, int x, int y)
{
    static unsigned char* currentData = 0;

    if (currentData != data) 
    {
        currentData = (unsigned char*)data;
        /* Take local copies of important variables */
        phStbGif_dataBufferWidth = width;
        phStbGif_dataBufferHeight = height;

        /* Check to see if a valid decode buffer has been provided */
        if (pOutput == NULL)
        {
            return PH_STB_ERR_NULL_PARAMETER;
        }

        gpphStbGifInput.noOfBytesLeft = length;
        gpphStbGifInput.pCurrentByte  = data;
        phStbGif_dataBuffer = pOutput;

        PH_STB_GIF_CHECK(phStbGif_readSignature());
        PH_STB_GIF_CHECK(phStbGif_readGlobalDescriptor());
        if (phStbGif_globalProperties.flags&PH_STB_GIF_COLOUR_MAP_MASK)
        {
            PH_STB_GIF_CHECK(phStbGif_readColourMap(phStbGif_shiftMask[phStbGif_globalProperties.colourDepth+1], phStbGif_globalColourMap));
        }
        phStbGif_dataBufferOffsetX = x - (phStbGif_globalProperties.width/2);
        phStbGif_dataBufferOffsetY = y - (phStbGif_globalProperties.height/2);
    }

    {
        int moreToProcess;
        int terminator;
        int colourDepth;
        int * pColourMap;
        int status;
        phStbGif_hasTransparency = 0;
        phStbGif_doFillBackground = 0;

        if (phStbGif_readExtensionBlocks() != PH_STB_OK)
        {
            DBG_PRINT((dbgGif,DBG_DEFAULT,"phStbGif_Decode: Error reading extension block\n"));
            goto clean_up_and_exit;
        }

        if (phStbGif_readLocalDescriptor() != PH_STB_OK)
        {
            DBG_PRINT((dbgGif,DBG_DEFAULT,"phStbGif_Decode: Error reading local descriptor\n"));
            goto clean_up_and_exit;
        }

        if (phStbGif_localProperties.flags&PH_STB_GIF_COLOUR_MAP_MASK)
        {
            if (phStbGif_readColourMap(phStbGif_shiftMask[phStbGif_localProperties.colourDepth+1], phStbGif_localColourMap) != PH_STB_OK)
            {
                DBG_PRINT((dbgGif,DBG_DEFAULT,"phStbGif_Decode: Error reading local colour map\n"));
                goto clean_up_and_exit;
            }
            colourDepth = phStbGif_localProperties.colourDepth;
            pColourMap = phStbGif_localColourMap;
        }
        else
        {
            pColourMap = phStbGif_globalColourMap;
            colourDepth = phStbGif_globalProperties.colourDepth;
        }
        if (phStbGif_hasTransparency)
        {
            pColourMap[phStbGif_transparentColour] &= PH_STB_GIF_TRANSPARENT_MASK;
        }

        if (phStbGif_doFillBackground)
        {
            phStbGif_fillBackground(pColourMap);
        }

        status = phStbGif_readRasterDataBlock(colourDepth, pColourMap);

        if (phStbGif_doFillBackground)
        {
            phStbGif_fillBackgroundFinish(pColourMap);
        }

        if (status != PH_STB_OK)
        {
            DBG_PRINT((dbgGif,DBG_DEFAULT,"phStbGif_Decode: Error reading raster data block\n"));
            goto clean_up_and_exit;
        }

        terminator = phStbGif_getByteValue(1);
        if (terminator == -1)
        {
            DBG_PRINT((dbgGif,DBG_DEFAULT,"phStbGif_Decode: Error reading terminator\n"));
            currentData = 0;
            goto clean_up_and_exit;
        }

        moreToProcess = (terminator!=PH_STB_GIF_TERMINATOR);

        if (moreToProcess)
        {
            phStbGif_putByteValue(terminator);
        }
        else
        {
            currentData = 0;
        }
    }

clean_up_and_exit :

    return PH_STB_OK;
}


