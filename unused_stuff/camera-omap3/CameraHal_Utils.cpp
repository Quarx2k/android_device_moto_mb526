/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/**
* @file CameraHal_Utils.cpp
*
* This file maps the Camera Hardware Interface to V4L2.
*
*/

#define LOG_TAG "CameraHalUtils"

#include "CameraHal.h"
#include "zoom_step.inc"

#define ASPECT_RATIO_FLAG_KEEP_ASPECT_RATIO     (1<<0)  // By default is enabled
#define ASPECT_RATIO_FLAG_SHRINK_WHOLE_SRC      (1<<1)
#define ASPECT_RATIO_FLAG_CROP_BY_PHYSICAL      (1<<2)
#define ASPECT_RATIO_FLAG_CROP_BY_SOURCE        (1<<3)
#define ASPECT_RATIO_FLAG_CROP_BY_DESTINATION   (1<<4)

#define ASPECT_RATIO_FLAG_CROP_BY_ALL           (ASPECT_RATIO_FLAG_CROP_BY_PHYSICAL| \
                                                ASPECT_RATIO_FLAG_CROP_BY_SOURCE| \
                                                ASPECT_RATIO_FLAG_CROP_BY_DESTINATION)

namespace android {

/**
* aspect_ratio_calc() This routine calculates crop rect and position to
* keep correct aspect ratio from original frame
*
* @param sens_width      unsigned int - Sensor image width (before HW resizer)
*
* @param sens_height     unsigned int - Sensor image height (before HW resizer)
*
* @param pixel_width     unsigned int - Pixel width (according to sensor mode)
*
* @param pixel_height    unsigned int - Pixle height (according to sensor mode)
*
* @param src_width       unsigned int - Source image width (after HW resizer)
*
* @param src_height      unsigned int - Source image height (after HW resizer)
*
* @param dst_width       unsigned int - Destination image width (after HW resizer)
*
* @param dst_height      unsigned int - Destination image height (after HW resizer)
*
* @param align_crop_width  unsigned int-Align result requirements by horizontal for Crop
*
* @param align_crop_height unsigned int-Align result requirements by vertical for Crop
*
* @param align_pos_width   unsigned int-Align result requirements by horizontal for Position
*
* @param align_pos_height  unsigned int-Align result requirements by vertical for Position
*
* @param crop_src_left   *int - Return crop rectangel: left
*
* @param crop_src_top    *int - Return crop rectangel: top
*
* @param crop_src_width  *int - Return crop rectangel: width
*
* @param crop_src_height *int - Return crop rectangel: height
*
* @param pos_dst_left    *int - Return crop rectangel: left (Can be NULL)
*
* @param pos_dst_top     *int - Return crop rectangel: top (Can be NULL)
*
* @param pos_dst_width   *int - Return crop rectangel: width (Can be NULL)
*
* @param pos_dst_height  *int - Return crop rectangel: height (Can be NULL)
*
* @param flags           unsigned int - Flags
*
* @param zero if no error
*
*/
/* ========================================================================== */
int aspect_ratio_calc(
        unsigned int sens_width,  unsigned int sens_height,
        unsigned int pix_width,   unsigned int pix_height,
        unsigned int src_width,   unsigned int src_height,
        unsigned int dst_width,   unsigned int dst_height,
        unsigned int align_crop_width, unsigned int align_crop_height,
        unsigned int align_pos_width,  unsigned int align_pos_height,
        int *crop_src_left,  int *crop_src_top,
        int *crop_src_width, int *crop_src_height,
        int *pos_dst_left,   int *pos_dst_top,
        int *pos_dst_width,  int *pos_dst_height,
        unsigned int flags)
{
    unsigned int phys_rate, src_rate, dst_rate;
    unsigned int cswidth, csheight; // cs - Crop Source
    unsigned int pdwidth, pdheight; // pd - Position Destination
    unsigned int phys_width, phys_height;
    unsigned int max_width, max_height;
    int error = 0;

#ifdef DEBUG_LOG
    LOGE("aspect_ratio_calc Enter");
#endif

    if ((crop_src_width==NULL) || (crop_src_height==NULL) ||
        (crop_src_left==NULL) || (crop_src_top==NULL)) {

        error = -1;

        LOGE("aspect_ratio_calc Invalid arguments");
        goto fail;
    }

#ifdef DEBUG_LOG
    LOGE("Input Parameters:");
    LOGE("\tSensor      : Width = %5d,  Height = %5d",
        (int)sens_width, (int)sens_height);
    LOGE("\tPixel Aspect: Width = %5d,  Height = %5d",
        (int)pix_width, (int)pix_height);
    LOGE("\tSource      : Width = %5d,  Height = %5d",
        (int)src_width, (int)src_height);
    LOGE("\tDestination : Width = %5d,  Height = %5d",
        (int)dst_width, (int)dst_height);
    LOGE("\tAlign Crop  : Width = %5d,  Height = %5d",
        (int)align_crop_width, (int)align_crop_height);
    LOGE("\tAlign Pos   : Width = %5d,  Height = %5d",
        (int)align_pos_width, (int)align_pos_height);
    LOGE("\tFlags       : %s,%s,%s,%s,%s",
        (flags & ASPECT_RATIO_FLAG_KEEP_ASPECT_RATIO)?"Keep Aspect Ratio":"-",
        (flags & ASPECT_RATIO_FLAG_SHRINK_WHOLE_SRC )?"Shrink Whole Src":"-",
        (flags & ASPECT_RATIO_FLAG_CROP_BY_PHYSICAL)?"Crop rect by Physical area":"-",
        (flags & ASPECT_RATIO_FLAG_CROP_BY_SOURCE)?"Crop rect by Source area":"-",
        (flags & ASPECT_RATIO_FLAG_CROP_BY_DESTINATION)?"Crop rect by Destination area":"-");
#endif

    // Correct source angle according to pixel aspect ration
    phys_width  = sens_width  * pix_width;
    phys_height = sens_height * pix_height;

#ifdef DEBUG_LOG
    LOGE("\tPhisical    : Width = %5d,  Height = %5d",
        (int)phys_width, (int)phys_height);
#endif

    // Calculate Aspect ration by physical, multiplied by (1<<16)
    phys_rate = (phys_width<<16) / phys_height;

    // Calculate Aspect ration by source multiplied by (1<<16)
    src_rate = (src_width<<16) / src_height;

    // Calculate Aspect ration by destination multiplied by (1<<16)
    dst_rate = (dst_width <<16) / dst_height;

#ifdef DEBUG_LOG
    LOGE( "\tScale       : Phys  = %5d,  Src    = %5d,  Dst   = %5d",
        (int)phys_rate>>10, (int)src_rate>>10, (int)dst_rate>>10);
#endif

    cswidth  = -1;
    csheight = -1;
    pdwidth  = dst_width;
    pdheight = dst_height;

    // Compare Source and destination aspect ratio
    if ((phys_rate>>10) == (dst_rate>>10)) {
        cswidth  = phys_width;
        csheight = phys_height;
    } else {
        if (phys_rate < dst_rate) {
            if (flags & ASPECT_RATIO_FLAG_SHRINK_WHOLE_SRC) {
                csheight = phys_height;
                pdwidth  = -1;
            } else {
                cswidth  = phys_width;
            }
        } else {
            if (flags & ASPECT_RATIO_FLAG_SHRINK_WHOLE_SRC) {
                cswidth  = phys_width;
                pdheight = -1;
            } else {
                csheight = phys_height;
            }
        }
    }

    if ((signed)cswidth == -1) {
        cswidth  = (phys_height * dst_rate) >> 16;
    }

    if ((signed)csheight == -1) {
        csheight = (phys_width << 16) / dst_rate;
    }

    switch (flags & ASPECT_RATIO_FLAG_CROP_BY_ALL) {
    case ASPECT_RATIO_FLAG_CROP_BY_PHYSICAL:
        max_width  = phys_width;
        max_height = phys_height;
        break;

    case ASPECT_RATIO_FLAG_CROP_BY_SOURCE:
        // Convert to destination dimension
        cswidth    *= src_width;
        cswidth    /= phys_width;
        csheight   *= src_height;
        csheight   /= phys_height;

        max_width  = src_width;
        max_height = src_height;
        break;

    case ASPECT_RATIO_FLAG_CROP_BY_DESTINATION:
        // Convert to destination dimension
        cswidth    *= dst_width;
        cswidth    /= phys_width;
        csheight   *= dst_height;
        csheight   /= phys_height;

        max_width  = dst_width;
        max_height = dst_height;
        break;

    default:
        LOGE( "Invalid setting for Cropping by...");
        error = -1;
        goto fail;
    }

#ifdef DEBUG_LOG
    LOGE( "Output Parameters:");
    LOGE( "\tCrop        : "
        "Top   = %5d,  Left   = %5d,  Width = %5d,  Height = %5d",
        (int)((max_height-((csheight>max_height)?max_height:csheight))>>1),
        (int)((max_width -((cswidth >max_width )?max_width :cswidth ))>>1),
        (int)((cswidth >max_width )?max_width :cswidth ),
        (int)((csheight>max_height)?max_height:csheight));
#endif

    // Width align
    cswidth   = (cswidth + align_crop_width);
    cswidth  /= (align_crop_width*2);
    cswidth  *= (align_crop_width*2);

    // Height align
    csheight  = (csheight + align_crop_height);
    csheight /= (align_crop_height*2);
    csheight *= (align_crop_height*2);

    *crop_src_width  = (cswidth  > max_width )?max_width:cswidth;
    *crop_src_height = (csheight > max_height)?max_height:csheight;
    *crop_src_left   = (max_width  - *crop_src_width ) >> 1;
    *crop_src_top    = (max_height - *crop_src_height) >> 1;

#ifdef DEBUG_LOG
    LOGE( "\tCrop Align  : "
        "Top   = %5d,  Left   = %5d,  Width = %5d,  Height = %5d",
        (int)*crop_src_top, (int)*crop_src_left,
        (int)*crop_src_width, (int)*crop_src_height);
#endif

    if ((signed)pdwidth == -1) {
        pdwidth = (pdheight * phys_rate) >> 16;
    }

    if ((signed)pdheight == -1) {
        pdheight = (pdwidth << 16) / phys_rate;
    }

#ifdef DEBUG_LOG
    LOGE( "\tPos         : "
        "Top   = %5d,  Left   = %5d,  Width = %5d,  Height = %5d",
        (int)((dst_height-((pdheight>dst_height)?dst_height:pdheight))>>1),
        (int)((dst_width -((pdwidth >dst_width )?dst_width :pdwidth ))>>1),
        (int)((pdwidth  > dst_width )?dst_width :pdwidth ),
        (int)((pdheight > dst_height)?dst_height:pdheight));
#endif

    // Width align
    pdwidth   = (pdwidth + align_pos_width);
    pdwidth  /= (align_pos_width*2);
    pdwidth  *= (align_pos_width*2);

    // Height align
    pdheight  = (pdheight + align_pos_height);
    pdheight /= (align_pos_height*2);
    pdheight *= (align_pos_height*2);

    if (pos_dst_width)
        *pos_dst_width = pdwidth;

    if (pos_dst_height)
        *pos_dst_height = pdheight;

    if (pos_dst_left)
        *pos_dst_left = (dst_width - pdwidth ) >> 1;

    if (pos_dst_top)
        *pos_dst_top = (dst_height - pdheight) >> 1;

#ifdef DEBUG_LOG
    LOGE( "\tPos Align   : "
        "Top   = %5d,  Left   = %5d,  Width = %5d,  Height = %5d",
        (int)((pos_dst_top)?*pos_dst_top:-1),
        (int)((pos_dst_left)?*pos_dst_left:-1),
        (int)((pos_dst_width)?*pos_dst_width:-1),
        (int)((pos_dst_height)?*pos_dst_height:-1));


    LOGE( "Exit ( %d x %d x %d x %d)",
        (int)*crop_src_top, (int)*crop_src_left, (int)*crop_src_width, (int)*crop_src_height);
#endif

fail:
    return error;
}


#ifdef FW3A
int CameraHal::FW3A_Create()
{
    int err = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    fobj = (lib3atest_obj*)malloc(sizeof(*fobj));
    if (!fobj) {
        LOGE("cannot alloc fobj");
        goto exit;
    }
    memset(fobj, 0 , sizeof(*fobj));

    err = ICam_Create(&fobj->hnd);
    if (err < 0) {
        LOGE("Can't Create2A");
        goto exit;
    }

#ifdef DEBUG_LOG

    LOGD("FW3A Create - %d   fobj=%p", err, fobj);

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;

exit:
    LOGD("Can't create 3A FW");
    return -1;
}

int CameraHal::FW3A_Init()
{
    int err = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    /* Init 2A framework */
    err = ICam_Init(fobj->hnd);
    if (err < 0) {
        LOGE("Can't ICam_Init, will try to release first");
        err = ICam_Release(fobj->hnd, ICAM_RELEASE);
        if (!err) {
            err = ICam_Init(fobj->hnd);
            if (err < 0) {
                LOGE("Can't ICam_Init");

                err = ICam_Destroy(fobj->hnd);
                goto exit;
            }
        }
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;

exit:
    LOGE("Can't init 3A FW");
    return -1;
}

int CameraHal::FW3A_Release()
{
    int ret;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    ret = ICam_Release(fobj->hnd, ICAM_RESTART);
    if (ret < 0) {
        LOGE("Cannot Release2A");
        goto exit;
    } else {
        LOGD("2A released");
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;

exit:

    return -1;
}

int CameraHal::FW3A_Destroy()
{
    int ret;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    ret = ICam_Destroy(fobj->hnd);
    if (ret < 0) {
        LOGE("Cannot Destroy2A");
    } else {
        LOGD("2A destroyed");
    }

    free(fobj->mnote.buffer);
  	free(fobj);

    fobj = NULL;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;
}

int CameraHal::FW3A_Start()
{
    int ret;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    if (isStart_FW3A!=0) {
        LOGE("3A FW is already started");
        return -1;
    }

    //Start 3AFW
    ret = ICam_ViewFinder(fobj->hnd, ICAM_ENABLE);
    if (0 > ret) {
        LOGE("Cannot Start 2A");
        return -1;
    } else {
        LOGE("3A FW Start - success");
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    isStart_FW3A = 1;
    return 0;
}

int CameraHal::FW3A_Stop()
{
    int ret;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    if (isStart_FW3A==0) {
        LOGE("3A FW is already stopped");
        return -1;
    }

    //Stop 3AFW
    ret = ICam_ViewFinder(fobj->hnd, ICAM_DISABLE);
    if (0 > ret) {
        LOGE("Cannot Stop 3A");
        return -1;
    }

#ifdef DEBUG_LOG
     else {
        LOGE("3A FW Stop - success");
    }
#endif

    isStart_FW3A = 0;

#ifdef DEBUG_LOG
    LOG_FUNCTION_NAME_EXIT
#endif

    return 0;
}

int CameraHal::FW3A_Start_CAF()
{
    int ret;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    if (isStart_FW3A_CAF!=0) {
        LOGE("3A FW CAF is already started");
        return -1;
    }

    if (isStart_FW3A == 0) {
        LOGE("3A FW is not started");
        return -1;
    }

    ret = ICam_CFocus(fobj->hnd, ICAM_ENABLE);

    if (ret != 0) {
        LOGE("Cannot Start CAF");
        return -1;
    } else {
        isStart_FW3A_CAF = 1;
#ifdef DEBUG_LOG
        LOGD("3A FW CAF Start - success");
#endif
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;
}

int CameraHal::FW3A_Stop_CAF()
{
    int ret, prev = isStart_FW3A_CAF;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    if (isStart_FW3A_CAF==0) {
        LOGE("3A FW CAF is already stopped");
        return prev;
    }

    ret = ICam_CFocus(fobj->hnd, ICAM_DISABLE);
    if (0 > ret) {
        LOGE("Cannot Stop CAF");
        return -1;
    }


     else {
        isStart_FW3A_CAF = 0;
#ifdef DEBUG_LOG
        LOGD("3A FW CAF Stop - success");
#endif
    }


#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return prev;
}

//TODO: Add mode argument
int CameraHal::FW3A_Start_AF()
{
    int ret = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif
    if (isStart_FW3A == 0) {
        LOGE("3A FW is not started");
        return -1;
    }

    if (isStart_FW3A_AF!=0) {
        LOGE("3A FW AF is already started");
        return -1;
    }

    ret = ICam_AFocus(fobj->hnd, ICAM_ENABLE);

    if (0 != ret) {
        LOGE("Cannot Start AF");
        return -1;
    } else {
        isStart_FW3A_AF = 1;
        LOGD("3A FW AF Start - success");
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return 0;
}

int CameraHal::FW3A_Stop_AF()
{
    int ret, prev = isStart_FW3A_AF;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    if (isStart_FW3A_AF == 0) {
        LOGE("3A FW AF is already stopped");
        return isStart_FW3A_AF;
    }

    //Stop 3AFW
    ret = ICam_AFocus(fobj->hnd, ICAM_DISABLE);
    if (0 > ret) {
        LOGE("Cannot Stop AF");
        return -1;
    } else {
        isStart_FW3A_AF = 0;
        LOGE("3A FW AF Stop - success");
    }

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return prev;
}

int CameraHal::FW3A_GetSettings() const
{
    int err = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    err = ICam_ReadSettings(fobj->hnd, &fobj->settings);

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return err;
}

int CameraHal::FW3A_SetSettings()
{
    int err = 0;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    err = ICam_WriteSettings(fobj->hnd, &fobj->settings);

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME_EXIT

#endif

    return err;
}

#endif

#ifdef IMAGE_PROCESSING_PIPELINE

int CameraHal::InitIPP(int w, int h, int fmt, int ippMode)
{
    int eError = 0;

	if( (ippMode != IPP_CromaSupression_Mode) && (ippMode != IPP_EdgeEnhancement_Mode) ){
		LOGE("Error unsupported mode=%d", ippMode);
		return -1;
	}

    pIPP.hIPP = IPP_Create();
    LOGD("IPP Handle: %p",pIPP.hIPP);

	if( !pIPP.hIPP ){
		LOGE("ERROR IPP_Create");
		return -1;
	}

    if( fmt == PIX_YUV420P)
	    pIPP.ippconfig.numberOfAlgos = 3;
	else
	    pIPP.ippconfig.numberOfAlgos = 4;

    pIPP.ippconfig.orderOfAlgos[0]=IPP_START_ID;

    if( fmt != PIX_YUV420P ){
        pIPP.ippconfig.orderOfAlgos[1]=IPP_YUVC_422iTO422p_ID;

	    if(ippMode == IPP_CromaSupression_Mode ){
		    pIPP.ippconfig.orderOfAlgos[2]=IPP_CRCBS_ID;
	    }
	    else if(ippMode == IPP_EdgeEnhancement_Mode){
		    pIPP.ippconfig.orderOfAlgos[2]=IPP_EENF_ID;
	    }

        pIPP.ippconfig.orderOfAlgos[3]=IPP_YUVC_422pTO422i_ID;
        pIPP.ippconfig.isINPLACE=INPLACE_ON;
	} else {
	    if(ippMode == IPP_CromaSupression_Mode ){
		    pIPP.ippconfig.orderOfAlgos[1]=IPP_CRCBS_ID;
	    }
	    else if(ippMode == IPP_EdgeEnhancement_Mode){
		    pIPP.ippconfig.orderOfAlgos[1]=IPP_EENF_ID;
	    }

        pIPP.ippconfig.orderOfAlgos[2]=IPP_YUVC_422pTO422i_ID;
        pIPP.ippconfig.isINPLACE=INPLACE_OFF;
	}

	pIPP.outputBufferSize = (w*h*2);

    LOGD("IPP_SetProcessingConfiguration");
    eError = IPP_SetProcessingConfiguration(pIPP.hIPP, pIPP.ippconfig);
	if(eError != 0){
		LOGE("ERROR IPP_SetProcessingConfiguration");
	}

	if(ippMode == IPP_CromaSupression_Mode ){
		pIPP.CRCBptr.size = sizeof(IPP_CRCBSAlgoCreateParams);
		pIPP.CRCBptr.maxWidth = w;
		pIPP.CRCBptr.maxHeight = h;
		pIPP.CRCBptr.errorCode = 0;

		LOGD("IPP_SetAlgoConfig CRBS");
		eError = IPP_SetAlgoConfig(pIPP.hIPP, IPP_CRCBS_CREATEPRMS_CFGID, &(pIPP.CRCBptr));
		if(eError != 0){
			LOGE("ERROR IPP_SetAlgoConfig");
		}
	}else if(ippMode == IPP_EdgeEnhancement_Mode ){
		pIPP.EENFcreate.size = sizeof(IPP_EENFAlgoCreateParams);
		pIPP.EENFcreate.maxImageSizeH = w;
		pIPP.EENFcreate.maxImageSizeV = h;
		pIPP.EENFcreate.errorCode = 0;
		pIPP.EENFcreate.inPlace = 0;
		pIPP.EENFcreate.inputBufferSizeForInPlace = 0;

		LOGD("IPP_SetAlgoConfig EENF");
		eError = IPP_SetAlgoConfig(pIPP.hIPP, IPP_EENF_CREATEPRMS_CFGID, &(pIPP.EENFcreate));
		if(eError != 0){
			LOGE("ERROR IPP_SetAlgoConfig");
		}
	}

    pIPP.YUVCcreate.size = sizeof(IPP_YUVCAlgoCreateParams);
    pIPP.YUVCcreate.maxWidth = w;
    pIPP.YUVCcreate.maxHeight = h;
    pIPP.YUVCcreate.errorCode = 0;

    if ( fmt != PIX_YUV420P ) {
        LOGD("IPP_SetAlgoConfig: IPP_YUVC_422TO420_CREATEPRMS_CFGID");
        eError = IPP_SetAlgoConfig(pIPP.hIPP, IPP_YUVC_422TO420_CREATEPRMS_CFGID, &(pIPP.YUVCcreate));
	    if(eError != 0){
		    LOGE("ERROR IPP_SetAlgoConfig: IPP_YUVC_422TO420_CREATEPRMS_CFGID");
	    }
	}

    LOGD("IPP_SetAlgoConfig: IPP_YUVC_420TO422_CREATEPRMS_CFGID");
    eError = IPP_SetAlgoConfig(pIPP.hIPP, IPP_YUVC_420TO422_CREATEPRMS_CFGID, &(pIPP.YUVCcreate));
    if(eError != 0){
        LOGE("ERROR IPP_SetAlgoConfig: IPP_YUVC_420TO422_CREATEPRMS_CFGID");
    }

    LOGD("IPP_InitializeImagePipe");
    eError = IPP_InitializeImagePipe(pIPP.hIPP);
	if(eError != 0){
		LOGE("ERROR IPP_InitializeImagePipe");
	} else {
        mIPPInitAlgoState = true;
    }

    pIPP.iStarInArgs = (IPP_StarAlgoInArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_StarAlgoInArgs)));
    pIPP.iStarOutArgs = (IPP_StarAlgoOutArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_StarAlgoOutArgs)));

	if(ippMode == IPP_CromaSupression_Mode ){
        pIPP.iCrcbsInArgs = (IPP_CRCBSAlgoInArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_CRCBSAlgoInArgs)));
        pIPP.iCrcbsOutArgs = (IPP_CRCBSAlgoOutArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_CRCBSAlgoOutArgs)));
	}

	if(ippMode == IPP_EdgeEnhancement_Mode){
        pIPP.iEenfInArgs = (IPP_EENFAlgoInArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_EENFAlgoInArgs)));
        pIPP.iEenfOutArgs = (IPP_EENFAlgoOutArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_EENFAlgoOutArgs)));
	}

    pIPP.iYuvcInArgs1 = (IPP_YUVCAlgoInArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_YUVCAlgoInArgs)));
    pIPP.iYuvcOutArgs1 = (IPP_YUVCAlgoOutArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_YUVCAlgoOutArgs)));
    pIPP.iYuvcInArgs2 = (IPP_YUVCAlgoInArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_YUVCAlgoInArgs)));
    pIPP.iYuvcOutArgs2 = (IPP_YUVCAlgoOutArgs*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_YUVCAlgoOutArgs)));

	if(ippMode == IPP_EdgeEnhancement_Mode){
        pIPP.dynEENF = (IPP_EENFAlgoDynamicParams*)((char*)DSP_CACHE_ALIGN_MEM_ALLOC(sizeof(IPP_EENFAlgoDynamicParams)));
	}

	if( !(pIPP.ippconfig.isINPLACE) ){
		pIPP.pIppOutputBuffer= (unsigned char*)DSP_CACHE_ALIGN_MEM_ALLOC(pIPP.outputBufferSize); // TODO make it dependent on the output format
	}

    return eError;
}
/*-------------------------------------------------------------------*/
/**
  * DeInitIPP()
  *
  *
  *
  * @param pComponentPrivate component private data structure.
  *
  * @retval OMX_ErrorNone       success, ready to roll
  *         OMX_ErrorHardware   if video driver API fails
  **/
/*-------------------------------------------------------------------*/
int CameraHal::DeInitIPP(int ippMode)
{
    int eError = 0;

    if(mIPPInitAlgoState){
        LOGD("IPP_DeinitializePipe");
        eError = IPP_DeinitializePipe(pIPP.hIPP);
        LOGD("IPP_DeinitializePipe");
        if( eError != 0){
            LOGE("ERROR IPP_DeinitializePipe");
        }
        mIPPInitAlgoState = false;
    }

    LOGD("IPP_Delete");
    IPP_Delete(&(pIPP.hIPP));

    free(((char*)pIPP.iStarInArgs));
    free(((char*)pIPP.iStarOutArgs));

	if(ippMode == IPP_CromaSupression_Mode ){
        free(((char*)pIPP.iCrcbsInArgs));
        free(((char*)pIPP.iCrcbsOutArgs));
	}

	if(ippMode == IPP_EdgeEnhancement_Mode){
        free(((char*)pIPP.iEenfInArgs));
        free(((char*)pIPP.iEenfOutArgs));
	}

    free(((char*)pIPP.iYuvcInArgs1));
    free(((char*)pIPP.iYuvcOutArgs1));
    free(((char*)pIPP.iYuvcInArgs2));
    free(((char*)pIPP.iYuvcOutArgs2));

	if(ippMode == IPP_EdgeEnhancement_Mode){
        free(((char*)pIPP.dynEENF));
	}

	if(!(pIPP.ippconfig.isINPLACE)){
		free(pIPP.pIppOutputBuffer);
	}

    LOGD("Terminating IPP");

    return eError;
}

int CameraHal::PopulateArgsIPP(int w, int h, int fmt, int ippMode)
{
    int eError = 0;

    LOGD("IPP: PopulateArgs ENTER");

	//configuring size of input and output structures
    pIPP.iStarInArgs->size = sizeof(IPP_StarAlgoInArgs);
	if(ippMode == IPP_CromaSupression_Mode ){
	    pIPP.iCrcbsInArgs->size = sizeof(IPP_CRCBSAlgoInArgs);
	}
	if(ippMode == IPP_EdgeEnhancement_Mode){
	    pIPP.iEenfInArgs->size = sizeof(IPP_EENFAlgoInArgs);
	}

    pIPP.iYuvcInArgs1->size = sizeof(IPP_YUVCAlgoInArgs);
    pIPP.iYuvcInArgs2->size = sizeof(IPP_YUVCAlgoInArgs);

    pIPP.iStarOutArgs->size = sizeof(IPP_StarAlgoOutArgs);
	if(ippMode == IPP_CromaSupression_Mode ){
    	pIPP.iCrcbsOutArgs->size = sizeof(IPP_CRCBSAlgoOutArgs);
	}
	if(ippMode == IPP_EdgeEnhancement_Mode){
    	pIPP.iEenfOutArgs->size = sizeof(IPP_EENFAlgoOutArgs);
	}

    pIPP.iYuvcOutArgs1->size = sizeof(IPP_YUVCAlgoOutArgs);
    pIPP.iYuvcOutArgs2->size = sizeof(IPP_YUVCAlgoOutArgs);

	//Configuring specific data of algorithms
	if(ippMode == IPP_CromaSupression_Mode ){
	    pIPP.iCrcbsInArgs->inputHeight = h;
	    pIPP.iCrcbsInArgs->inputWidth = w;
	    pIPP.iCrcbsInArgs->inputChromaFormat = IPP_YUV_420P;
	}

	if(ippMode == IPP_EdgeEnhancement_Mode){
		pIPP.iEenfInArgs->inputChromaFormat = IPP_YUV_420P;
		pIPP.iEenfInArgs->inFullWidth = w;
		pIPP.iEenfInArgs->inFullHeight = h;
		pIPP.iEenfInArgs->inOffsetV = 0;
		pIPP.iEenfInArgs->inOffsetH = 0;
		pIPP.iEenfInArgs->inputWidth = w;
		pIPP.iEenfInArgs->inputHeight = h;
		pIPP.iEenfInArgs->inPlace = 0;
		pIPP.iEenfInArgs->NFprocessing = 0;
    }

    if ( fmt == PIX_YUV422I ) {
        pIPP.iYuvcInArgs1->inputChromaFormat = IPP_YUV_422ILE;
        pIPP.iYuvcInArgs1->outputChromaFormat = IPP_YUV_420P;
        pIPP.iYuvcInArgs1->inputHeight = h;
        pIPP.iYuvcInArgs1->inputWidth = w;
    }

    pIPP.iYuvcInArgs2->inputChromaFormat = IPP_YUV_420P;
    pIPP.iYuvcInArgs2->outputChromaFormat = IPP_YUV_422ILE;
    pIPP.iYuvcInArgs2->inputHeight = h;
    pIPP.iYuvcInArgs2->inputWidth = w;

	pIPP.iStarOutArgs->extendedError= 0;
	pIPP.iYuvcOutArgs1->extendedError = 0;
	pIPP.iYuvcOutArgs2->extendedError = 0;
	if(ippMode == IPP_EdgeEnhancement_Mode)
		pIPP.iEenfOutArgs->extendedError = 0;
	if(ippMode == IPP_CromaSupression_Mode )
		pIPP.iCrcbsOutArgs->extendedError = 0;

	//Filling ipp status structure
    pIPP.starStatus.size = sizeof(IPP_StarAlgoStatus);
	if(ippMode == IPP_CromaSupression_Mode ){
    	pIPP.CRCBSStatus.size = sizeof(IPP_CRCBSAlgoStatus);
	}
	if(ippMode == IPP_EdgeEnhancement_Mode){
    	pIPP.EENFStatus.size = sizeof(IPP_EENFAlgoStatus);
	}

    pIPP.statusDesc.statusPtr[0] = &(pIPP.starStatus);
	if(ippMode == IPP_CromaSupression_Mode ){
	    pIPP.statusDesc.statusPtr[1] = &(pIPP.CRCBSStatus);
	}
	if(ippMode == IPP_EdgeEnhancement_Mode){
	    pIPP.statusDesc.statusPtr[1] = &(pIPP.EENFStatus);
	}

    pIPP.statusDesc.numParams = 2;
    pIPP.statusDesc.algoNum[0] = 0;
    pIPP.statusDesc.algoNum[1] = 1;
    pIPP.statusDesc.algoNum[1] = 1;

    LOGD("IPP: PopulateArgs EXIT");

    return eError;
}

int CameraHal::ProcessBufferIPP(void *pBuffer, long int nAllocLen, int fmt, int ippMode,
                               int EdgeEnhancementStrength, int WeakEdgeThreshold, int StrongEdgeThreshold,
                                int LowFreqLumaNoiseFilterStrength, int MidFreqLumaNoiseFilterStrength, int HighFreqLumaNoiseFilterStrength,
                                int LowFreqCbNoiseFilterStrength, int MidFreqCbNoiseFilterStrength, int HighFreqCbNoiseFilterStrength,
                                int LowFreqCrNoiseFilterStrength, int MidFreqCrNoiseFilterStrength, int HighFreqCrNoiseFilterStrength,
                                int shadingVertParam1, int shadingVertParam2, int shadingHorzParam1, int shadingHorzParam2,
                                int shadingGainScale, int shadingGainOffset, int shadingGainMaxValue,
                                int ratioDownsampleCbCr)
{
    int eError = 0;
    IPP_EENFAlgoDynamicParams IPPEENFAlgoDynamicParamsCfg =
    {
        sizeof(IPP_EENFAlgoDynamicParams),
        0,//  inPlace
        150,//  EdgeEnhancementStrength;
        100,//  WeakEdgeThreshold;
        300,//  StrongEdgeThreshold;
        30,//  LowFreqLumaNoiseFilterStrength;
        80,//  MidFreqLumaNoiseFilterStrength;
        20,//  HighFreqLumaNoiseFilterStrength;
        60,//  LowFreqCbNoiseFilterStrength;
        40,//  MidFreqCbNoiseFilterStrength;
        30,//  HighFreqCbNoiseFilterStrength;
        50,//  LowFreqCrNoiseFilterStrength;
        30,//  MidFreqCrNoiseFilterStrength;
        20,//  HighFreqCrNoiseFilterStrength;
        1,//  shadingVertParam1;
        800,//  shadingVertParam2;
        1,//  shadingHorzParam1;
        800,//  shadingHorzParam2;
        128,//  shadingGainScale;
        4096,//  shadingGainOffset;
        24576,//  shadingGainMaxValue;
        1//  ratioDownsampleCbCr;
    };//2

    LOGD("IPP_StartProcessing");
    eError = IPP_StartProcessing(pIPP.hIPP);
    if(eError != 0){
        LOGE("ERROR IPP_SetAlgoConfig");
    }

	if(ippMode == IPP_EdgeEnhancement_Mode){
       IPPEENFAlgoDynamicParamsCfg.inPlace = 0;
        NONNEG_ASSIGN(EdgeEnhancementStrength, IPPEENFAlgoDynamicParamsCfg.EdgeEnhancementStrength);
        NONNEG_ASSIGN(WeakEdgeThreshold, IPPEENFAlgoDynamicParamsCfg.WeakEdgeThreshold);
        NONNEG_ASSIGN(StrongEdgeThreshold, IPPEENFAlgoDynamicParamsCfg.StrongEdgeThreshold);
        NONNEG_ASSIGN(LowFreqLumaNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.LowFreqLumaNoiseFilterStrength);
        NONNEG_ASSIGN(MidFreqLumaNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.MidFreqLumaNoiseFilterStrength);
        NONNEG_ASSIGN(HighFreqLumaNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.HighFreqLumaNoiseFilterStrength);
        NONNEG_ASSIGN(LowFreqCbNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.LowFreqCbNoiseFilterStrength);
        NONNEG_ASSIGN(MidFreqCbNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.MidFreqCbNoiseFilterStrength);
        NONNEG_ASSIGN(HighFreqCbNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.HighFreqCbNoiseFilterStrength);
        NONNEG_ASSIGN(LowFreqCrNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.LowFreqCrNoiseFilterStrength);
        NONNEG_ASSIGN(MidFreqCrNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.MidFreqCrNoiseFilterStrength);
        NONNEG_ASSIGN(HighFreqCrNoiseFilterStrength, IPPEENFAlgoDynamicParamsCfg.HighFreqCrNoiseFilterStrength);
        NONNEG_ASSIGN(shadingVertParam1, IPPEENFAlgoDynamicParamsCfg.shadingVertParam1);
        NONNEG_ASSIGN(shadingVertParam2, IPPEENFAlgoDynamicParamsCfg.shadingVertParam2);
        NONNEG_ASSIGN(shadingHorzParam1, IPPEENFAlgoDynamicParamsCfg.shadingHorzParam1);
        NONNEG_ASSIGN(shadingHorzParam2, IPPEENFAlgoDynamicParamsCfg.shadingHorzParam2);
        NONNEG_ASSIGN(shadingGainScale, IPPEENFAlgoDynamicParamsCfg.shadingGainScale);
        NONNEG_ASSIGN(shadingGainOffset, IPPEENFAlgoDynamicParamsCfg.shadingGainOffset);
        NONNEG_ASSIGN(shadingGainMaxValue, IPPEENFAlgoDynamicParamsCfg.shadingGainMaxValue);
        NONNEG_ASSIGN(ratioDownsampleCbCr, IPPEENFAlgoDynamicParamsCfg.ratioDownsampleCbCr);

        LOGD("Set EENF Dynamics Params:");
        LOGD("\tInPlace                      = %d", (int)IPPEENFAlgoDynamicParamsCfg.inPlace);
        LOGD("\tEdge Enhancement Strength    = %d", (int)IPPEENFAlgoDynamicParamsCfg.EdgeEnhancementStrength);
        LOGD("\tWeak Edge Threshold          = %d", (int)IPPEENFAlgoDynamicParamsCfg.WeakEdgeThreshold);
        LOGD("\tStrong Edge Threshold        = %d", (int)IPPEENFAlgoDynamicParamsCfg.StrongEdgeThreshold);
        LOGD("\tLuma Noise Filter Low Freq Strength   = %d", (int)IPPEENFAlgoDynamicParamsCfg.LowFreqLumaNoiseFilterStrength );
        LOGD("\tLuma Noise Filter Mid Freq Strength   = %d", (int)IPPEENFAlgoDynamicParamsCfg.MidFreqLumaNoiseFilterStrength );
        LOGD("\tLuma Noise Filter High Freq Strength   = %d", (int)IPPEENFAlgoDynamicParamsCfg.HighFreqLumaNoiseFilterStrength );
        LOGD("\tChroma Noise Filter Low Freq Cb Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.LowFreqCbNoiseFilterStrength);
        LOGD("\tChroma Noise Filter Mid Freq Cb Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.MidFreqCbNoiseFilterStrength);
        LOGD("\tChroma Noise Filter High Freq Cb Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.HighFreqCbNoiseFilterStrength);
        LOGD("\tChroma Noise Filter Low Freq Cr Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.LowFreqCrNoiseFilterStrength);
        LOGD("\tChroma Noise Filter Mid Freq Cr Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.MidFreqCrNoiseFilterStrength);
        LOGD("\tChroma Noise Filter High Freq Cr Strength = %d", (int)IPPEENFAlgoDynamicParamsCfg.HighFreqCrNoiseFilterStrength);
        LOGD("\tShading Vert 1 = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingVertParam1);
        LOGD("\tShading Vert 2 = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingVertParam2);
        LOGD("\tShading Horz 1 = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingHorzParam1);
        LOGD("\tShading Horz 2 = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingHorzParam2);
        LOGD("\tShading Gain Scale = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingGainScale);
        LOGD("\tShading Gain Offset = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingGainOffset);
        LOGD("\tShading Gain Max Val = %d", (int)IPPEENFAlgoDynamicParamsCfg.shadingGainMaxValue);
        LOGD("\tRatio Downsample CbCr = %d", (int)IPPEENFAlgoDynamicParamsCfg.ratioDownsampleCbCr);


		/*Set Dynamic Parameter*/
		memcpy(pIPP.dynEENF,
		       (void*)&IPPEENFAlgoDynamicParamsCfg,
		       sizeof(IPPEENFAlgoDynamicParamsCfg));


		LOGD("IPP_SetAlgoConfig");
		eError = IPP_SetAlgoConfig(pIPP.hIPP, IPP_EENF_DYNPRMS_CFGID, (void*)pIPP.dynEENF);
		if( eError != 0){
			LOGE("ERROR IPP_SetAlgoConfig");
		}
	}

    pIPP.iInputBufferDesc.numBuffers = 1;
    pIPP.iInputBufferDesc.bufPtr[0] = pBuffer;
    pIPP.iInputBufferDesc.bufSize[0] = nAllocLen;
    pIPP.iInputBufferDesc.usedSize[0] = nAllocLen;
    pIPP.iInputBufferDesc.port[0] = 0;
    pIPP.iInputBufferDesc.reuseAllowed[0] = 0;

	if(!(pIPP.ippconfig.isINPLACE)){
		pIPP.iOutputBufferDesc.numBuffers = 1;
		pIPP.iOutputBufferDesc.bufPtr[0] = pIPP.pIppOutputBuffer;						/*TODO, depend on pix format*/
		pIPP.iOutputBufferDesc.bufSize[0] = pIPP.outputBufferSize;
		pIPP.iOutputBufferDesc.usedSize[0] = pIPP.outputBufferSize;
		pIPP.iOutputBufferDesc.port[0] = 1;
		pIPP.iOutputBufferDesc.reuseAllowed[0] = 0;
	}

    if ( fmt == PIX_YUV422I){
	    pIPP.iInputArgs.numArgs = 4;
	    pIPP.iOutputArgs.numArgs = 4;

        pIPP.iInputArgs.argsArray[0] = pIPP.iStarInArgs;
        pIPP.iInputArgs.argsArray[1] = pIPP.iYuvcInArgs1;
	    if(ippMode == IPP_CromaSupression_Mode ){
	        pIPP.iInputArgs.argsArray[2] = pIPP.iCrcbsInArgs;
	    }
	    if(ippMode == IPP_EdgeEnhancement_Mode){
		    pIPP.iInputArgs.argsArray[2] = pIPP.iEenfInArgs;
	    }
        pIPP.iInputArgs.argsArray[3] = pIPP.iYuvcInArgs2;

        pIPP.iOutputArgs.argsArray[0] = pIPP.iStarOutArgs;
        pIPP.iOutputArgs.argsArray[1] = pIPP.iYuvcOutArgs1;
        if(ippMode == IPP_CromaSupression_Mode ){
            pIPP.iOutputArgs.argsArray[2] = pIPP.iCrcbsOutArgs;
        }
        if(ippMode == IPP_EdgeEnhancement_Mode){
            pIPP.iOutputArgs.argsArray[2] = pIPP.iEenfOutArgs;
        }
        pIPP.iOutputArgs.argsArray[3] = pIPP.iYuvcOutArgs2;
	 } else {
        pIPP.iInputArgs.numArgs = 3;
        pIPP.iOutputArgs.numArgs = 3;

        pIPP.iInputArgs.argsArray[0] = pIPP.iStarInArgs;
        if(ippMode == IPP_CromaSupression_Mode ){
            pIPP.iInputArgs.argsArray[1] = pIPP.iCrcbsInArgs;
        }
        if(ippMode == IPP_EdgeEnhancement_Mode){
            pIPP.iInputArgs.argsArray[1] = pIPP.iEenfInArgs;
        }
        pIPP.iInputArgs.argsArray[2] = pIPP.iYuvcInArgs2;

        pIPP.iOutputArgs.argsArray[0] = pIPP.iStarOutArgs;
        if(ippMode == IPP_CromaSupression_Mode ){
            pIPP.iOutputArgs.argsArray[1] = pIPP.iCrcbsOutArgs;
        }
        if(ippMode == IPP_EdgeEnhancement_Mode){
            pIPP.iOutputArgs.argsArray[1] = pIPP.iEenfOutArgs;
        }
        pIPP.iOutputArgs.argsArray[2] = pIPP.iYuvcOutArgs2;
    }

    LOGD("IPP_ProcessImage");
	if((pIPP.ippconfig.isINPLACE)){
		eError = IPP_ProcessImage(pIPP.hIPP, &(pIPP.iInputBufferDesc), &(pIPP.iInputArgs), NULL, &(pIPP.iOutputArgs));
	}
	else{
		eError = IPP_ProcessImage(pIPP.hIPP, &(pIPP.iInputBufferDesc), &(pIPP.iInputArgs), &(pIPP.iOutputBufferDesc),&(pIPP.iOutputArgs));
	}
    if( eError != 0){
		LOGE("ERROR IPP_ProcessImage");
	}

    LOGD("IPP_StopProcessing");
    eError = IPP_StopProcessing(pIPP.hIPP);
    if( eError != 0){
        LOGE("ERROR IPP_StopProcessing");
    }

	LOGD("IPP_ProcessImage Done");

    return eError;
}

#endif

int CameraHal::CorrectPreview()
{
    int dst_width, dst_height;
    struct v4l2_crop crop;
    struct v4l2_cropcap cropcap;
    int ret;
    int pos_l, pos_t, pos_w, pos_h;

#ifdef DEBUG_LOG

    LOG_FUNCTION_NAME

#endif

    mParameters.getPreviewSize(&dst_width, &dst_height);

    ret = ioctl(camera_device, VIDIOC_CROPCAP, &cropcap);
    if ( ret < 0) {
        LOGE("Error while retrieving crop capabilities");

        return EINVAL;
    }

    if (aspect_ratio_calc(cropcap.bounds.width, cropcap.bounds.height,
                         cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator,
                         cropcap.bounds.width, cropcap.bounds.height,
                         dst_width, dst_height,
                         1, 1, 1, 1,
                         (int *) &crop.c.left, (int *) &crop.c.top,
                         (int *) &crop.c.width, (int *) &crop.c.height,
                         &pos_l, &pos_t, &pos_w, &pos_h,
                         ASPECT_RATIO_FLAG_KEEP_ASPECT_RATIO|ASPECT_RATIO_FLAG_CROP_BY_SOURCE)) {

        LOGE("Error while calculating crop");

        return -1;
    }

    ret = ioctl(camera_device, VIDIOC_S_CROP, &crop);
    if (ret < 0) {
      LOGE("[%s]: ERROR VIDIOC_S_CROP failed", strerror(errno));
      return -1;
    }

    ret = ioctl(camera_device, VIDIOC_G_CROP, &crop);
    if (ret < 0) {
      LOGE("[%s]: ERROR VIDIOC_G_CROP failed", strerror(errno));
      return -1;
    }

    mInitialCrop.c.top = crop.c.top;
    mInitialCrop.c.left = crop.c.left;
    mInitialCrop.c.width = crop.c.width;
    mInitialCrop.c.height = crop.c.height;

#ifdef DEBUG_LOG

    LOGE("VIDIOC_G_CROP: top = %d, left = %d, width = %d, height = %d", crop.c.top, crop.c.left, crop.c.width, crop.c.height);

    LOG_FUNCTION_NAME_EXIT

#endif

    return NO_ERROR;
}

int CameraHal::ZoomPerform(float zoom)
{
    struct v4l2_crop crop;
    int delta_x, delta_y;
    int ret;

    LOG_FUNCTION_NAME

    memcpy( &crop, &mInitialCrop, sizeof(struct v4l2_crop));

    delta_x = crop.c.width - (crop.c.width /zoom);
    delta_y = crop.c.height - (crop.c.height/zoom);

    crop.c.width -= delta_x;
    crop.c.height -= delta_y;
    crop.c.left += delta_x >> 1;
    crop.c.top += delta_y >> 1;

    LOGE("Perform ZOOM: zoom_top = %d, zoom_left = %d, zoom_width = %d, zoom_height = %d", crop.c.top, crop.c.left, crop.c.width, crop.c.height);

    ret = ioctl(camera_device, VIDIOC_S_CROP, &crop);
    if (ret < 0) {
      LOGE("[%s]: ERROR VIDIOC_S_CROP failed", strerror(errno));
      return -1;
    }

    ret = ioctl(camera_device, VIDIOC_G_CROP, &crop);
    if (ret < 0) {
      LOGE("[%s]: ERROR VIDIOC_G_CROP failed", strerror(errno));
      return -1;
    }

    LOGE("Perform ZOOM G_GROP: crop_top = %d, crop_left = %d, crop_width = %d, crop_height = %d", crop.c.top, crop.c.left, crop.c.width, crop.c.height);

    LOG_FUNCTION_NAME_EXIT

    return 0;
}

void CameraHal::SetDSPHz(unsigned int Hz)
{
    char command[100];
    sprintf(command, "echo %u > /sys/power/dsp_freq", Hz);
    system(command);
    // LOGD("command: %s", command);
}
/************/

void CameraHal::PPM(const char* str){

#if PPM_INSTRUMENTATION

    gettimeofday(&ppm, NULL);
    ppm.tv_sec = ppm.tv_sec - ppm_start.tv_sec;
    ppm.tv_sec = ppm.tv_sec * 1000000;
    ppm.tv_sec = ppm.tv_sec + ppm.tv_usec - ppm_start.tv_usec;
    LOGD("PPM: %s :%ld.%ld ms",str, ppm.tv_sec/1000, ppm.tv_sec%1000 );

#elif PPM_INSTRUMENTATION_ABS

    unsigned long long elapsed, absolute;
    gettimeofday(&ppm, NULL);
    elapsed = ppm.tv_sec - ppm_start.tv_sec;
    elapsed *= 1000000;
    elapsed += ppm.tv_usec - ppm_start.tv_usec;
    absolute = ppm.tv_sec;
    absolute *= 1000;
    absolute += ppm.tv_usec/1000;
	LOGD("PPM: %s :%llu.%llu ms : %llu ms",str, elapsed/1000, elapsed%1000, absolute);

 #endif

}

void CameraHal::PPM(const char* str, struct timeval* ppm_first, ...){
#if PPM_INSTRUMENTATION || PPM_INSTRUMENTATION_ABS
    char temp_str[256];
    va_list args;
    va_start(args, ppm_first);
    vsprintf(temp_str, str, args);
	gettimeofday(&ppm, NULL); 
	ppm.tv_sec = ppm.tv_sec - ppm_first->tv_sec; 
	ppm.tv_sec = ppm.tv_sec * 1000000; 
	ppm.tv_sec = ppm.tv_sec + ppm.tv_usec - ppm_first->tv_usec; 
	LOGD("PPM: %s :%ld.%ld ms",temp_str, ppm.tv_sec/1000, ppm.tv_sec%1000 );
    va_end(args);
#endif
}

};




