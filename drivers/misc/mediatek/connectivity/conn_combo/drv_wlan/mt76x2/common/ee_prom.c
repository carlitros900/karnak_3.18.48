/*
 ***************************************************************************
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ***************************************************************************

	Module Name:
	ee_prom.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#include	"rt_config.h"

/* IRQL = PASSIVE_LEVEL*/
static inline VOID RaiseClock(IN PRTMP_ADAPTER pAd, IN UINT32 *x)
{
	*x = *x | EESK;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
	RtmpusecDelay(1);	/* Max frequency = 1MHz in Spec. definition */
}

/* IRQL = PASSIVE_LEVEL*/
static inline VOID LowerClock(IN PRTMP_ADAPTER pAd, IN UINT32 *x)
{
	*x = *x & ~EESK;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
	RtmpusecDelay(1);
}

/* IRQL = PASSIVE_LEVEL*/
static inline USHORT ShiftInBits(IN PRTMP_ADAPTER pAd)
{
	UINT32 x, i;
	USHORT data = 0;

	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~(EEDO | EEDI);

	for (i = 0; i < 16; i++) {
		data = data << 1;
		RaiseClock(pAd, &x);

		RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
		LowerClock(pAd, &x);	/*prevent read failed */

		x &= ~(EEDI);
		if (x & EEDO)
			data |= 1;
	}

	return data;
}

/* IRQL = PASSIVE_LEVEL*/
static inline VOID ShiftOutBits(IN PRTMP_ADAPTER pAd, IN USHORT data, IN USHORT count)
{
	UINT32 x, mask;

	mask = 0x01 << (count - 1);
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~(EEDO | EEDI);

	do {
		x &= ~EEDI;
		if (data & mask)
			x |= EEDI;

		RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

		RaiseClock(pAd, &x);
		LowerClock(pAd, &x);

		mask = mask >> 1;
	} while (mask);

	x &= ~EEDI;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);
}

/* IRQL = PASSIVE_LEVEL*/
static inline VOID EEpromCleanup(IN PRTMP_ADAPTER pAd)
{
	UINT32 x;

	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~(EECS | EEDI);
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);
}

static inline VOID EWEN(IN PRTMP_ADAPTER pAd)
{
	UINT32 x;

	/* reset bits and set EECS */
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* kick a pulse */
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

	/* output the read_opcode and six pulse in that order    */
	ShiftOutBits(pAd, EEPROM_EWEN_OPCODE, 5);
	ShiftOutBits(pAd, 0, 6);

	EEpromCleanup(pAd);
}

static inline VOID EWDS(IN PRTMP_ADAPTER pAd)
{
	UINT32 x;

	/* reset bits and set EECS */
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* kick a pulse */
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

	/* output the read_opcode and six pulse in that order    */
	ShiftOutBits(pAd, EEPROM_EWDS_OPCODE, 5);
	ShiftOutBits(pAd, 0, 6);

	EEpromCleanup(pAd);
}


INT rtmp_ee_write_to_prom(IN PRTMP_ADAPTER pAd)
{
	USHORT value, offset = 0;

#ifdef RTMP_USB_SUPPORT
	if (pAd->infType == RTMP_DEV_INF_USB)
		RTUSBWriteEEPROM(pAd, offset, (PUCHAR) pAd->EEPROMImage, EEPROM_SIZE);
	else
#endif /* RTMP_USB_SUPPORT */
	{
		for (offset = 0; offset < (EEPROM_SIZE >> 1);) {
			value = ((PUSHORT) pAd->EEPROMImage)[offset];
			RT28xx_EEPROM_WRITE16(pAd, (offset << 1), value);
			offset++;
		}
	}

	return TRUE;
}
