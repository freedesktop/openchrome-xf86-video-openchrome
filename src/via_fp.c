/*
 * Copyright 2017 Kevin Brace. All Rights Reserved.
 * Copyright 2007-2015 The OpenChrome Project
 *                     [https://www.freedesktop.org/wiki/Openchrome]
 * Copyright 1998-2007 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2007 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Integrated LVDS power management functions.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "via_driver.h"
#include <unistd.h>

/*
 * Option handling.
 */
enum ViaPanelOpts {
    OPTION_CENTER
};

static OptionInfoRec ViaPanelOptions[] =
{
    {OPTION_CENTER,     "Center",       OPTV_BOOLEAN,   {0},    FALSE},
    {-1,                NULL,           OPTV_NONE,      {0},    FALSE}
};

/* These table values were copied from lcd.c of VIA Frame 
 * Buffer device driver. */
/* {int Width, int Height, bool useDualEdge, bool useDithering}; */
static ViaPanelModeRec ViaPanelNativeModes[] = {
    { 640,  480, FALSE,  TRUE},
    { 800,  600, FALSE,  TRUE},
    {1024,  768, FALSE,  TRUE},
    {1280,  768, FALSE,  TRUE},
    {1280, 1024,  TRUE,  TRUE},
    {1400, 1050,  TRUE,  TRUE},
    {1600, 1200,  TRUE,  TRUE},
    {1280,  800, FALSE,  TRUE},
    { 800,  480, FALSE,  TRUE},
    {1024,  768,  TRUE,  TRUE},
    {1366,  768, FALSE, FALSE},
    {1024,  768,  TRUE, FALSE},
    {1280,  768, FALSE, FALSE},
    {1280, 1024,  TRUE, FALSE},
    {1400, 1050,  TRUE, FALSE},
    {1600, 1200,  TRUE, FALSE},
    {1366,  768, FALSE, FALSE},
    {1024,  600, FALSE,  TRUE},
    {1280,  768,  TRUE,  TRUE},
    {1280,  800, FALSE,  TRUE},
    {1360,  768, FALSE, FALSE},
    {1280,  768,  TRUE, FALSE},
    { 480,  640, FALSE,  TRUE},
    {1200,  900, FALSE, FALSE}};

#define MODEPREFIX(name) NULL, NULL, name, 0, M_T_DRIVER | M_T_DEFAULT
#define MODESUFFIX 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,FALSE,FALSE,0,NULL,0,0.0,0.0

static DisplayModeRec OLPCMode = {
    MODEPREFIX("1200x900"),
    57275, 1200, 1208, 1216, 1240, 0,
    900,  905,  908,  912, 0,
    V_NHSYNC | V_NVSYNC, MODESUFFIX
};

/*
	1. Formula:
		2^13 X 0.0698uSec [1/14.318MHz] = 8192 X 0.0698uSec =572.1uSec
		Timer = Counter x 572 uSec
	2. Note:
		0.0698 uSec is too small to compute for hardware. So we multiply a
		reference value(2^13) to make it big enough to compute for hardware.
	3. Note:
		The meaning of the TD0~TD3 are count of the clock.
		TD(sec) = (sec)/(per clock) x (count of clocks)
*/

#define TD0 200
#define TD1 25
#define TD2 0
#define TD3 25

/*
 * Sets LVDS2 (LVDS Channel 2) integrated LVDS transmitter delay tap.
 */
static void
viaLVDS2SetDelayTap(ScrnInfoPtr pScrn, CARD8 delayTap)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaLVDS2SetDelayTap.\n"));

    /* Set LVDS2 delay tap. */
    /* 3X5.97[3:0] - LVDS2 Delay Tap */
    ViaCrtcMask(hwp, 0x97, delayTap, 0x0F);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "LVDS2 Delay Tap: %d\n",
                (delayTap & 0x0F));

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaLVDS2SetDelayTap.\n"));
}

/*
 * Sets IGA1 or IGA2 as the display output source for VIA Technologies
 * Chrome IGP DFP (Digital Flat Panel) High interface.
 */
static void
viaDFPHighSetDisplaySource(ScrnInfoPtr pScrn, CARD8 displaySource)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    CARD8 temp = displaySource;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaDFPHighSetDisplaySource.\n"));

    /* Set DFP High display output source. */
    /* 3X5.97[4] - DFP High Data Source Selection
     *             0: Primary Display
     *             1: Secondary Display */
    ViaCrtcMask(hwp, 0x97, temp << 4, 0x10);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "DFP High Display Output Source: IGA%d\n",
                (temp & 0x01) + 1);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaDFPHighSetDisplaySource.\n"));
}

/*
 * Sets DFP (Digital Flat Panel) Low interface delay tap.
 */
static void
viaDFPLowSetDelayTap(ScrnInfoPtr pScrn, CARD8 delayTap)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaDFPLowSetDelayTap.\n"));

    /* Set DFP Low interface delay tap. */
    /* 3X5.99[3:0] - DFP Low Delay Tap */
    ViaCrtcMask(hwp, 0x99, delayTap, 0x0F);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "DFP Low Delay Tap: %d\n",
                (delayTap & 0x0F));

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaDFPLowSetDelayTap.\n"));
}

/*
 * Sets DFP (Digital Flat Panel) High interface delay tap.
 */
static void
viaDFPHighSetDelayTap(ScrnInfoPtr pScrn, CARD8 delayTap)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaDFPHighSetDelayTap.\n"));

    /* Set DFP High interface delay tap. */
    /* 3X5.97[3:0] - DFP High Delay Tap */
    ViaCrtcMask(hwp, 0x97, delayTap, 0x0F);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "DFP High Delay Tap: %d\n",
                (delayTap & 0x0F));

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaDFPHighSetDelayTap.\n"));
}

static void
viaFPIOPadState(ScrnInfoPtr pScrn, CARD8 diPort, Bool ioPadOn)
{
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPIOPadState.\n"));

    switch(diPort) {
    case VIA_DI_PORT_DVP0:
        viaDVP0SetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case VIA_DI_PORT_DVP1:
        viaDVP1SetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case VIA_DI_PORT_FPDPLOW:
        viaFPDPLowSetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case VIA_DI_PORT_FPDPHIGH:
        viaFPDPHighSetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case (VIA_DI_PORT_FPDPLOW |
          VIA_DI_PORT_FPDPHIGH):
        viaFPDPLowSetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        viaFPDPHighSetIOPadState(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case VIA_DI_PORT_LVDS1:
        viaLVDS1SetIOPadSetting(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case VIA_DI_PORT_LVDS2:
        viaLVDS2SetIOPadSetting(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    case (VIA_DI_PORT_LVDS1 |
          VIA_DI_PORT_LVDS2):
        viaLVDS1SetIOPadSetting(pScrn, ioPadOn ? 0x03 : 0x00);
        viaLVDS2SetIOPadSetting(pScrn, ioPadOn ? 0x03 : 0x00);
        break;
    default:
        break;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "FP I/O Pad: %s\n",
                ioPadOn ? "On": "Off");

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPIOPadState.\n"));
}

static void
viaFPFormat(ScrnInfoPtr pScrn, CARD8 diPort, CARD8 format)
{
    CARD8 temp = format & 0x01;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPFormat.\n"));

    switch(diPort) {
    case VIA_DI_PORT_LVDS1:
        viaLVDS1SetFormat(pScrn, temp);
        break;
    case VIA_DI_PORT_LVDS2:
        viaLVDS2SetFormat(pScrn, temp);
        break;
    case (VIA_DI_PORT_LVDS1 |
          VIA_DI_PORT_LVDS2):
        viaLVDS1SetFormat(pScrn, temp);
        viaLVDS2SetFormat(pScrn, temp);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPFormat.\n"));
}

static void
viaFPOutputFormat(ScrnInfoPtr pScrn, CARD8 diPort, CARD8 outputFormat)
{
    CARD8 temp = outputFormat & 0x01;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPOutputFormat.\n"));

    switch(diPort) {
    case VIA_DI_PORT_LVDS1:
        viaLVDS1SetOutputFormat(pScrn, temp);
        break;
    case VIA_DI_PORT_LVDS2:
        viaLVDS2SetOutputFormat(pScrn, temp);
        break;
    case (VIA_DI_PORT_LVDS1 |
          VIA_DI_PORT_LVDS2):
        viaLVDS1SetOutputFormat(pScrn, temp);
        viaLVDS2SetOutputFormat(pScrn, temp);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPOutputFormat.\n"));
}

static void
viaFPDithering(ScrnInfoPtr pScrn, CARD8 diPort, Bool dithering)
{
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPDithering.\n"));

    switch(diPort) {
    case VIA_DI_PORT_LVDS1:
        viaLVDS1SetDithering(pScrn, dithering);
        break;
    case VIA_DI_PORT_LVDS2:
        viaLVDS2SetDithering(pScrn, dithering);
        break;
    case (VIA_DI_PORT_LVDS1 |
          VIA_DI_PORT_LVDS2):
        viaLVDS1SetDithering(pScrn, dithering);
        viaLVDS2SetDithering(pScrn, dithering);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPDithering.\n"));
}

static void
viaFPDisplaySource(ScrnInfoPtr pScrn, CARD8 diPort, int index)
{
    CARD8 displaySource = index & 0x01;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPDisplaySource.\n"));

    switch(diPort) {
    case VIA_DI_PORT_DVP0:
        viaDVP0SetDisplaySource(pScrn, displaySource);
        break;
    case VIA_DI_PORT_DVP1:
        viaDVP1SetDisplaySource(pScrn, displaySource);
        break;
    case VIA_DI_PORT_FPDPLOW:
        viaDFPLowSetDisplaySource(pScrn, displaySource);
        viaDVP1SetDisplaySource(pScrn, displaySource);
        break;
    case VIA_DI_PORT_FPDPHIGH:
        viaDFPHighSetDisplaySource(pScrn, displaySource);
        viaDVP0SetDisplaySource(pScrn, displaySource);
        break;
    case (VIA_DI_PORT_FPDPLOW |
          VIA_DI_PORT_FPDPHIGH):
        viaDFPLowSetDisplaySource(pScrn, displaySource);
        viaDFPHighSetDisplaySource(pScrn, displaySource);
        break;
    case VIA_DI_PORT_LVDS1:
        viaLVDS1SetDisplaySource(pScrn, displaySource);
        break;
    case VIA_DI_PORT_LVDS2:
        viaLVDS2SetDisplaySource(pScrn, displaySource);
        break;
    case (VIA_DI_PORT_LVDS1 |
          VIA_DI_PORT_LVDS2):
        viaLVDS1SetDisplaySource(pScrn, displaySource);
        viaLVDS2SetDisplaySource(pScrn, displaySource);
        break;
    default:
        break;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "FP Display Source: IGA%d\n",
                displaySource + 1);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPDisplaySource.\n"));
}

/*
 * This software controlled FP power on / off sequence code is
 * for CLE266's IGP which was codenamed Castle Rock. The code is
 * untested. The turn on sequence and register access likely
 * originated from the code VIA Technologies made open source around
 * Year 2004.
 */
static void
viaFPCastleRockSoftPowerSeq(ScrnInfoPtr pScrn, Bool powerState)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPCastleRockSoftPowerSeq.\n"));

    if (powerState) {
        ViaCrtcMask(hwp, 0x6A, BIT(3), BIT(3));

        ViaCrtcMask(hwp, 0x91, BIT(4), BIT(4));
        usleep(25);

        ViaCrtcMask(hwp, 0x91, BIT(3), BIT(3));
        usleep(510);

        ViaCrtcMask(hwp, 0x91, BIT(2) | BIT(1), BIT(2) | BIT(1));
        usleep(1);
    } else {
        ViaCrtcMask(hwp, 0x6A, 0x00, BIT(3));

        ViaCrtcMask(hwp, 0x91, 0x00, BIT(2) | BIT(1));
        usleep(210);

        ViaCrtcMask(hwp, 0x91, 0x00, BIT(3));
        usleep(25);

        ViaCrtcMask(hwp, 0x91, 0x00, BIT(4));
        usleep(1);
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPCastleRockSoftPowerSeq.\n"));
}

static void
ViaLVDSSoftwarePowerFirstSequence(ScrnInfoPtr pScrn, Bool on)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ViaLVDSSoftwarePowerFirstSequence: %d\n", on));
    if (on) {

        /* Software control power sequence ON*/
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0x7F);
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) | 0x01);
        usleep(TD0);

        /* VDD ON*/
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) | 0x10);
        usleep(TD1);

        /* DATA ON */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) | 0x08);
        usleep(TD2);

        /* VEE ON (unused on vt3353)*/
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) | 0x04);
        usleep(TD3);

        /* Back-Light ON */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) | 0x02);
    } else {
        /* Back-Light OFF */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0xFD);
        usleep(TD3);

        /* VEE OFF (unused on vt3353)*/
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0xFB);
        usleep(TD2);

        /* DATA OFF */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0xF7);
        usleep(TD1);

        /* VDD OFF */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0xEF);
    }
}

static void
ViaLVDSSoftwarePowerSecondSequence(ScrnInfoPtr pScrn, Bool on)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ViaLVDSSoftwarePowerSecondSequence: %d\n", on));
    if (on) {
        /* Secondary power hardware power sequence enable 0:off 1: on */
        hwp->writeCrtc(hwp, 0xD4, hwp->readCrtc(hwp, 0xD4) & 0xFD);

        /* Software control power sequence ON */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) | 0x01);
        usleep(TD0);

        /* VDD ON*/
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) | 0x10);
        usleep(TD1);

        /* DATA ON */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) | 0x08);
        usleep(TD2);

        /* VEE ON (unused on vt3353)*/
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) | 0x04);
        usleep(TD3);

        /* Back-Light ON */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) | 0x02);
    } else {
        /* Back-Light OFF */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0xFD);
        usleep(TD3);

        /* VEE OFF */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0xFB);
        /* Delay TD2 msec. */
        usleep(TD2);

        /* DATA OFF */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0xF7);
        /* Delay TD1 msec. */
        usleep(TD1);

        /* VDD OFF */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0xEF);
    }
}

static void
viaFPPrimaryHardPowerSeq(ScrnInfoPtr pScrn, Bool powerState)
{
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPPrimaryHardPowerSeq.\n"));

    /* Use hardware FP power sequence control. */
    viaFPSetPrimaryPowerSeqType(pScrn, TRUE);

    if (powerState) {
        /* Power on FP. */
        viaFPSetPrimaryHardPower(pScrn, TRUE);

        /* Make sure back light is turned on. */
        viaFPSetPrimaryDirectBackLightCtrl(pScrn, TRUE);

        /* Make sure display period is turned on. */
        viaFPSetPrimaryDirectDisplayPeriodCtrl(pScrn, TRUE);
    } else {
        /* Make sure display period is turned off. */
        viaFPSetPrimaryDirectDisplayPeriodCtrl(pScrn, FALSE);

        /* Make sure back light is turned off. */
        viaFPSetPrimaryDirectBackLightCtrl(pScrn, FALSE);

        /* Power off FP. */
        viaFPSetPrimaryHardPower(pScrn, FALSE);
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPPrimaryHardPowerSeq.\n"));
}

static void
ViaLVDSHardwarePowerFirstSequence(ScrnInfoPtr pScrn, Bool on)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    if (on) {
        /* Use hardware control power sequence. */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0xFE);
        /* Turn on back light. */
        hwp->writeCrtc(hwp, 0x91, hwp->readCrtc(hwp, 0x91) & 0x3F);
        /* Turn on hardware power sequence. */
        hwp->writeCrtc(hwp, 0x6A, hwp->readCrtc(hwp, 0x6A) | 0x08);
    } else {
        /* Turn off power sequence. */
        hwp->writeCrtc(hwp, 0x6A, hwp->readCrtc(hwp, 0x6A) & 0xF7);
        usleep(1);
        /* Turn off back light. */
        hwp->writeCrtc(hwp, 0x91, 0xC0);
    }
}

static void
ViaLVDSHardwarePowerSecondSequence(ScrnInfoPtr pScrn, Bool on)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    if (on) {
        /* Use hardware control power sequence. */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0xFE);
        /* Turn on back light. */
        hwp->writeCrtc(hwp, 0xD3, hwp->readCrtc(hwp, 0xD3) & 0x3F);
        /* Turn on hardware power sequence. */
        hwp->writeCrtc(hwp, 0xD4, hwp->readCrtc(hwp, 0xD4) | 0x02);
    } else {
        /* Turn off power sequence. */
        hwp->writeCrtc(hwp, 0xD4, hwp->readCrtc(hwp, 0xD4) & 0xFD);
        usleep(1);
        /* Turn off back light. */
        hwp->writeCrtc(hwp, 0xD3, 0xC0);
    }
}

static void
viaFPPower(ScrnInfoPtr pScrn, int Chipset, CARD8 diPortType, Bool powerState)
{
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPPower.\n"));

    switch (Chipset) {
    case VIA_CLE266:
        viaFPCastleRockSoftPowerSeq(pScrn, powerState);
        break;
    case VIA_KM400:
    case VIA_P4M800PRO:
    case VIA_PM800:
    case VIA_K8M800:
    case VIA_P4M890:
    case VIA_K8M890:
    case VIA_P4M900:
        viaFPPrimaryHardPowerSeq(pScrn, powerState);
        break;
    case VIA_CX700:
    case VIA_VX800:
        /*
         * VX800, CX700 have HW issue, so we'd better use SW power sequence.
         * Fix Ticket #308.
         */
        if (diPortType & VIA_DI_PORT_LVDS1) {
            ViaLVDSSoftwarePowerFirstSequence(pScrn, powerState);
            viaLVDS1SetPower(pScrn, powerState);
        }

        if (diPortType & VIA_DI_PORT_LVDS2) {
            ViaLVDSSoftwarePowerSecondSequence(pScrn, powerState);
            viaLVDS2SetPower(pScrn, powerState);
        }

        break;
    case VIA_VX855:
    case VIA_VX900:
        ViaLVDSHardwarePowerFirstSequence(pScrn, powerState);
        viaLVDS1SetPower(pScrn, powerState);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPPower.\n"));
}

/*
 * Try to interpret EDID ourselves.
 */
static Bool
ViaPanelGetSizeFromEDID(ScrnInfoPtr pScrn, xf86MonPtr pMon,
                        int *width, int *height)
{
    int i, max_hsize = 0, vsize = 0;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VIAGetPanelSizeFromEDID\n"));

    /* !!! Why are we not checking VESA modes? */

    /* checking standard timings */
    for (i = 0; i < STD_TIMINGS; i++)
        if ((pMon->timings2[i].hsize > 256)
            && (pMon->timings2[i].hsize > max_hsize)) {
            max_hsize = pMon->timings2[i].hsize;
            vsize = pMon->timings2[i].vsize;
        }

    if (max_hsize != 0) {
        *width = max_hsize;
        *height = vsize;
        return TRUE;
    }

    /* checking detailed monitor section */

    /* !!! skip Ranges and standard timings */

    /* check detailed timings */
    for (i = 0; i < DET_TIMINGS; i++)
        if (pMon->det_mon[i].type == DT) {
            struct detailed_timings timing = pMon->det_mon[i].section.d_timings;

            /* ignore v_active for now */
            if ((timing.clock > 15000000) && (timing.h_active > max_hsize)) {
                max_hsize = timing.h_active;
                vsize = timing.v_active;
            }
        }

    if (max_hsize != 0) {
        *width = max_hsize;
        *height = vsize;
        return TRUE;
    }
    return FALSE;
}

/*
 * Gets the native panel resolution from scratch pad registers.
 */
static void
viaLVDSGetFPInfoFromScratchPad(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;
    CARD8 index;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                     "Entered viaLVDSGetFPInfoFromScratchPad.\n"));

    index = hwp->readCrtc(hwp, 0x3F) & 0x0F;

    pVIAFP->NativeModeIndex = index;
    pVIAFP->NativeWidth = ViaPanelNativeModes[index].Width;
    pVIAFP->NativeHeight = ViaPanelNativeModes[index].Height;
    pVIAFP->useDualEdge = ViaPanelNativeModes[index].useDualEdge;
    pVIAFP->useDithering = ViaPanelNativeModes[index].useDithering;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "VIA Technologies VGA BIOS Scratch Pad Register "
               "Flat Panel Index: %d\n",
               pVIAFP->NativeModeIndex);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Flat Panel Native Resolution: %dx%d\n",
               pVIAFP->NativeWidth, pVIAFP->NativeHeight);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Flat Panel Dual Edge Transfer: %s\n",
               pVIAFP->useDualEdge ? "On" : "Off");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Flat Panel Output Color Dithering: %s\n",
               pVIAFP->useDithering ? "On (18 bit)" : "Off (24 bit)");

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                     "Exiting viaLVDSGetFPInfoFromScratchPad.\n"));
}

static void
ViaPanelCenterMode(DisplayModePtr mode, DisplayModePtr adjusted_mode)
{
    int panelHSyncTime = adjusted_mode->HSyncEnd - adjusted_mode->HSyncStart;
    int panelVSyncTime = adjusted_mode->VSyncEnd - adjusted_mode->VSyncStart;
    int panelHBlankStart = adjusted_mode->HDisplay;
    int panelVBlankStart = adjusted_mode->VDisplay;
    int hBorder = (adjusted_mode->HDisplay - mode->HDisplay)/2;
    int vBorder = (adjusted_mode->VDisplay - mode->VDisplay)/2;
    int newHBlankStart = hBorder + mode->HDisplay;
    int newVBlankStart = vBorder + mode->VDisplay;

    adjusted_mode->HDisplay = mode->HDisplay;
    adjusted_mode->HSyncStart = (adjusted_mode->HSyncStart - panelHBlankStart) + newHBlankStart;
    adjusted_mode->HSyncEnd = adjusted_mode->HSyncStart + panelHSyncTime;
    adjusted_mode->VDisplay = mode->VDisplay;
    adjusted_mode->VSyncStart = (adjusted_mode->VSyncStart - panelVBlankStart) + newVBlankStart;
    adjusted_mode->VSyncEnd = adjusted_mode->VSyncStart + panelVSyncTime;
    /* Adjust Crtc H and V */
    adjusted_mode->CrtcHDisplay = adjusted_mode->HDisplay;
    adjusted_mode->CrtcHBlankStart = newHBlankStart;
    adjusted_mode->CrtcHBlankEnd = adjusted_mode->CrtcHTotal - hBorder;
    adjusted_mode->CrtcHSyncStart = adjusted_mode->HSyncStart;
    adjusted_mode->CrtcHSyncEnd = adjusted_mode->HSyncEnd;
    adjusted_mode->CrtcVDisplay = adjusted_mode->VDisplay;
    adjusted_mode->CrtcVBlankStart = newVBlankStart;
    adjusted_mode->CrtcVBlankEnd = adjusted_mode->CrtcVTotal - vBorder;
    adjusted_mode->CrtcVSyncStart = adjusted_mode->VSyncStart;
    adjusted_mode->CrtcVSyncEnd = adjusted_mode->VSyncEnd;
}

static void
ViaPanelScale(ScrnInfoPtr pScrn, int resWidth, int resHeight,
              int panelWidth, int panelHeight)
{
    VIAPtr pVia = VIAPTR(pScrn);
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    int horScalingFactor = 0;
    int verScalingFactor = 0;
    CARD8 cra2 = 0;
    CARD8 cr77 = 0;
    CARD8 cr78 = 0;
    CARD8 cr79 = 0;
    CARD8 cr9f = 0;
    Bool scaling = FALSE;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "ViaPanelScale: %d,%d -> %d,%d\n",
                    resWidth, resHeight, panelWidth, panelHeight));

    if (resWidth < panelWidth) {
        /* Load Horizontal Scaling Factor */
        if (pVia->Chipset != VIA_CLE266 && pVia->Chipset != VIA_KM400) {
            horScalingFactor = ((resWidth - 1) * 4096) / (panelWidth - 1);

            /* Horizontal scaling enabled */
            cra2 = 0xC0;
            cr9f = horScalingFactor & 0x0003;   /* HSCaleFactor[1:0] at CR9F[1:0] */
        } else {
            /* TODO: Need testing */
            horScalingFactor = ((resWidth - 1) * 1024) / (panelWidth - 1);
        }

        cr77 = (horScalingFactor & 0x03FC) >> 2;   /* HSCaleFactor[9:2] at CR77[7:0] */
        cr79 = (horScalingFactor & 0x0C00) >> 10;  /* HSCaleFactor[11:10] at CR79[5:4] */
        cr79 <<= 4;
        scaling = TRUE;
    }

    if (resHeight < panelHeight) {
        /* Load Vertical Scaling Factor */
        if (pVia->Chipset != VIA_CLE266 && pVia->Chipset != VIA_KM400) {
            verScalingFactor = ((resHeight - 1) * 2048) / (panelHeight - 1);

            /* Vertical scaling enabled */
            cra2 |= 0x08;
            cr79 |= ((verScalingFactor & 0x0001) << 3); /* VSCaleFactor[0] at CR79[3] */
        } else {
            /* TODO: Need testing */
            verScalingFactor = ((resHeight - 1) * 1024) / (panelHeight - 1);
        }

        cr78 |= (verScalingFactor & 0x01FE) >> 1;           /* VSCaleFactor[8:1] at CR78[7:0] */

        cr79 |= ((verScalingFactor & 0x0600) >> 9) << 6;    /* VSCaleFactor[10:9] at CR79[7:6] */
        scaling = TRUE;
    }

    if (scaling) {
        DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Scaling factor: horizontal %d (0x%x), vertical %d (0x%x)\n",
        horScalingFactor, horScalingFactor,
        verScalingFactor, verScalingFactor));

        ViaCrtcMask(hwp, 0x77, cr77, 0xFF);
        ViaCrtcMask(hwp, 0x78, cr78, 0xFF);
        ViaCrtcMask(hwp, 0x79, cr79, 0xF8);

        if (pVia->Chipset != VIA_CLE266 && pVia->Chipset != VIA_KM400) {
            ViaCrtcMask(hwp, 0x9F, cr9f, 0x03);
        }
        ViaCrtcMask(hwp, 0x79, 0x03, 0x03);
    } else {
        /*  Disable panel scale */
        ViaCrtcMask(hwp, 0x79, 0x00, 0x01);
    }

    if (pVia->Chipset != VIA_CLE266 && pVia->Chipset != VIA_KM400) {
        ViaCrtcMask(hwp, 0xA2, cra2, 0xC8);
    }

    /* Horizontal scaling selection: interpolation */
    // ViaCrtcMask(hwp, 0x79, 0x02, 0x02);
    // else
    // ViaCrtcMask(hwp, 0x79, 0x00, 0x02);
    /* Horizontal scaling factor selection original / linear */
    //ViaCrtcMask(hwp, 0xA2, 0x40, 0x40);
}

static void
ViaPanelScaleDisable(ScrnInfoPtr pScrn)
{
    VIAPtr pVia = VIAPTR(pScrn);
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    ViaCrtcMask(hwp, 0x79, 0x00, 0x01);
    /* Disable VX900 down scaling */
    if (pVia->Chipset == VIA_VX900)
        ViaCrtcMask(hwp, 0x89, 0x00, 0x01);
    if (pVia->Chipset != VIA_CLE266 && pVia->Chipset != VIA_KM400)
        ViaCrtcMask(hwp, 0xA2, 0x00, 0xC8);
}

static void
via_fp_create_resources(xf86OutputPtr output)
{
}

static void
via_fp_dpms(xf86OutputPtr output, int mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    VIAPtr pVia = VIAPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_fp_dpms.\n"));

    switch (mode) {
    case DPMSModeOn:
        viaFPPower(pScrn, pVia->Chipset, pVIAFP->diPort, TRUE);
        viaFPIOPadState(pScrn, pVIAFP->diPort, TRUE);
        break;
    case DPMSModeStandby:
    case DPMSModeSuspend:
    case DPMSModeOff:
        viaFPPower(pScrn, pVia->Chipset, pVIAFP->diPort, FALSE);
        viaFPIOPadState(pScrn, pVIAFP->diPort, FALSE);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_fp_dpms.\n"));
}

static void
via_fp_save(xf86OutputPtr output)
{
}

static void
via_fp_restore(xf86OutputPtr output)
{
}

static int
via_fp_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    ScrnInfoPtr pScrn = output->scrn;
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    if (pVIAFP->NativeWidth < pMode->HDisplay ||
        pVIAFP->NativeHeight < pMode->VDisplay)
        return MODE_PANEL;

    if (!pVIAFP->Scale && pVIAFP->NativeHeight != pMode->VDisplay &&
         pVIAFP->NativeWidth != pMode->HDisplay)
        return MODE_PANEL;

    if (!ViaModeDotClockTranslate(pScrn, pMode))
        return MODE_NOCLOCK;

    return MODE_OK;
}

static Bool
via_fp_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode)
{
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    xf86SetModeCrtc(adjusted_mode, 0);
    if (!pVIAFP->Center && (mode->HDisplay < pVIAFP->NativeWidth ||
        mode->VDisplay < pVIAFP->NativeHeight)) {
        pVIAFP->Scale = TRUE;
    } else {
        pVIAFP->Scale = FALSE;
        ViaPanelCenterMode(mode, adjusted_mode);
    }
    return TRUE;
}

static void
via_fp_prepare(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    VIAPtr pVia = VIAPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_fp_prepare.\n"));

    viaFPPower(pScrn, pVia->Chipset, pVIAFP->diPort, FALSE);
    viaFPIOPadState(pScrn, pVIAFP->diPort, FALSE);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_fp_prepare.\n"));
}

static void
via_fp_commit(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    VIAPtr pVia = VIAPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_fp_commit.\n"));

    viaFPPower(pScrn, pVia->Chipset, pVIAFP->diPort, TRUE);
    viaFPIOPadState(pScrn, pVIAFP->diPort, TRUE);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_fp_commit.\n"));
}

static void
via_fp_mode_set(xf86OutputPtr output, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    drmmode_crtc_private_ptr iga = output->crtc->driver_private;
    VIAPtr pVia = VIAPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    if (output->crtc) {
        if (pVIAFP->Scale) {
            ViaPanelScale(pScrn, mode->HDisplay, mode->VDisplay,
                            pVIAFP->NativeWidth,
                            pVIAFP->NativeHeight);
        } else {
            ViaPanelScaleDisable(pScrn);
        }

        switch (pVia->Chipset) {
        case VIA_P4M900:
            viaDFPLowSetDelayTap(pScrn, 0x08);
            break;
        default:
            break;
        }

        switch (pVia->Chipset) {
        case VIA_CX700:
        case VIA_VX800:
        case VIA_VX855:
        case VIA_VX900:
            /* OPENLDI Mode */
            viaFPFormat(pScrn, pVIAFP->diPort, 0x01);

            /* Sequential Mode */
            viaFPOutputFormat(pScrn, pVIAFP->diPort, 0x01);

            viaFPDithering(pScrn, pVIAFP->diPort, pVIAFP->useDithering);
            break;
        default:
            break;
        }

        viaFPDisplaySource(pScrn, pVIAFP->diPort, iga->index);
    }
}

static xf86OutputStatus
via_fp_detect(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    xf86MonPtr pMon;
    xf86OutputStatus status = XF86OutputStatusDisconnected;
    I2CBusPtr pI2CBus;
    VIAPtr pVia = VIAPTR(pScrn);
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_fp_detect.\n"));

    /* Hardcode panel size for the OLPC XO-1.5. */
    if (pVia->IsOLPCXO15) {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "Setting up OLPC XO-1.5 flat panel.\n");
        pVIAFP->NativeWidth = 1200;
        pVIAFP->NativeHeight = 900;
        status = XF86OutputStatusConnected;
        goto exit;
    }

    if (pVIAFP->i2cBus & VIA_I2C_BUS2) {
        pI2CBus = pVia->pI2CBus2;
    } else if (pVIAFP->i2cBus & VIA_I2C_BUS3) {
        pI2CBus = pVia->pI2CBus3;
    } else {
        pI2CBus = NULL;
    }

    if (pI2CBus) {
        pMon = xf86OutputGetEDID(output, pI2CBus);
        if (pMon && DIGITAL(pMon->features.input_type)) {
            xf86OutputSetEDID(output, pMon);
            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                        "Detected a flat panel.\n");
            if (!ViaPanelGetSizeFromEDID(pScrn, pMon, &pVIAFP->NativeWidth, &pVIAFP->NativeHeight)) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                            "Unable to obtain panel size from EDID.\n");
                goto exit;
            }

            status = XF86OutputStatusConnected;
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                        "Could not obtain EDID from a flat "
                        "panel, but will obtain flat panel "
                        "information from scratch pad register.\n");

            /* For FP without I2C bus connection, CRTC scratch pad
             * register supplied by the VGA BIOS is the only method
             * available to figure out the FP native screen resolution. */
            viaLVDSGetFPInfoFromScratchPad(output);
            status = XF86OutputStatusConnected;
        }
    } else {
        /* For FP without I2C bus connection, CRTC scratch pad
         * register supplied by the VGA BIOS is the only method
         * available to figure out the FP native screen resolution. */
        viaLVDSGetFPInfoFromScratchPad(output);
        status = XF86OutputStatusConnected;
    }

exit:
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_fp_detect.\n"));
    return status;
}

static DisplayModePtr
via_fp_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    DisplayModePtr pDisplay_Mode = NULL;
    VIAFPPtr pVIAFP = (VIAFPPtr) output->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_fp_get_modes.\n"));

    if (output->status == XF86OutputStatusConnected) {
        if (!output->MonInfo) {
            /*
             * Generates a display mode for the native panel resolution,
             * using CVT.
             */
            if (pVIAFP->NativeWidth && pVIAFP->NativeHeight) {
                VIAPtr pVia = VIAPTR(pScrn);

                if (pVia->IsOLPCXO15) {
                    pDisplay_Mode = xf86DuplicateMode(&OLPCMode);
                } else {
                    pDisplay_Mode = xf86CVTMode(pVIAFP->NativeWidth, pVIAFP->NativeHeight,
                                    60.0f, FALSE, FALSE);
                }

                if (pDisplay_Mode) {
                    pDisplay_Mode->CrtcHDisplay = pDisplay_Mode->HDisplay;
                    pDisplay_Mode->CrtcHSyncStart = pDisplay_Mode->HSyncStart;
                    pDisplay_Mode->CrtcHSyncEnd = pDisplay_Mode->HSyncEnd;
                    pDisplay_Mode->CrtcHTotal = pDisplay_Mode->HTotal;
                    pDisplay_Mode->CrtcHSkew = pDisplay_Mode->HSkew;
                    pDisplay_Mode->CrtcVDisplay = pDisplay_Mode->VDisplay;
                    pDisplay_Mode->CrtcVSyncStart = pDisplay_Mode->VSyncStart;
                    pDisplay_Mode->CrtcVSyncEnd = pDisplay_Mode->VSyncEnd;
                    pDisplay_Mode->CrtcVTotal = pDisplay_Mode->VTotal;

                    pDisplay_Mode->CrtcVBlankStart = min(pDisplay_Mode->CrtcVSyncStart, pDisplay_Mode->CrtcVDisplay);
                    pDisplay_Mode->CrtcVBlankEnd = max(pDisplay_Mode->CrtcVSyncEnd, pDisplay_Mode->CrtcVTotal);
                    pDisplay_Mode->CrtcHBlankStart = min(pDisplay_Mode->CrtcHSyncStart, pDisplay_Mode->CrtcHDisplay);
                    pDisplay_Mode->CrtcHBlankEnd = max(pDisplay_Mode->CrtcHSyncEnd, pDisplay_Mode->CrtcHTotal);
                    pDisplay_Mode->type = M_T_DRIVER | M_T_PREFERRED;
                } else {
                    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                "Out of memory. Size: %zu bytes\n", sizeof(DisplayModeRec));
                }
            } else {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                            "Invalid Flat Panel Screen Resolution: "
                            "%dx%d\n",
                            pVIAFP->NativeWidth, pVIAFP->NativeHeight);
            }
        } else {
            pDisplay_Mode = xf86OutputGetEDIDModes(output);
        }
    }
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_fp_get_modes.\n"));
    return pDisplay_Mode;
}

#ifdef RANDR_12_INTERFACE
static Bool
via_fp_set_property(xf86OutputPtr output, Atom property,
                        RRPropertyValuePtr value)
{
    return FALSE;
}

static Bool
via_fp_get_property(xf86OutputPtr output, Atom property)
{
    return FALSE;
}
#endif

static void
via_fp_destroy(xf86OutputPtr output)
{
    if (output->driver_private)
        free(output->driver_private);
    output->driver_private = NULL;
}

static const xf86OutputFuncsRec via_fp_funcs = {
    .create_resources   = via_fp_create_resources,
    .dpms               = via_fp_dpms,
    .save               = via_fp_save,
    .restore            = via_fp_restore,
    .mode_valid         = via_fp_mode_valid,
    .mode_fixup         = via_fp_mode_fixup,
    .prepare            = via_fp_prepare,
    .commit             = via_fp_commit,
    .mode_set           = via_fp_mode_set,
    .detect             = via_fp_detect,
    .get_modes          = via_fp_get_modes,
#ifdef RANDR_12_INTERFACE
    .set_property       = via_fp_set_property,
#endif
#ifdef RANDR_13_INTERFACE
    .get_property       = via_fp_get_property,
#endif
    .destroy            = via_fp_destroy
};

void
viaFPProbe(ScrnInfoPtr pScrn)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    VIAPtr pVia = VIAPTR(pScrn);
    VIADisplayPtr pVIADisplay = pVia->pVIADisplay;
    CARD8 sr12, sr13, sr5a;
    CARD8 cr3b;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaFPProbe.\n"));

    sr12 = hwp->readSeq(hwp, 0x12);
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "SR12: 0x%02X\n", sr12));
    sr13 = hwp->readSeq(hwp, 0x13);
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "SR13: 0x%02X\n", sr13));
    cr3b = hwp->readCrtc(hwp, 0x3B);
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "CR3B: 0x%02X\n", sr13));

    /* Detect the presence of FPs. */
    switch (pVia->Chipset) {
    case VIA_CLE266:
        if ((sr12 & BIT(4)) || (cr3b & BIT(3))) {
            pVIADisplay->intFP1Presence = TRUE;
            pVIADisplay->intFP1DIPort = VIA_DI_PORT_DIP0;
            pVIADisplay->intFP2Presence = FALSE;
            pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
        } else {
            pVIADisplay->intFP1Presence = FALSE;
            pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
            pVIADisplay->intFP2Presence = FALSE;
            pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
        }

        break;
    case VIA_KM400:
    case VIA_P4M800PRO:
    case VIA_PM800:
    case VIA_K8M800:
        /* 3C5.13[3] - DVP0D8 pin strapping
         *             0: AGP pins are used for AGP
         *             1: AGP pins are used by FPDP
         *             (Flat Panel Display Port) */
        if ((sr13 & BIT(3)) && (cr3b & BIT(1))) {
            pVIADisplay->intFP1Presence = TRUE;
            pVIADisplay->intFP1DIPort = VIA_DI_PORT_FPDPHIGH
                                            | VIA_DI_PORT_FPDPLOW;
            pVIADisplay->intFP2Presence = FALSE;
            pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;

        } else {
            pVIADisplay->intFP1Presence = FALSE;
            pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
            pVIADisplay->intFP2Presence = FALSE;
            pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
        }

        break;
    case VIA_P4M890:
    case VIA_K8M890:
    case VIA_P4M900:
        if (cr3b & BIT(1)) {

            /* 3C5.12[4] - DVP0D4 pin strapping
             *             0: 12-bit FPDP (Flat Panel Display Port)
             *             1: 24-bit FPDP (Flat Panel Display Port) */
            if (sr12 & BIT(4)) {
                pVIADisplay->intFP1Presence = TRUE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_FPDPLOW
                                            | VIA_DI_PORT_FPDPHIGH;
                pVIADisplay->intFP2Presence = FALSE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
            } else {
                pVIADisplay->intFP1Presence = TRUE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_FPDPLOW;
                pVIADisplay->intFP2Presence = FALSE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
            }
        }

        break;
    case VIA_CX700:
    case VIA_VX800:
    case VIA_VX855:
    case VIA_VX900:
        sr5a = hwp->readSeq(hwp, 0x5A);

        /* Setting SR5A[0] to 1.
         * This allows the reading out the alternative
         * pin strapping information from SR12 and SR13. */
        ViaSeqMask(hwp, 0x5A, BIT(0), BIT(0));

        sr13 = hwp->readSeq(hwp, 0x13);
        DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                            "SR13: 0x%02X\n", sr13));

        if (cr3b & BIT(1)) {
            if (pVia->isVIANanoBook) {
                pVIADisplay->intFP1Presence = FALSE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
                pVIADisplay->intFP2Presence = TRUE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_LVDS2;

            /* 3C5.13[7:6] - Integrated LVDS / DVI Mode Select
             *               (DVP1D15-14 pin strapping)
             *               00: LVDS1 + LVDS2
             *               01: DVI + LVDS2
             *               10: Dual LVDS Channel (High Resolution Panel)
             *               11: One DVI only (decrease the clock jitter) */
            } else if ((!(sr13 & BIT(7))) && (!(sr13 & BIT(6)))) {
                pVIADisplay->intFP1Presence = TRUE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_LVDS1;
                pVIADisplay->intFP2Presence = TRUE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_LVDS2;
            } else if ((!(sr13 & BIT(7))) && (sr13 & BIT(6))) {
                pVIADisplay->intFP1Presence = FALSE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
                pVIADisplay->intFP2Presence = TRUE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_LVDS2;
            } else if ((sr13 & BIT(7)) && (!(sr13 & BIT(6)))) {
                pVIADisplay->intFP1Presence = TRUE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_LVDS1
                                                | VIA_DI_PORT_LVDS2;
                pVIADisplay->intFP2Presence = FALSE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
            } else {
                pVIADisplay->intFP1Presence = FALSE;
                pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
                pVIADisplay->intFP2Presence = FALSE;
                pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
            }
        } else {
            pVIADisplay->intFP1Presence = FALSE;
            pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
            pVIADisplay->intFP2Presence = FALSE;
            pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
        }

        hwp->writeSeq(hwp, 0x5A, sr5a);
        break;
    default:
        pVIADisplay->intFP1Presence = FALSE;
        pVIADisplay->intFP1DIPort = VIA_DI_PORT_NONE;
        pVIADisplay->intFP2Presence = FALSE;
        pVIADisplay->intFP2DIPort = VIA_DI_PORT_NONE;
        break;
    }

    pVIADisplay->intFP1I2CBus = VIA_I2C_NONE;
    pVIADisplay->intFP2I2CBus = VIA_I2C_NONE;

    if ((pVIADisplay->intFP1Presence)
        && (!(pVIADisplay->mappedI2CBus & VIA_I2C_BUS2))) {
        pVIADisplay->intFP1I2CBus = VIA_I2C_BUS2;
        pVIADisplay->mappedI2CBus |= VIA_I2C_BUS2;
    }

    if ((pVIADisplay->intFP2Presence)
        && (!(pVIADisplay->mappedI2CBus & VIA_I2C_BUS2))) {
        pVIADisplay->intFP2I2CBus = VIA_I2C_BUS2;
        pVIADisplay->mappedI2CBus |= VIA_I2C_BUS2;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPProbe.\n"));
}

void
viaFPInit(ScrnInfoPtr pScrn)
{
    xf86OutputPtr output;
    VIAPtr pVia = VIAPTR(pScrn);
    VIADisplayPtr pVIADisplay = pVia->pVIADisplay;
    VIAFPPtr pVIAFP;
    char outputNameBuffer[32];

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entering viaFPInit.\n"));

    if (pVIADisplay->intFP1Presence) {
        pVIAFP = (VIAFPPtr) xnfcalloc(1, sizeof(VIAFPRec));
        if (!pVIAFP) {
            DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                "Failed to allocate private storage for "
                                "FP.\n"));
            goto exit;
        }

        /* The code to dynamically designate a particular FP (i.e., FP-1,
         * FP-2, etc.) for xrandr was borrowed from xf86-video-r128 DDX. */
        sprintf(outputNameBuffer, "FP-%d", (pVIADisplay->numberFP + 1));
        output = xf86OutputCreate(pScrn, &via_fp_funcs, outputNameBuffer);
        if (!output) {
            free(pVIAFP);
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "Failed to allocate X Server display output record for "
                        "FP.\n");
            goto exit;
        }

        /* Increment the number of FP connectors. */
        pVIADisplay->numberFP++;

        pVIAFP->diPort = pVIADisplay->intFP1DIPort;

        /* Hint about which I2C bus to access for obtaining EDID. */
        pVIAFP->i2cBus = pVIADisplay->intFP1I2CBus;

        output->driver_private = pVIAFP;

        output->possible_crtcs = BIT(1) | BIT(0);

        output->possible_clones = 0;
        output->interlaceAllowed = FALSE;
        output->doubleScanAllowed = FALSE;

        if (pVia->IsOLPCXO15) {
            output->mm_height = 152;
            output->mm_width = 114;
        }
    }

    if (pVIADisplay->intFP2Presence) {
        pVIAFP = (VIAFPPtr) xnfcalloc(1, sizeof(VIAFPRec));
        if (!pVIAFP) {
            DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                "Failed to allocate private storage for "
                                "FP.\n"));
            goto exit;
        }

        /* The code to dynamically designate a particular FP (i.e., FP-1,
         * FP-2, etc.) for xrandr was borrowed from xf86-video-r128 DDX. */
        sprintf(outputNameBuffer, "FP-%d", (pVIADisplay->numberFP + 1));
        output = xf86OutputCreate(pScrn, &via_fp_funcs, outputNameBuffer);
        if (!output) {
            free(pVIAFP);
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "Failed to allocate X Server display output record for "
                        "FP.\n");
            goto exit;
        }

        /* Increment the number of FP connectors. */
        pVIADisplay->numberFP++;

        pVIAFP->diPort = pVIADisplay->intFP2DIPort;

        /* Hint about which I2C bus to access for obtaining EDID. */
        pVIAFP->i2cBus = pVIADisplay->intFP2I2CBus;

        output->driver_private = pVIAFP;

        output->possible_crtcs = BIT(1) | BIT(0);

        output->possible_clones = 0;
        output->interlaceAllowed = FALSE;
        output->doubleScanAllowed = FALSE;
    }

exit:
    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaFPInit.\n"));
}
