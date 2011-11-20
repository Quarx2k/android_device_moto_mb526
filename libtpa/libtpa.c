/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Quarx (c) 2011 stud for libsmiledetect.so
 */

#define LOG_TAG "libtpa_core"

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

/*****************************************************************************/
/*
EXPORTS
TPA_SECCLK_Set_NITZ_Time     000066C4
TPA_SECCLK_Get_NITZ_Time     00006600
*/

 int TPA_LIB_KDF_Gen()
{
 LOGE(__func__);
return 0 ;
}

