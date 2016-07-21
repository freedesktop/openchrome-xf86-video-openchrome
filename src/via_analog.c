/*
 * Copyright 2005-2016 The OpenChrome Project
 *                     [http://www.freedesktop.org/wiki/Openchrome]
 * Copyright 2004-2005 The Unichrome Project  [unichrome.sf.net]
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
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
 * via_analog.c
 *
 * Handles the initialization and management of analog VGA related
 * resources.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "via_driver.h"
#include <unistd.h>


static void
ViaPrintMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    xf86PrintModeline(pScrn->scrnIndex, mode);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHDisplay: 0x%x\n",
               mode->CrtcHDisplay);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHBlankStart: 0x%x\n",
               mode->CrtcHBlankStart);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHSyncStart: 0x%x\n",
               mode->CrtcHSyncStart);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHSyncEnd: 0x%x\n",
               mode->CrtcHSyncEnd);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHBlankEnd: 0x%x\n",
               mode->CrtcHBlankEnd);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHTotal: 0x%x\n",
               mode->CrtcHTotal);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcHSkew: 0x%x\n",
               mode->CrtcHSkew);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVDisplay: 0x%x\n",
               mode->CrtcVDisplay);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVBlankStart: 0x%x\n",
               mode->CrtcVBlankStart);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVSyncStart: 0x%x\n",
               mode->CrtcVSyncStart);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVSyncEnd: 0x%x\n",
               mode->CrtcVSyncEnd);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVBlankEnd: 0x%x\n",
               mode->CrtcVBlankEnd);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CrtcVTotal: 0x%x\n",
               mode->CrtcVTotal);

}

/*
 * Enables or disables analog VGA output by controlling DAC
 * (Digital to Analog Converter) output state.
 */
static void
viaAnalogOutput(ScrnInfoPtr pScrn, Bool outputState)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaAnalogOutput.\n"));

    /* This register controls analog VGA DAC output state. */
    /* 3X5.47[2] - DACOFF Backdoor Register
     *             0: DAC on
     *             1: DAC off */
    ViaCrtcMask(hwp, 0x47, outputState ? 0x00 : 0x04, 0x04);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Analog VGA Output: %s\n",
                outputState ? "On" : "Off");

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaAnalogOutput.\n"));
}

/*
 * Specifies IGA1 or IGA2 for analog VGA DAC source.
 */
static void
viaAnalogSource(ScrnInfoPtr pScrn, CARD8 displaySource)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    CARD8 value = displaySource;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaAnalogSource.\n"));

    ViaSeqMask(hwp, 0x16, value << 6, 0x40);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Analog VGA Output Source: IGA%d\n",
                (value & 0x01) + 1);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaAnalogSource.\n"));
}

/*
 * Intializes analog VGA related registers.
 */
static void
viaAnalogInit(ScrnInfoPtr pScrn)
{
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    VIAPtr pVia = VIAPTR(pScrn);

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered viaAnalogInit.\n"));

    /* 3X5.37[7]   - DAC Power Save Control 1
     *               0: Depend on Rx3X5.37[5:4] setting
     *               1: DAC always goes into power save mode
     * 3X5.37[6]   - DAC Power Down Control
     *               0: Depend on Rx3X5.47[2] setting
     *               1: DAC never goes to power down mode
     * 3X5.37[5:4] - DAC Power Save Control 2
     *               00: DAC never goes to power save mode
     *               01: DAC goes to power save mode by line
     *               10: DAC goes to power save mode by frame
     *               11: DAC goes to power save mode by line and frame
     * 3X5.37[3]   - DAC PEDESTAL Control
     * 3X5.37[2:0] - DAC Factor
     *               (Default: 100) */
    ViaCrtcMask(hwp, 0x37, 0x04, 0xFF);

    switch (pVia->Chipset) {
    case VIA_CX700:
    case VIA_VX800:
    case VIA_VX855:
    case VIA_VX900:
        /* 3C5.5E[0] - CRT DACOFF Setting
         *             1: CRT DACOFF controlled by 3C5.01[5] */
        ViaSeqMask(hwp, 0x5E, 0x01, 0x01);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting viaAnalogInit.\n"));
}

static void
via_analog_create_resources(xf86OutputPtr output)
{
}

#ifdef RANDR_12_INTERFACE
static Bool
via_analog_set_property(xf86OutputPtr output, Atom property,
                        RRPropertyValuePtr value)
{
    return TRUE;
}

static Bool
via_analog_get_property(xf86OutputPtr output, Atom property)
{
    return FALSE;
}
#endif

static void
via_analog_dpms(xf86OutputPtr output, int mode)
{
    ScrnInfoPtr pScrn = output->scrn;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_analog_dpms.\n"));

    switch (mode) {
    case DPMSModeOn:
        viaAnalogOutput(pScrn, TRUE);
        break;
    case DPMSModeStandby:
    case DPMSModeSuspend:
    case DPMSModeOff:
        viaAnalogOutput(pScrn, FALSE);
        break;
    default:
        break;
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_analog_dpms.\n"));
}

static void
via_analog_save(xf86OutputPtr output)
{
}

static void
via_analog_restore(xf86OutputPtr output)
{
}

static int
via_analog_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    ScrnInfoPtr pScrn = output->scrn;

    if (!ViaModeDotClockTranslate(pScrn, pMode))
        return MODE_NOCLOCK;
    return MODE_OK;
}

static Bool
via_analog_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
                      DisplayModePtr adjusted_mode)
{
    return TRUE;
}

static void
via_analog_prepare(xf86OutputPtr output)
{
    via_analog_dpms(output, DPMSModeOff);
}

static void
via_analog_commit(xf86OutputPtr output)
{
    via_analog_dpms(output, DPMSModeOn);
}

static void
via_analog_mode_set(xf86OutputPtr output, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode)
{
    ScrnInfoPtr pScrn = output->scrn;
    vgaHWPtr hwp = VGAHWPTR(pScrn);
    drmmode_crtc_private_ptr iga = output->crtc->driver_private;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_analog_mode_set.\n"));

    viaAnalogInit(pScrn);

    if (output->crtc) {
        viaAnalogSource(pScrn, iga->index ? 0x01 : 0x00);
    }

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_analog_mode_set.\n"));
}

static xf86OutputStatus
via_analog_detect(xf86OutputPtr output)
{
    xf86OutputStatus status = XF86OutputStatusDisconnected;
    ScrnInfoPtr pScrn = output->scrn;
    VIAPtr pVia = VIAPTR(pScrn);
    xf86MonPtr mon;

    /* Probe I2C Bus 1 to see if a VGA monitor is connected. */
    xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                "Probing for a VGA monitor on I2C Bus 1.\n");
    mon = xf86OutputGetEDID(output, pVia->pI2CBus1);
    if (mon && (!mon->features.input_type)) {
        xf86OutputSetEDID(output, mon);
        status = XF86OutputStatusConnected;
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "Detected a VGA monitor on I2C Bus 1.\n");
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "Did not detect a VGA monitor on I2C Bus 1.\n");

        /* Probe I2C Bus 2 to see if a VGA monitor is connected. */
        xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                    "Probing for a VGA monitor on I2C Bus 2.\n");
        mon = xf86OutputGetEDID(output, pVia->pI2CBus2);
        if (mon && (!mon->features.input_type)) {
            xf86OutputSetEDID(output, mon);
            status = XF86OutputStatusConnected;
            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                        "Detected a VGA monitor on I2C Bus 2.\n");
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                        "Did not detect a VGA monitor on I2C Bus 2.\n");

            /* Perform manual detection of a VGA monitor since */
            /* it was not detected via I2C buses. */
            xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                        "Now perform manual detection of a VGA "
                        "monitor.\n");
            vgaHWPtr hwp = VGAHWPTR(pScrn);
            CARD8 SR01 = hwp->readSeq(hwp, 0x01);
            CARD8 SR40 = hwp->readSeq(hwp, 0x40);
            CARD8 CR36 = hwp->readCrtc(hwp, 0x36);

            /* We have to power on the display to detect it */
            ViaSeqMask(hwp, 0x01, 0x00, 0x20);
            ViaCrtcMask(hwp, 0x36, 0x00, 0xF0);

            /* Wait for vblank */
            usleep(16);

            /* Detect the load on pins */
            ViaSeqMask(hwp, 0x40, 0x80, 0x80);

            if ((VIA_CX700 == pVia->Chipset) ||
                (VIA_VX800 == pVia->Chipset) ||
                (VIA_VX855 == pVia->Chipset) ||
                (VIA_VX900 == pVia->Chipset))
                ViaSeqMask(hwp, 0x40, 0x00, 0x80);

            if (ViaVgahwIn(hwp, 0x3C2) & 0x20) {
                status = XF86OutputStatusConnected;
                xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
                            "Detected a VGA monitor using manual "
                            "detection method.\n");
            }

            if ((VIA_CX700 == pVia->Chipset) ||
                (VIA_VX800 == pVia->Chipset) ||
                (VIA_VX855 == pVia->Chipset) ||
                (VIA_VX900 == pVia->Chipset))
                ViaSeqMask(hwp, 0x40, 0x00, 0x80);

            /* Restore previous state */
            hwp->writeSeq(hwp, 0x40, SR40);
            hwp->writeSeq(hwp, 0x01, SR01);
            hwp->writeCrtc(hwp, 0x36, CR36);
        }
    }

    return status;
}

static void
via_analog_destroy(xf86OutputPtr output)
{
}

static const xf86OutputFuncsRec via_analog_funcs = {
    .create_resources   = via_analog_create_resources,
#ifdef RANDR_12_INTERFACE
    .set_property       = via_analog_set_property,
#endif
#ifdef RANDR_13_INTERFACE
    .get_property       = via_analog_get_property,
#endif
    .dpms               = via_analog_dpms,
    .save               = via_analog_save,
    .restore            = via_analog_restore,
    .mode_valid         = via_analog_mode_valid,
    .mode_fixup         = via_analog_mode_fixup,
    .prepare            = via_analog_prepare,
    .commit             = via_analog_commit,
    .mode_set           = via_analog_mode_set,
    .detect             = via_analog_detect,
    .get_modes          = xf86OutputGetEDIDModes,
    .destroy            = via_analog_destroy,
};

void
via_analog_init(ScrnInfoPtr pScrn)
{
    VIAPtr pVia = VIAPTR(pScrn);
    VIABIOSInfoPtr pBIOSInfo = pVia->pBIOSInfo;
    xf86OutputPtr output = NULL;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Entered via_analog_init.\n"));

    if (!pVia->pI2CBus1 || !pVia->pI2CBus2) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "I2C Bus 1 or I2C Bus 2 does not exist.\n");
        DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                            "Exiting via_analog_init.\n"));
        return;
    }

    output = xf86OutputCreate(pScrn, &via_analog_funcs, "VGA-1");

    output->possible_crtcs = 0x3;
    output->possible_clones = 0;
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = FALSE;
    pBIOSInfo->analog = output;

    DEBUG(xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Exiting via_analog_init.\n"));
}
