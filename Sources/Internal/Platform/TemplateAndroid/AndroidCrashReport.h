/*==================================================================================
    Copyright (c) 2008, binaryzebra
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the binaryzebra nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE binaryzebra AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL binaryzebra BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
=====================================================================================*/



#ifndef __DAVAENGINE_ANDROID_CRASH_REPORT_H__
#define __DAVAENGINE_ANDROID_CRASH_REPORT_H__

#include "Base/BaseTypes.h"
#if defined(__DAVAENGINE_ANDROID__)

#include "Platform/TemplateAndroid/JniHelpers.h"
#include <signal.h>

namespace DAVA
{

class File;
class JniCrashReporter
{
public:
    struct CrashStep
    {
        const char * module;
        const char * function;
        int32 fileLine;
    };
    JniCrashReporter();
    void ThrowJavaExpetion(const Vector<CrashStep>& chashSteps);
       
private:
    JNI::JavaClass jniCrashReporter;
    Function<void (jstringArray, jstringArray, jintArray)> throwJavaExpetion;

    JNI::JavaClass jniString;
};
    
class AndroidCrashReport
{
public:
    static void Init();
    static void ThrowExeption(const String& message);
    static void Unload();

private:
    static void SignalHandler(int signal, siginfo_t *info, void *uapVoid);
    static void OnStackFrame(pointer_size addr);
    static JniCrashReporter::CrashStep FormatTeamcityIdStep(int32 addr);
private:
    static stack_t s_sigstk;
    
    //pre allocated here to be used inside signal handler
    static Vector<JniCrashReporter::CrashStep> crashSteps;
    static const size_t functionStringSize = 30;
    static const size_t maxStackSize = 256;
    
    static const char * teamcityBuildNamePrototype;
    static const char * teamcityBuildNamePrototypeEnd;
    static const char * teamcityBuildNamePrototypePlaceHolder;
    
    static char * teamcityBuildName;
    static char functionString[maxStackSize][functionStringSize];
};



}

#endif //#if defined(__DAVAENGINE_ANDROID__)

#endif /* #ifndef __DAVAENGINE_ANDROID_CRASH_HANDLER_H__ */
