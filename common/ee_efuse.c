/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2004, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

	Module Name:
	ee_efuse.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#ifdef RTMP_EFUSE_SUPPORT

#include	"rt_config.h"

/* eFuse registers */
#define EFUSE_CTRL				0x0580
#define EFUSE_DATA0				0x0590
#define EFUSE_DATA1				0x0594
#define EFUSE_DATA2				0x0598
#define EFUSE_DATA3				0x059c

#define EFUSE_CTRL_3290			0x24
#define EFUSE_DATA0_3290		0x28
#define EFUSE_DATA1_3290		0x2c
#define EFUSE_DATA2_3290		0x30
#define EFUSE_DATA3_3290		0x34

#define EFUSE_TAG				0x2fe

#ifdef RT_BIG_ENDIAN
typedef	union	_EFUSE_CTRL_STRUC {
	struct	{
		uint32_t            SEL_EFUSE:1;
		uint32_t            EFSROM_KICK:1;
		uint32_t            RESERVED:4;
		uint32_t            EFSROM_AIN:10;
		uint32_t            EFSROM_LDO_ON_TIME:2;
		uint32_t            EFSROM_LDO_OFF_TIME:6;
		uint32_t            EFSROM_MODE:2;
		uint32_t            EFSROM_AOUT:6;
	}	field;
	uint32_t 		word;
}	EFUSE_CTRL_STRUC, *PEFUSE_CTRL_STRUC;
#else
typedef	union	_EFUSE_CTRL_STRUC {
	struct	{
		uint32_t            EFSROM_AOUT:6;
		uint32_t            EFSROM_MODE:2;
		uint32_t            EFSROM_LDO_OFF_TIME:6;
		uint32_t            EFSROM_LDO_ON_TIME:2;
		uint32_t            EFSROM_AIN:10;
		uint32_t            RESERVED:4;
		uint32_t            EFSROM_KICK:1;
		uint32_t            SEL_EFUSE:1;
	}	field;
	uint32_t 		word;
}	EFUSE_CTRL_STRUC, *PEFUSE_CTRL_STRUC;
#endif /* RT_BIG_ENDIAN */

VOID eFuseReadPhysical(
	IN	struct rtmp_adapter *pAd,
  	IN	PUSHORT lpInBuffer,
  	IN	ULONG nInBufferSize,
  	OUT	PUSHORT lpOutBuffer,
  	IN	ULONG nOutBufferSize);

static VOID eFusePhysicalWriteRegisters(
	IN	struct rtmp_adapter *pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData);

static VOID eFuseWritePhysical(
	IN	struct rtmp_adapter *pAd,
  	PUSHORT lpInBuffer,
	ULONG nInBufferSize,
  	u8 *lpOutBuffer,
  	ULONG nOutBufferSize);

static NTSTATUS eFuseWriteRegistersFromBin(
	IN	struct rtmp_adapter *pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData);


/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
UCHAR eFuseReadRegisters(
	IN	struct rtmp_adapter *pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset;
	uint32_t data;
	uint32_t efuse_ctrl_reg = EFUSE_CTRL;

#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
	if (IS_RT65XX(pAd) || IS_MT7601(pAd))
		efuse_ctrl_reg = EFUSE_CTRL_3290;
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */

	RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);

	/*Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.*/
	/*Use the eeprom logical address and covert to address to block number*/
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 0.*/
	eFuseCtrlStruc.field.EFSROM_MODE = 0;

	/*Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.*/
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	memmove(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, efuse_ctrl_reg, data);

	/*Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.*/
	i = 0;
	while(i < 500)
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
			return 0;

		/*rtmp.HwMemoryReadDword(EFUSE_CTRL, (DWORD *) &eFuseCtrlStruc, 4);*/
		RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);
		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
		{
			RtmpusecDelay(2);
			break;
		}
		RtmpusecDelay(2);
		i++;
	}

	/*if EFSROM_AOUT is not found in physical address, write 0xffff*/
	if (eFuseCtrlStruc.field.EFSROM_AOUT == 0x3f)
	{
		for(i=0; i<Length/2; i++)
			*(pData+2*i) = 0xffff;
	}
	else
	{
		/*Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x590-0x59C)*/
#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
		if (IS_RT65XX(pAd) || IS_MT7601(pAd))
			efuseDataOffset =  EFUSE_DATA0_3290 + (Offset & 0xC);
		else
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */
		efuseDataOffset =  EFUSE_DATA3 - (Offset & 0xC);
		/*data hold 4 bytes data.*/
		/*In RTMP_IO_READ32 will automatically execute 32-bytes swapping*/
		RTMP_IO_READ32(pAd, efuseDataOffset, &data);
		/*Decide the upper 2 bytes or the bottom 2 bytes.*/
		/* Little-endian		S	|	S	Big-endian*/
		/* addr	3	2	1	0	|	0	1	2	3*/
		/* Ori-V	D	C	B	A	|	A	B	C	D*/
		/*After swapping*/
		/*		D	C	B	A	|	D	C	B	A*/
		/*Return 2-bytes*/
		/*The return byte statrs from S. Therefore, the little-endian will return BA, the Big-endian will return DC.*/
		/*For returning the bottom 2 bytes, the Big-endian should shift right 2-bytes.*/
#ifdef RT_BIG_ENDIAN
		data = data << (8*((Offset & 0x3)^0x2));
#else
		data = data >> (8*(Offset & 0x3));
#endif /* RT_BIG_ENDIAN */

		memmove(pData, &data, Length);
	}

	return (UCHAR) eFuseCtrlStruc.field.EFSROM_AOUT;

}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
VOID eFusePhysicalReadRegisters(
	IN	struct rtmp_adapter *pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset;
	uint32_t data;
	uint32_t efuse_ctrl_reg = EFUSE_CTRL;

#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
	if (IS_RT65XX(pAd) || IS_MT7601(pAd))
		efuse_ctrl_reg = EFUSE_CTRL_3290;
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */

	RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);

	/*Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.*/
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 1.*/
	/*Read in physical view*/
	eFuseCtrlStruc.field.EFSROM_MODE = 1;

	/*Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.*/
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	memmove(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, efuse_ctrl_reg, data);

	/*Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.*/
	i = 0;
	while(i < 500)
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
			return;

		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
		{
			RtmpusecDelay(2);
			break;
		}

		RtmpusecDelay(2);
		i++;
	}

	/*Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590)*/
	/*Because the size of each EFUSE_DATA is 4 Bytes, the size of address of each is 2 bits.*/
	/*The previous 2 bits is the EFUSE_DATA number, the last 2 bits is used to decide which bytes*/
	/*Decide which EFUSE_DATA to read*/
	/*590:F E D C */
	/*594:B A 9 8 */
	/*598:7 6 5 4*/
	/*59C:3 2 1 0*/
#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
	if (IS_RT65XX(pAd) || IS_MT7601(pAd))
		efuseDataOffset =  EFUSE_DATA0_3290 + (Offset & 0xC)  ;
	else
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */
	efuseDataOffset =  EFUSE_DATA3 - (Offset & 0xC)  ;

	RTMP_IO_READ32(pAd, efuseDataOffset, &data);

#ifdef RT_BIG_ENDIAN
		data = data << (8*((Offset & 0x3)^0x2));
#else
	data = data >> (8*(Offset & 0x3));
#endif /* RT_BIG_ENDIAN */

	memmove(pData, &data, Length);

}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
VOID eFuseReadPhysical(
	IN	struct rtmp_adapter *pAd,
  	IN	PUSHORT lpInBuffer,
  	IN	ULONG nInBufferSize,
  	OUT	PUSHORT lpOutBuffer,
  	IN	ULONG nOutBufferSize
)
{
	USHORT* pInBuf = (USHORT*)lpInBuffer;
	USHORT* pOutBuf = (USHORT*)lpOutBuffer;

	USHORT Offset = pInBuf[0];					/*addr*/
	USHORT Length = pInBuf[1];					/*length*/
	int 		i;

	for(i=0; i<Length; i+=2)
	{
		eFusePhysicalReadRegisters(pAd,Offset+i, 2, &pOutBuf[i/2]);
	}
}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
static VOID eFusePhysicalWriteRegisters(
	IN	struct rtmp_adapter *pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset, efuse_data = EFUSE_DATA3;
	uint32_t data, eFuseDataBuffer[4];
	uint32_t efuse_ctrl_reg = EFUSE_CTRL;

#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
	if (IS_RT65XX(pAd) || IS_MT7601(pAd)) {
		efuse_ctrl_reg = EFUSE_CTRL_3290;
		efuse_data = EFUSE_DATA0_3290;
	}
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */

	/*Step0. Write 16-byte of data to EFUSE_DATA0-3 (0x590-0x59C), where EFUSE_DATA0 is the LSB DW, EFUSE_DATA3 is the MSB DW.*/

	/*read current values of 16-byte block	*/
	RTMP_IO_READ32(pAd, efuse_ctrl_reg,  &eFuseCtrlStruc.word);

	/*Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.*/
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 1.*/
	eFuseCtrlStruc.field.EFSROM_MODE = 1;

	/*Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.*/
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	memmove(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, efuse_ctrl_reg, data);

	/*Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.*/
	i = 0;
	while(i < 500)
	{
		RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);

		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
		{
			RtmpusecDelay(2);
			break;
		}
		RtmpusecDelay(2);
		i++;
	}

	/*Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590)*/
	efuseDataOffset =  efuse_data;
	for(i=0; i< 4; i++)
	{
		RTMP_IO_READ32(pAd, efuseDataOffset, (uint32_t *) &eFuseDataBuffer[i]);
#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
		if (IS_RT65XX(pAd) || IS_MT7601(pAd))
			efuseDataOffset += 4;
		else
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */
			efuseDataOffset -= 4;
	}

	/*Update the value, the offset is multiple of 2, length is 2*/
	efuseDataOffset = (Offset & 0xc) >> 2;
	data = pData[0] & 0xffff;
	/*The offset should be 0x***10 or 0x***00*/
	if((Offset % 4) != 0)
	{
		eFuseDataBuffer[efuseDataOffset] = (eFuseDataBuffer[efuseDataOffset] & 0xffff) | (data << 16);
	}
	else
	{
		eFuseDataBuffer[efuseDataOffset] = (eFuseDataBuffer[efuseDataOffset] & 0xffff0000) | data;
	}

	efuseDataOffset =  efuse_data;
	for(i=0; i< 4; i++)
	{
		RTMP_IO_WRITE32(pAd, efuseDataOffset, eFuseDataBuffer[i]);
#if defined(RT3290) || defined(RT65xx) || defined(MT7601)
		if (IS_RT65XX(pAd) || IS_MT7601(pAd))
			efuseDataOffset += 4;
		else
#endif /* defined(RT3290) || defined(RT65xx) || defined(MT7601) */
			efuseDataOffset -= 4;
	}

	/*Step1. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.*/
	// TODO: shiang, for below line, windows driver didn't have this read, why we have ??
	RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);

	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step2. Write EFSROM_MODE (0x580, bit7:bit6) to 3.*/
	eFuseCtrlStruc.field.EFSROM_MODE = 3;

	/*Step3. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical write procedure.*/
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	memmove(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, efuse_ctrl_reg, data);

	/*Step4. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. It��s done.*/
	i = 0;

	while (i < 500)
	{
		RTMP_IO_READ32(pAd, efuse_ctrl_reg, &eFuseCtrlStruc.word);

		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
		{
			RtmpusecDelay(2);
			break;
		}

		RtmpusecDelay(2);
		i++;
	}
}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
static VOID eFuseWritePhysical(
	IN	struct rtmp_adapter *pAd,
  	PUSHORT lpInBuffer,
	ULONG nInBufferSize,
  	u8 *lpOutBuffer,
  	ULONG nOutBufferSize
)
{
	USHORT* pInBuf = (USHORT*)lpInBuffer;
	int 		i;
	/*USHORT* pOutBuf = (USHORT*)ioBuffer;*/
	USHORT Offset = pInBuf[0];					/* addr*/
	USHORT Length = pInBuf[1];					/* length*/
	USHORT* pValueX = &pInBuf[2];				/* value ...		*/

#ifdef MT76x2
	uint32_t MacReg0x14c = 0, MacReg0x24 = 0;
#endif /* MT76x2 */

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWritePhysical Offset=0x%x, length=%d\n", Offset, Length));

	{

#ifdef MT76x2
	if ( IS_MT76x2(pAd) )
	{
		uint32_t MacValue;

		RTMP_IO_READ32(pAd, CLK_ENABLE,  &MacValue);
		MacReg0x14c = MacValue;
		MacValue |= 0x2000000;
		RTMP_IO_WRITE32(pAd, CLK_ENABLE, MacValue);

		RTMP_IO_READ32(pAd, 0x24,  &MacValue);
		MacReg0x24 = MacValue;
		MacValue |= 0xC000;
		MacValue &= ~0x3F00;
		MacValue |= 0x800;
		RTMP_IO_WRITE32(pAd, 0x24, MacValue);
	}
#endif /* MT76x2 */

		/* Little-endian		S	|	S	Big-endian*/
		/* addr	3	2	1	0	|	0	1	2	3*/
		/* Ori-V	D	C	B	A	|	A	B	C	D*/
		/* After swapping*/
		/*		D	C	B	A	|	D	C	B	A*/
		/* Both the little and big-endian use the same sequence to write  data.*/
		/* Therefore, we only need swap data when read the data.*/
		for (i=0; i<Length; i+=2)
		{
			eFusePhysicalWriteRegisters(pAd, Offset+i, 2, &pValueX[i/2]);
		}

#ifdef MT76x2
		if ( IS_MT76x2(pAd) )
		{
			RTMP_IO_WRITE32(pAd, CLK_ENABLE, MacReg0x14c);
			RTMP_IO_WRITE32(pAd, 0x24, MacReg0x24);
		}
#endif /* MT76x2 */

	}
}


/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
INT set_eFuseGetFreeBlockCount_Proc(
   	IN	struct rtmp_adapter *pAd,
	IN	char *		arg)
{
	UINT free_num = 0;

	if (pAd->bUseEfuse == FALSE)
		return FALSE;

	eFuseGetFreeBlockCount(pAd, &free_num);
	DBGPRINT(RT_DEBUG_OFF, ("efuseFreeNumber = %d\n", free_num));
	return TRUE;
}

VOID eFuseGetFreeBlockCount(IN struct rtmp_adapter *pAd,
	PUINT EfuseFreeBlock)
{

	USHORT i=0, StartBlock=0, EndBlock=0;
	USHORT	LogicalAddress;
	USHORT	FirstFreeBlock = 0xffff, LastFreeBlock = 0xffff;

#ifndef CAL_FREE_IC_SUPPORT
	if(!pAd->bUseEfuse)
		{
		DBGPRINT(RT_DEBUG_TRACE,("eFuseGetFreeBlockCount Only supports efuse Mode\n"));
		return ;
		}
#endif /* CAL_FREE_IC_SUPPORT */
	*EfuseFreeBlock = 0;
	/* find first free block*/
	if( (pAd->chipCap.EFUSE_USAGE_MAP_START % 2) != 0)
	{
		StartBlock = pAd->chipCap.EFUSE_USAGE_MAP_START-1;
	}
	else
	{
		StartBlock = pAd->chipCap.EFUSE_USAGE_MAP_START;
	}

	if( (pAd->chipCap.EFUSE_USAGE_MAP_END % 2) != 0)
	{
		EndBlock = pAd->chipCap.EFUSE_USAGE_MAP_END-1;
	}
	else
	{
		EndBlock = pAd->chipCap.EFUSE_USAGE_MAP_END;
	}

	for (i = StartBlock; i <= EndBlock; i+=2)
	{
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);

		if( (LogicalAddress & 0xff) == 0)
		{
			if(i != (pAd->chipCap.EFUSE_USAGE_MAP_START-1))
			{
				FirstFreeBlock = i;
				break;
			}
		}

		if(( (LogicalAddress >> 8) & 0xff) == 0)
		{
			if(i != pAd->chipCap.EFUSE_USAGE_MAP_END)
			{
				FirstFreeBlock = i+1;
				break;
			}
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseGetFreeBlockCount, FirstFreeBlock= 0x%x\n", FirstFreeBlock));

	/*if not find, return free block number = 0*/
	if(FirstFreeBlock == 0xffff)
	{
		*EfuseFreeBlock = 0;
		return;
	}
	for (i = EndBlock; i >= StartBlock; i-=2)
	{
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);

		if(( (LogicalAddress >> 8) & 0xff) == 0)
		{
			if(i != pAd->chipCap.EFUSE_USAGE_MAP_END)
			{
				LastFreeBlock = i+1;
				break;
			}
		}

		if( (LogicalAddress & 0xff) == 0)
		{
			if(i != (pAd->chipCap.EFUSE_USAGE_MAP_START-1))
			{
				LastFreeBlock = i;
				break;
			}
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseGetFreeBlockCount, LastFreeBlock= 0x%x\n", LastFreeBlock));

	/*if not find last free block, return free block number = 0, this should not happen since we have checked first free block number previously*/
	if(LastFreeBlock == 0xffff)
	{
		*EfuseFreeBlock = 0;
		return;
	}

	/* return total free block number, last free block number must >= first free block number*/
	if(LastFreeBlock < FirstFreeBlock)
	{
		*EfuseFreeBlock = 0;
	}
	else
	{
		*EfuseFreeBlock = LastFreeBlock - FirstFreeBlock + 1;
	}

	DBGPRINT(RT_DEBUG_TRACE,("eFuseGetFreeBlockCount is %d\n",*EfuseFreeBlock));
}


INT eFuse_init(struct rtmp_adapter *pAd)
{
	UINT EfuseFreeBlock=0;
	INT result;
	BOOLEAN bCalFree;

	/*RT3572 means 3062/3562/3572*/
	/*3593 means 3593*/
	DBGPRINT(RT_DEBUG_ERROR, ("NVM is Efuse and its size =%x[%x-%x] \n",pAd->chipCap.EFUSE_USAGE_MAP_SIZE,pAd->chipCap.EFUSE_USAGE_MAP_START,pAd->chipCap.EFUSE_USAGE_MAP_END));
	eFuseGetFreeBlockCount(pAd, &EfuseFreeBlock);
	/*If the used block of efuse is less than 5. We assume the default value*/
	/* of this efuse is empty and change to the buffer mode in odrder to */
	/*bring up interfaces successfully.*/


	if ( EfuseFreeBlock >= pAd->chipCap.EFUSE_RESERVED_SIZE )
	{


		DBGPRINT(RT_DEBUG_OFF, ("NVM is efuse and the information is too less to bring up the interface\n"));
		DBGPRINT(RT_DEBUG_OFF, ("Load EEPROM buffer from BIN, and force to use BIN buffer mode\n"));

		pAd->bUseEfuse = FALSE;

		/* Forse to use BIN eeprom buffer mode */
		RtmpChipOpsEepromHook(pAd);

#ifdef CAL_FREE_IC_SUPPORT
		//pAd->bFroceEEPROMBuffer = TRUE;
		RTMP_CAL_FREE_IC_CHECK(pAd, bCalFree);
#endif /* CAL_FREE_IC_SUPPORT */

		if ( result == FALSE )
		{
			if ( pAd->chipCap.EEPROM_DEFAULT_BIN != NULL )
			{
				memmove(pAd->EEPROMImage, pAd->chipCap.EEPROM_DEFAULT_BIN,
					pAd->chipCap.EEPROM_DEFAULT_BIN_SIZE > MAX_EEPROM_BUFFER_SIZE?MAX_EEPROM_BUFFER_SIZE:pAd->chipCap.EEPROM_DEFAULT_BIN_SIZE);
				DBGPRINT(RT_DEBUG_TRACE, ("Load EEPROM Buffer from default BIN.\n"));
			}

#ifdef CAL_FREE_IC_SUPPORT
			if ( bCalFree )
			{
				RTMP_CAL_FREE_DATA_GET(pAd);
			}
#endif /* CAL_FREE_IC_SUPPORT */
		}

	}

	return 0;
}


INT efuse_probe(struct rtmp_adapter *pAd)
{
	uint32_t eFuseCtrl, ctrl_reg;


	if (WaitForAsicReady(pAd) == FALSE)
		return -1;

	pAd->bUseEfuse=FALSE;
#ifdef RT65xx
	// TODO: shiang-6590, find a better naming for EFUSE_CTRL_3290!!
	if (IS_RT65XX(pAd))
		ctrl_reg = EFUSE_CTRL_3290;
	else
#endif /* RT65xx */
		ctrl_reg = EFUSE_CTRL;
	RTMP_IO_READ32(pAd, ctrl_reg, &eFuseCtrl);
	pAd->bUseEfuse = ( (eFuseCtrl & 0x80000000) == 0x80000000) ? 1 : 0;

	return 0;
}


#endif /* RTMP_EFUSE_SUPPORT */

