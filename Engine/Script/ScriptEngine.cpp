//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "Exception.h"
#include "Log.h"
#include "Profiler.h"
#include "RegisterArray.h"
#include "RegisterStdString.h"
#include "ScriptEngine.h"
#include "ScriptFile.h"
#include "StringUtils.h"

#include <angelscript.h>

#include "DebugNew.h"

void messageCallback(const asSMessageInfo* msg, void* param)
{
    ScriptEngine* engine = static_cast<ScriptEngine*>(param);
    engine->logMessage(msg);
}

ScriptEngine::ScriptEngine() :
    mAngelScriptEngine(0),
    mImmediateContext(0)
{
    mAngelScriptEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (!mAngelScriptEngine)
        EXCEPTION("Could not create AngelScript engine");
    
    LOGINFO("Script engine created");
    
    mAngelScriptEngine->SetUserData(this);
    mAngelScriptEngine->SetEngineProperty(asEP_USE_CHARACTER_LITERALS, true);
    mAngelScriptEngine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
    mAngelScriptEngine->SetMessageCallback(asFUNCTION(messageCallback), this, asCALL_CDECL);
    
    // Register the array and string types, but leave it for the script engine instantiator to install the rest of the API
    {
        PROFILE(Script_RegisterInbuiltTypes);
        LOGDEBUG("Registering array and string types");
        registerArray(mAngelScriptEngine);
        registerStdString(mAngelScriptEngine);
    }
    
    // Create the context for immediate execution
    mImmediateContext = mAngelScriptEngine->CreateContext();
    // Create the function/method contexts
    for (unsigned i = 0 ; i < MAX_SCRIPT_NESTING_LEVEL; ++i)
        mScriptFileContexts.push_back(mAngelScriptEngine->CreateContext());
}

ScriptEngine::~ScriptEngine()
{
    LOGINFO("Script engine shut down");
    
    if (mImmediateContext)
    {
        mImmediateContext->Release();
        mImmediateContext = 0;
    }
    for (unsigned i = 0 ; i < MAX_SCRIPT_NESTING_LEVEL; ++i)
    {
        if (mScriptFileContexts[i])
            mScriptFileContexts[i]->Release();
    }
    mScriptFileContexts.clear();
    
    if (mAngelScriptEngine)
    {
        mAngelScriptEngine->Release();
        mAngelScriptEngine = 0;
    }
}

bool ScriptEngine::execute(const std::string& line)
{
    // Note: compiling code each time is slow. Not to be used for performance-critical or repeating activity
    PROFILE(Script_ExecuteImmediate);
    
    std::string wrappedLine = "void f(){\n" + line + ";\n}";
    
    // Create a dummy module for compiling the line
    asIScriptModule* module = mAngelScriptEngine->GetModule("ExecuteImmediate", asGM_ALWAYS_CREATE);
    if (!module)
        return false;
    
    // Use the line as the function name to get an easy to understand error message in case of failure
    asIScriptFunction *function = 0;
    if (module->CompileFunction(line.c_str(), wrappedLine.c_str(), -1, 0, &function) < 0)
        return false;
    
    if (mImmediateContext->Prepare(function->GetId()) < 0)
    {
        function->Release();
        return false;
    }
    
    bool success = false;
    
    success = mImmediateContext->Execute() >= 0;
    function->Release();
    
    return success;
}

void ScriptEngine::garbageCollect(bool fullCycle)
{
    PROFILE(Script_GarbageCollect);
    
    // Unprepare contexts up to the highest used
    mImmediateContext->Unprepare();
    unsigned highest = getHighestScriptNestingLevel();
    for (unsigned i = 0; i < highest; ++i)
        mScriptFileContexts[i]->Unprepare();
    
    if (fullCycle)
        mAngelScriptEngine->GarbageCollect(asGC_FULL_CYCLE);
    else
    {
        // If not doing a full cycle, first detect garbage using one cycle, then do a full destruction
        // This is faster than doing an actual full cycle
        mAngelScriptEngine->GarbageCollect(asGC_ONE_STEP | asGC_DETECT_GARBAGE);
        mAngelScriptEngine->GarbageCollect(asGC_FULL_CYCLE | asGC_DESTROY_GARBAGE);
    }
}

void ScriptEngine::setLogMode(ScriptLogMode mode)
{
    mLogMode = mode;
}

void ScriptEngine::clearLogMessages()
{
    mLogMessages.clear();
}

void ScriptEngine::logMessage(const asSMessageInfo* msg)
{
    std::string message = std::string(msg->section) + " (" + toString(msg->row) + "," + toString(msg->col) + ") " +
        std::string(msg->message);
    
    if (mLogMode == LOGMODE_IMMEDIATE)
    {
        switch (msg->type)
        {
        case asMSGTYPE_ERROR:
            LOGERROR(message);
            break;
            
        case asMSGTYPE_WARNING:
            LOGWARNING(message);
            break;
            
        default:
            LOGINFO(message);
            break;
        }
    }
    else
    {
        // In retained mode, ignore info messages
        if ((msg->type == asMSGTYPE_ERROR) || (msg->type == asMSGTYPE_WARNING))
            mLogMessages += message + "\n";
    }
}

asIScriptContext* ScriptEngine::getScriptFileContext(unsigned nestingLevel) const
{
    if (nestingLevel >= mScriptFileContexts.size())
        return 0;
    return mScriptFileContexts[nestingLevel];
}
