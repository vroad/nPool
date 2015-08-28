#include "nrequire.h"

// C++
#include <string>

// C
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Custom
#include "isolate_context.h"

NAN_METHOD(Require::RequireFunction)
{
    Nan::HandleScope scope;

    // validate input
    if((info.Length() != 1) || !info[0]->IsString())
    {
        return Nan::ThrowError("Require::RequireFunction - Expects 1 arguments: 1) file name (string)");
    }

    // get filename string
    Nan::Utf8String fileName(info[0]);

    // get handle to directory of current executing script
    #if NODE_VERSION_AT_LEAST(0, 12, 0)
    Handle<Object> currentContextObject = Isolate::GetCurrent()->GetCallingContext()->Global();
    #else
    Handle<Object> currentContextObject = NanGetCurrentContext()->GetCalling()->Global();
    #endif
    Handle<String> dirNameHandle = currentContextObject->Get(Nan::New<String>("__dirname").ToLocalChecked())->ToString();
    Nan::Utf8String __dirname(dirNameHandle);

    // allocate file buffer
    const FILE_INFO* fileInfo = Utilities::GetFileInfo(*fileName, *__dirname);

    // file was invalid
    if(fileInfo->fileBuffer == 0)
    {
        std::string exceptionPrefix("Require::RequireFunction - File Name is invalid: ");
        std::string exceptionFileName(*fileName);
        std::string exceptionString = exceptionPrefix + exceptionFileName;
        return Nan::ThrowTypeError(exceptionString.c_str());
    }
    // file was read successfully
    else
    {
        // register external memory
        Nan::AdjustExternalMemory(fileInfo->fileBufferLength);

        // get reference to calling context
        Handle<Context> globalContext = Nan::GetCurrentContext();

        // create new module context
        Local<Context> moduleContext = Nan::New<Context>();

        // set the security token to access calling context properties within new context
        moduleContext->SetSecurityToken(globalContext->GetSecurityToken());

        // clone the calling context properties into this context
        IsolateContext::CloneGlobalContextObject(globalContext->Global(), moduleContext->Global());

        // get reference to current context's object
        Handle<Object> contextObject = moduleContext->Global();

        // create the module context
        IsolateContext::CreateModuleContext(contextObject, fileInfo);

        // enter new context context scope
        Context::Scope context_scope(moduleContext);

        // process the source and execute it
        Handle<Value> scriptResult;
        {
            TryCatch scriptTryCatch;

            // compile the script
            ScriptOrigin scriptOrigin(Nan::New<String>(fileInfo->fileName).ToLocalChecked());
            #if NODE_VERSION_AT_LEAST(0, 11, 13)
            Handle<UnboundScript> moduleScript = Nan::New<UnboundScript>(
                Nan::New<String>(fileInfo->fileBuffer).ToLocalChecked(),
                scriptOrigin).ToLocalChecked();
            #else
            Handle<Script> moduleScript = Nan::New<Script>(
                Nan::New<String>(fileInfo->fileBuffer),
                scriptOrigin);
            #endif

            // throw exception if script failed to compile
            if(moduleScript.IsEmpty() || scriptTryCatch.HasCaught())
            {
                Require::FreeFileInfo((FILE_INFO*)fileInfo);
                scriptTryCatch.ReThrow();
                return;
            }

            //printf("[%u] Require::RequireFunction - Script Running: %s\n", SyncGetThreadId(), *fileName);
            scriptResult = Nan::RunScript(moduleScript).ToLocalChecked();
            //printf("[%u] Require::RequireFunction - Script Completed: %s\n", SyncGetThreadId(), *fileName);

            // throw exception if script failed to execute
            if(scriptResult.IsEmpty() || scriptTryCatch.HasCaught())
            {
                Require::FreeFileInfo((FILE_INFO*)fileInfo);
                scriptTryCatch.ReThrow();
                return;
            }
        }

        // print object properties
        //Utilities::PrintObjectProperties(contextObject);
        Require::FreeFileInfo((FILE_INFO*)fileInfo);

        // return module export(s)
        Handle<Object> moduleObject = contextObject->Get(Nan::New<String>("module").ToLocalChecked())->ToObject();
        info.GetReturnValue().Set(moduleObject->Get(Nan::New<String>("exports").ToLocalChecked()));
    }
}

void Require::FreeFileInfo(FILE_INFO* fileInfo) {
    // free the file buffer and de-register memory
    Nan::AdjustExternalMemory(-(fileInfo->fileBufferLength));
    Utilities::FreeFileInfo(fileInfo);
}
