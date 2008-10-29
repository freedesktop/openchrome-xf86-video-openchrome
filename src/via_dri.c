/*
 * Copyright 2005-2008 The Openchrome Project  [openchrome.org]
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Priv.h"

#include "xf86PciInfo.h"
#include "xf86Pci.h"

#define _XF86DRI_SERVER_
#include "GL/glxtokens.h"
#include "sarea.h"

#include "via.h"
#include "via_driver.h"
#include "via_drm.h"
#include "via_dri.h"
#include "via_id.h"
#include "xf86drm.h"

#ifndef DRIINFO_MAJOR_VERSION
#define DRIINFO_MAJOR_VERSION 4
#endif
#ifndef DRIINFO_MINOR_VERSION
#define DRIINFO_MINOR_VERSION 0
#endif

#define VIDEO  0
extern void GlxSetVisualConfigs(int nconfigs,
                                __GLXvisualConfig * configs,
                                void **configprivs);

typedef struct
{
    int major;
    int minor;
    int patchlevel;
} ViaDRMVersion;

static char VIAKernelDriverName[] = "via";
static char VIAClientDriverName[] = "unichrome";
static const ViaDRMVersion drmExpected = { 4, 0, 0 };
static const ViaDRMVersion drmCompat = { 4, 0, 0 };

static Bool VIAInitVisualConfigs(ScreenPtr pScreen);
static Bool VIADRIFBInit(ScreenPtr pScreen, VIAPtr pVia);
static Bool VIADRIKernelInit(ScreenPtr pScreen, VIAPtr pVia);
static Bool VIADRIMapInit(ScreenPtr pScreen, VIAPtr pVia);

static Bool VIACreateContext(ScreenPtr pScreen, VisualPtr visual,
                             drm_context_t hwContext, void *pVisualConfigPriv,
                             DRIContextType contextStore);
static void VIADestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
                              DRIContextType contextStore);
static void VIADRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
                              DRIContextType readContextType,
                              void *readContextStore,
                              DRIContextType writeContextType,
                              void *writeContextStore);
static void VIADRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index);
static void VIADRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
                              RegionPtr prgnSrc, CARD32 index);


static void
VIADRIIrqInit(ScrnInfoPtr pScrn, VIADRIPtr pVIADRI)
{
    VIAPtr pVia = VIAPTR(pScrn);

    pVIADRI->irqEnabled = drmGetInterruptFromBusID
            (pVia->drmFD,
#ifdef XSERVER_LIBPCIACCESS
             ((pVia->PciInfo->domain << 8) | pVia->PciInfo->bus),
             pVia->PciInfo->dev, pVia->PciInfo->func
#else
             ((pciConfigPtr)pVia->PciInfo->thisCard)->busnum,
             ((pciConfigPtr)pVia->PciInfo->thisCard)->devnum,
             ((pciConfigPtr)pVia->PciInfo->thisCard)->funcnum
#endif
            );
    if ((drmCtlInstHandler(pVia->drmFD, pVIADRI->irqEnabled))) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "[drm] Failure adding IRQ handler. "
                   "Falling back to IRQ-free operation.\n");
        pVIADRI->irqEnabled = 0;
    }

    if (pVIADRI->irqEnabled)
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "[drm] IRQ handler installed, using IRQ %d.\n",
                   pVIADRI->irqEnabled);
}

static void
VIADRIIrqExit(ScrnInfoPtr pScrn, VIADRIPtr pVIADRI)
{
    VIAPtr pVia = VIAPTR(pScrn);

    if (pVIADRI->irqEnabled) {
        if (drmCtlUninstHandler(pVia->drmFD)) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "[drm] IRQ handler uninstalled.\n");
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "[drm] Could not uninstall IRQ handler.\n");
        }
    }
}

static Bool
VIADRIFBInit(ScreenPtr pScreen, VIAPtr pVia)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int FBSize = pVia->driSize;
    int FBOffset;
    VIADRIPtr pVIADRI = pVia->pDRIInfo->devPrivate;

    if (FBSize < pVia->Bpl) {
        xf86DrvMsg(pScreen->myNum, X_ERROR,
                   "[drm] No DRM framebuffer heap available.\n"
                   "[drm] Please increase the frame buffer\n"
                   "[drm] memory area in the BIOS. Disabling DRI.\n");
        return FALSE;
    }
    if (FBSize < 3 * (pScrn->virtualY * pVia->Bpl)) {
        xf86DrvMsg(pScreen->myNum, X_WARNING,
                   "[drm] The DRM heap and pixmap cache memory may be too\n"
                   "[drm] small for optimal performance. Please increase\n"
                   "[drm] the frame buffer memory area in the BIOS.\n");
    }

    pVia->driOffScreenMem.pool = 0;
    if (Success != viaOffScreenLinear(&pVia->driOffScreenMem, pScrn, FBSize)) {
        xf86DrvMsg(pScreen->myNum, X_ERROR,
                   "[drm] Failed to allocate offscreen frame buffer area.\n");
        return FALSE;
    }

    FBOffset = pVia->driOffScreenMem.base;

    pVIADRI->fbOffset = FBOffset;
    pVIADRI->fbSize = FBSize;

    {
        struct drm_via_fb fb;

        fb.offset = FBOffset;
        fb.size = FBSize;

        if (drmCommandWrite(pVia->drmFD, DRM_VIA_FB_INIT, &fb,
                            sizeof(struct drm_via_fb)) < 0) {
            xf86DrvMsg(pScreen->myNum, X_ERROR,
                       "[drm] Failed to initialize frame buffer area.\n");
            return FALSE;
        } else {
            xf86DrvMsg(pScreen->myNum, X_INFO,
                       "[drm] Using %d bytes for DRM memory heap.\n", FBSize);
            return TRUE;
        }
    }
}

static Bool
VIAInitVisualConfigs(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
    int numConfigs = 0;
    __GLXvisualConfig *pConfigs = 0;
    VIAConfigPrivPtr pVIAConfigs = 0;
    VIAConfigPrivPtr *pVIAConfigPtrs = 0;
    int i, db, stencil, accum;

    if (pScrn->bitsPerPixel == 16 || pScrn->bitsPerPixel == 32) {
        numConfigs = 12;
        if (!(pConfigs = (__GLXvisualConfig *)
                         xcalloc(sizeof(__GLXvisualConfig), numConfigs)))
            return FALSE;
        if (!(pVIAConfigs = (VIAConfigPrivPtr)
                            xcalloc(sizeof(VIAConfigPrivRec), numConfigs))) {
            xfree(pConfigs);
            return FALSE;
        }
        if (!(pVIAConfigPtrs = (VIAConfigPrivPtr *)
                               xcalloc(sizeof(VIAConfigPrivPtr), numConfigs))) {
            xfree(pConfigs);
            xfree(pVIAConfigs);
            return FALSE;
        }
        for (i = 0; i < numConfigs; i++)
            pVIAConfigPtrs[i] = &pVIAConfigs[i];

        i = 0;
        for (accum = 0; accum <= 1; accum++) {
            /* 32bpp depth buffer disabled, as Mesa has limitations */
            for (stencil = 0; stencil <= 2; stencil++) {
                for (db = 0; db <= 1; db++) {
                    pConfigs[i].vid = -1;
                    pConfigs[i].class = -1;
                    pConfigs[i].rgba = TRUE;
                    pConfigs[i].redSize = -1;
                    pConfigs[i].greenSize = -1;
                    pConfigs[i].blueSize = -1;
                    pConfigs[i].redMask = -1;
                    pConfigs[i].greenMask = -1;
                    pConfigs[i].blueMask = -1;
                    if (pScrn->bitsPerPixel == 32) {
                        pConfigs[i].alphaSize = 8;
                        pConfigs[i].alphaMask = 0xFF000000;
                    } else {
                        pConfigs[i].alphaSize = 0;
                        pConfigs[i].alphaMask = 0;
                    }

                    if (accum) {
                        pConfigs[i].accumRedSize = 16;
                        pConfigs[i].accumGreenSize = 16;
                        pConfigs[i].accumBlueSize = 16;
                        if (pScrn->bitsPerPixel == 32)
                            pConfigs[i].accumAlphaSize = 16;
                        else
                            pConfigs[i].accumAlphaSize = 0;
                    } else {
                        pConfigs[i].accumRedSize = 0;
                        pConfigs[i].accumGreenSize = 0;
                        pConfigs[i].accumBlueSize = 0;
                        pConfigs[i].accumAlphaSize = 0;
                    }
                    if (!db)
                        pConfigs[i].doubleBuffer = TRUE;
                    else
                        pConfigs[i].doubleBuffer = FALSE;

                    pConfigs[i].stereo = FALSE;
                    pConfigs[i].bufferSize = -1;

                    switch (stencil) {
                        case 0:
                            pConfigs[i].depthSize = 24;
                            pConfigs[i].stencilSize = 8;
                            break;
                        case 1:
                            pConfigs[i].depthSize = 16;
                            pConfigs[i].stencilSize = 0;
                            break;
                        case 2:
                            pConfigs[i].depthSize = 0;
                            pConfigs[i].stencilSize = 0;
                            break;
                        case 3:
                            pConfigs[i].depthSize = 32;
                            pConfigs[i].stencilSize = 0;
                            break;
                    }

                    pConfigs[i].auxBuffers = 0;
                    pConfigs[i].level = 0;
                    if (accum)
                        pConfigs[i].visualRating = GLX_SLOW_VISUAL_EXT;
                    else
                        pConfigs[i].visualRating = GLX_NONE_EXT;
                    pConfigs[i].transparentPixel = GLX_NONE_EXT;
                    pConfigs[i].transparentRed = 0;
                    pConfigs[i].transparentGreen = 0;
                    pConfigs[i].transparentBlue = 0;
                    pConfigs[i].transparentAlpha = 0;
                    pConfigs[i].transparentIndex = 0;
                    i++;
                }
            }
        }

        if (i != numConfigs) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[dri] Incorrect "
                       "initialization of visuals.  Disabling DRI.\n");
            return FALSE;
        }
    }

    pVia->numVisualConfigs = numConfigs;
    pVia->pVisualConfigs = pConfigs;
    pVia->pVisualConfigsPriv = pVIAConfigs;
    GlxSetVisualConfigs(numConfigs, pConfigs, (void **)pVIAConfigPtrs);

    return TRUE;
}

Bool
VIADRIScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
    DRIInfoPtr pDRIInfo;
    VIADRIPtr pVIADRI;
    drmVersionPtr drmVer;

    /* If symbols or version check fails, we still want this to be NULL. */
    pVia->pDRIInfo = NULL;

    /* Check that the GLX, DRI, and DRM modules have been loaded by testing
     * for canonical symbols in each module. */
    if (!xf86LoaderCheckSymbol("GlxSetVisualConfigs"))
        return FALSE;
    if (!xf86LoaderCheckSymbol("drmAvailable"))
        return FALSE;
    if (!xf86LoaderCheckSymbol("DRIQueryVersion")) {
        xf86DrvMsg(pScreen->myNum, X_ERROR,
                   "[dri] VIADRIScreenInit failed (libdri.a is too old).\n");
        return FALSE;
    }

    /* Check the DRI version. */
    {
        int major, minor, patch;

        DRIQueryVersion(&major, &minor, &patch);
        if (major != DRIINFO_MAJOR_VERSION || minor < DRIINFO_MINOR_VERSION) {
            xf86DrvMsg(pScreen->myNum, X_ERROR,
                       "[dri] VIADRIScreenInit failed -- version mismatch.\n"
                       "[dri] libdri is %d.%d.%d, but %d.%d.x is needed.\n"
                       "[dri] Disabling DRI.\n",
                       major, minor, patch,
                       DRIINFO_MAJOR_VERSION, DRIINFO_MINOR_VERSION);
            return FALSE;
        }
    }

    pVia->pDRIInfo = DRICreateInfoRec();
    if (!pVia->pDRIInfo)
        return FALSE;

    pDRIInfo = pVia->pDRIInfo;
    pDRIInfo->drmDriverName = VIAKernelDriverName;
    pDRIInfo->clientDriverName = VIAClientDriverName;
    pDRIInfo->busIdString = xalloc(64);
    sprintf(pDRIInfo->busIdString, "PCI:%d:%d:%d",
#ifdef XSERVER_LIBPCIACCESS
            ((pVia->PciInfo->domain << 8) | pVia->PciInfo->bus),
            pVia->PciInfo->dev, pVia->PciInfo->func
#else
            ((pciConfigPtr)pVia->PciInfo->thisCard)->busnum,
            ((pciConfigPtr)pVia->PciInfo->thisCard)->devnum,
            ((pciConfigPtr)pVia->PciInfo->thisCard)->funcnum
#endif
           );
    pDRIInfo->ddxDriverMajorVersion = VIA_DRIDDX_VERSION_MAJOR;
    pDRIInfo->ddxDriverMinorVersion = VIA_DRIDDX_VERSION_MINOR;
    pDRIInfo->ddxDriverPatchVersion = VIA_DRIDDX_VERSION_PATCH;
#if (DRIINFO_MAJOR_VERSION == 5)
    pDRIInfo->frameBufferPhysicalAddress = (pointer) pVia->FrameBufferBase;
#else
    pDRIInfo->frameBufferPhysicalAddress = pVia->FrameBufferBase;
#endif
    pDRIInfo->frameBufferSize = pVia->videoRambytes;

    pDRIInfo->frameBufferStride = (pScrn->displayWidth *
                                   pScrn->bitsPerPixel / 8);
    pDRIInfo->ddxDrawableTableEntry = VIA_MAX_DRAWABLES;

    if (SAREA_MAX_DRAWABLES < VIA_MAX_DRAWABLES)
        pDRIInfo->maxDrawableTableEntry = SAREA_MAX_DRAWABLES;
    else
        pDRIInfo->maxDrawableTableEntry = VIA_MAX_DRAWABLES;

#ifdef NOT_DONE
    /* FIXME: need to extend DRI protocol to pass this size back to client
     * for SAREA mapping that includes a device private record. */
    pDRIInfo->SAREASize = ((sizeof(XF86DRISAREARec) + 0xfff) & 0x1000); /* round to page */
    /* + shared memory device private rec */
#else
    /* For now the mapping works by using a fixed size defined
     * in the SAREA header. */
    if (sizeof(XF86DRISAREARec) + sizeof(struct drm_via_sarea) > SAREA_MAX) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Data does not fit in SAREA\n");
        DRIDestroyInfoRec(pVia->pDRIInfo);
        pVia->pDRIInfo = NULL;
        return FALSE;
    }
    pDRIInfo->SAREASize = SAREA_MAX;
#endif

    if (!(pVIADRI = (VIADRIPtr) xcalloc(sizeof(VIADRIRec), 1))) {
        DRIDestroyInfoRec(pVia->pDRIInfo);
        pVia->pDRIInfo = NULL;
        return FALSE;
    }
    pDRIInfo->devPrivate = pVIADRI;
    pDRIInfo->devPrivateSize = sizeof(VIADRIRec);
    pDRIInfo->contextSize = sizeof(VIADRIContextRec);

    pDRIInfo->CreateContext = VIACreateContext;
    pDRIInfo->DestroyContext = VIADestroyContext;
    pDRIInfo->SwapContext = VIADRISwapContext;
    pDRIInfo->InitBuffers = VIADRIInitBuffers;
    pDRIInfo->MoveBuffers = VIADRIMoveBuffers;
    pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;

    if (!DRIScreenInit(pScreen, pDRIInfo, &pVia->drmFD)) {
        xf86DrvMsg(pScreen->myNum, X_ERROR,
                   "[dri] DRIScreenInit failed.  Disabling DRI.\n");
        xfree(pDRIInfo->devPrivate);
        pDRIInfo->devPrivate = NULL;
        DRIDestroyInfoRec(pVia->pDRIInfo);
        pVia->pDRIInfo = NULL;
        pVia->drmFD = -1;
        return FALSE;
    }

    if (NULL == (drmVer = drmGetVersion(pVia->drmFD))) {
        VIADRICloseScreen(pScreen);
        return FALSE;
    }
    pVia->drmVerMajor = drmVer->version_major;
    pVia->drmVerMinor = drmVer->version_minor;
    pVia->drmVerPL = drmVer->version_patchlevel;

    if ((drmVer->version_major < drmExpected.major) ||
        (drmVer->version_major > drmCompat.major) ||
        ((drmVer->version_major == drmExpected.major) &&
         (drmVer->version_minor < drmExpected.minor))) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "[dri] Kernel drm is not compatible with this driver.\n"
                   "[dri] Kernel drm version is %d.%d.%d, "
                   "and I can work with versions %d.%d.x - %d.x.x.\n"
                   "[dri] Update either this 2D driver or your kernel DRM. "
                   "Disabling DRI.\n",
                   drmVer->version_major, drmVer->version_minor,
                   drmVer->version_patchlevel,
                   drmExpected.major, drmExpected.minor, drmCompat.major);
        drmFreeVersion(drmVer);
        VIADRICloseScreen(pScreen);
        return FALSE;
    }
    drmFreeVersion(drmVer);

    if (!(VIAInitVisualConfigs(pScreen))) {
        VIADRICloseScreen(pScreen);
        return FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[dri] visual configs initialized.\n");

    /* DRIScreenInit doesn't add all the common mappings.
     * Add additional mappings here. */
    if (!VIADRIMapInit(pScreen, pVia)) {
        VIADRICloseScreen(pScreen);
        return FALSE;
    }
    pVIADRI->regs.size = VIA_MMIO_REGSIZE;
    pVIADRI->regs.handle = pVia->registerHandle;
    xf86DrvMsg(pScreen->myNum, X_INFO, "[drm] mmio Registers = 0x%08lx\n",
               (unsigned long)pVIADRI->regs.handle);

    pVIADRI->drixinerama = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[dri] mmio mapped.\n");

    return TRUE;
}

void
VIADRICloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
    VIADRIPtr pVIADRI;


    DRICloseScreen(pScreen);
    VIAFreeLinear(&pVia->driOffScreenMem);

    if (pVia->pDRIInfo) {
        if ((pVIADRI = (VIADRIPtr) pVia->pDRIInfo->devPrivate)) {
            VIADRIIrqExit(pScrn, pVIADRI);
            xfree(pVIADRI);
            pVia->pDRIInfo->devPrivate = NULL;
        }
        DRIDestroyInfoRec(pVia->pDRIInfo);
        pVia->pDRIInfo = NULL;
    }

    if (pVia->pVisualConfigs) {
        xfree(pVia->pVisualConfigs);
        pVia->pVisualConfigs = NULL;
    }
    if (pVia->pVisualConfigsPriv) {
        xfree(pVia->pVisualConfigsPriv);
        pVia->pVisualConfigsPriv = NULL;
    }
}

/* TODO: xserver receives driver's swapping event and does something
 *       according the data initialized in this function.
 */
static Bool
VIACreateContext(ScreenPtr pScreen, VisualPtr visual,
                 drm_context_t hwContext, void *pVisualConfigPriv,
                 DRIContextType contextStore)
{
    return TRUE;
}

static void
VIADestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
                  DRIContextType contextStore)
{
}

Bool
VIADRIFinishScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
    VIADRIPtr pVIADRI;

    pVia->pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;

    pVia->IsPCI = TRUE;;

    if (!(VIADRIFBInit(pScreen, pVia))) {
        VIADRICloseScreen(pScreen);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[dri] Frame buffer initialization failed.\n");
        return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[dri] Frame buffer initialized.\n");

    DRIFinishScreenInit(pScreen);

    if (!VIADRIKernelInit(pScreen, pVia)) {
        VIADRICloseScreen(pScreen);
        return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO, "[dri] Kernel data initialized.\n");

    /* Set SAREA value. */
    {
        struct drm_via_sarea *saPriv;

        saPriv = (struct drm_via_sarea *) DRIGetSAREAPrivate(pScreen);
        assert(saPriv);
        memset(saPriv, 0, sizeof(*saPriv));
        saPriv->ctxOwner = -1;
    }
    pVIADRI = (VIADRIPtr) pVia->pDRIInfo->devPrivate;
    pVIADRI->deviceID = pVia->Chipset;
    pVIADRI->width = pScrn->virtualX;
    pVIADRI->height = pScrn->virtualY;
    pVIADRI->mem = pScrn->videoRam * 1024;
    pVIADRI->bytesPerPixel = (pScrn->bitsPerPixel + 7) / 8;
    pVIADRI->sarea_priv_offset = sizeof(XF86DRISAREARec);
    /* TODO */
    pVIADRI->scrnX = pVIADRI->width;
    pVIADRI->scrnY = pVIADRI->height;

    /* Initialize IRQ. */
    if (pVia->DRIIrqEnable)
        VIADRIIrqInit(pScrn, pVIADRI);
#if 0
    pVIADRI->ringBufActive = 0;
    VIADRIRingBufferInit(pScrn);
#else
    pVIADRI->ringBufActive = 1;
#endif    
    return TRUE;
}

static void
VIADRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
                  DRIContextType oldContextType, void *oldContext,
                  DRIContextType newContextType, void *newContext)
{
#if 0
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
#endif
    return;
}

static void
VIADRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index)
{
#if 0
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
#endif
    return;
}

static void
VIADRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
                  RegionPtr prgnSrc, CARD32 index)
{
#if 0
    ScreenPtr pScreen = pParent->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VIAPtr pVia = VIAPTR(pScrn);
#endif
    return;
}

/* Initialize the kernel data structures. */
static Bool
VIADRIKernelInit(ScreenPtr pScreen, VIAPtr pVia)
{
    struct drm_via_init drmInfo;

    memset(&drmInfo, 0, sizeof(drmInfo));
    drmInfo.op= VIA_INIT_MAP;
    drmInfo.sarea_priv_offset = sizeof(XF86DRISAREARec);
    drmInfo.fb_offset = pVia->frameBufferHandle;
    drmInfo.mmio_offset = pVia->registerHandle;
    drmInfo.agpAddr = (CARD32) NULL;

    if ((drmCommandWrite(pVia->drmFD, DRM_VIA_MAP_INIT, &drmInfo,
                         sizeof(drmInfo))) < 0)
        return FALSE;

    return TRUE;
}

/* Add a map for the MMIO registers. */
static Bool
VIADRIMapInit(ScreenPtr pScreen, VIAPtr pVia)
{
    int flags = DRM_READ_ONLY;

    if (drmAddMap(pVia->drmFD, pVia->MmioBase, VIA_MMIO_REGSIZE,
                  DRM_REGISTERS, flags, &pVia->registerHandle) < 0) {
        return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO, "[drm] register handle = 0x%08lx\n",
               (unsigned long)pVia->registerHandle);
    if (drmAddMap(pVia->drmFD, pVia->FrameBufferBase, pVia->videoRambytes,
                  DRM_FRAME_BUFFER, 0, &pVia->frameBufferHandle) < 0) {
        return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO, "[drm] framebuffer handle = 0x%08lx\n",
               (unsigned long)pVia->frameBufferHandle);

    return TRUE;
}

#define DRM_VIA_BLIT_MAX_SIZE (2048*2048*4)

static int
viaDRIFBMemcpy(int fd, unsigned long fbOffset, unsigned char *addr,
               unsigned long size, Bool toFB)
{
    int err;
    struct drm_via_dmablit blit;
    unsigned long curSize;

    do {
        curSize = (size > DRM_VIA_BLIT_MAX_SIZE) ? DRM_VIA_BLIT_MAX_SIZE : size;

        blit.num_lines = 1;
        blit.line_length = curSize;
        blit.fb_addr = fbOffset;
        blit.fb_stride = ALIGN_TO(curSize, 16);
        blit.mem_addr = addr;
        blit.mem_stride = blit.fb_stride;
        blit.to_fb = (toFB) ? 1 : 0;

        do {
            err = drmCommandWriteRead(fd, DRM_VIA_DMA_BLIT,
                                      &blit, sizeof(blit));
        } while (-EAGAIN == err);
        if (err)
            return err;

        do {
            err = drmCommandWriteRead(fd, DRM_VIA_BLIT_SYNC,
                                      &blit.sync, sizeof(blit.sync));
        } while (-EAGAIN == err);
        if (err)
            return err;

        fbOffset += curSize;
        addr += curSize;
        size -= curSize;

    } while (size > 0);
    return 0;
}


void
viaDRIOffscreenSave(ScrnInfoPtr pScrn)
{
    VIAPtr pVia = VIAPTR(pScrn);
    VIADRIPtr pVIADRI = pVia->pDRIInfo->devPrivate;
    unsigned char *saveAddr = pVia->FBBase + pVIADRI->fbOffset;
    unsigned long saveSize = pVIADRI->fbSize;
    unsigned long curSize;
    int err;

    if (pVia->driOffScreenSave)
        free(pVia->driOffScreenSave);

    pVia->driOffScreenSave = malloc(saveSize + 16);
    if (pVia->driOffScreenSave) {
        if ((pVia->drmVerMajor == 2) && (pVia->drmVerMinor >= 8)) {
            err = viaDRIFBMemcpy(pVia->drmFD, pVIADRI->fbOffset,
                                 (unsigned char *)
                                 ALIGN_TO((unsigned long)
                                          pVia->driOffScreenSave, 16),
                                 saveSize, FALSE);
            if (!err)
                return;

            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "Hardware backup of DRI offscreen memory failed: %s.\n"
                       "\tUsing slow software backup instead.\n",
                       strerror(-err));
        }
        memcpy((void *)ALIGN_TO((unsigned long)pVia->driOffScreenSave, 16),
               saveAddr, saveSize);

    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Out of memory trying to backup DRI offscreen memory.\n");
    }
    return;
}

void
viaDRIOffscreenRestore(ScrnInfoPtr pScrn)
{
    VIAPtr pVia = VIAPTR(pScrn);
    VIADRIPtr pVIADRI = pVia->pDRIInfo->devPrivate;

    unsigned char *saveAddr = pVia->FBBase + pVIADRI->fbOffset;
    unsigned long saveSize = pVIADRI->fbSize;

    if (pVia->driOffScreenSave) {
        memcpy(saveAddr,
               (void *)ALIGN_TO((unsigned long)pVia->driOffScreenSave, 16),
               saveSize);
        free(pVia->driOffScreenSave);
        pVia->driOffScreenSave = NULL;
    }
}
