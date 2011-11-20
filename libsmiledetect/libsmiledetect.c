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

#define LOG_TAG "libsmiledetect"

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <utils/Log.h>

#include <cutils/atomic.h>

/*****************************************************************************/
/*
EXPORTS
SmileDetectEngineImpl::SmileDetectEngineImpl(void)                00000B74
SmileDetectEngineImpl::SmileDetectEngineImpl(void)                00000BA4
destroySmileDetectEngine                                          000009C4
SmileDetectEngineImpl::release(void)                              00000A20
`vtable for'SmileDetectEngineImpl                                 00001018
SmileDetectEngineImpl::~SmileDetectEngineImpl()                   00000A4C
SmileDetectEngineImpl::getPixelLineBit(int)                       000009F8
createSmileDetectEngine                                           00000D70
SmileDetectEngineImpl::mapPixelFormat(int)                        000009D4
SmileDetectEngineImpl::smileDetect(int,SDBox *,SDImageInfo,int *) 00000BD4
SmileDetectEngineImpl::create(void)                               00000ABC
SmileDetectEngineImpl::~SmileDetectEngineImpl()                   00000A84
SmileDetectEngineImpl::~SmileDetectEngineImpl()                   00000D34
start                                                             000009A4
*/

 int destroySmileDetectEngine()
{
return 0 ;
}

 int createSmileDetectEngine()
{
return 0 ;
}

