#include "isolate_context.h"

// C
#include "string.h"

// Custom
#include "nrequire.h"
#include "ndlopen.h"
#include "json_utility.h"

static NAN_METHOD(ConsoleLog)
{
    Nan::HandleScope scope;

    // validate input
    if(info.Length() > 1)
    {
        return Nan::ThrowTypeError("console.log - Expects only 1 argument.");
    }

    // get log message
    if(info.Length() == 0)
    {
        printf("\n");
    }
    else
    {
        Nan::Utf8String* utf8String = JsonUtility::Stringify(info[0]);
        printf("%s\n", **utf8String);
        delete utf8String;
    }

    return;
}

void IsolateContext::CreateGlobalContext(Handle<Object> globalContext)
{
    Nan::HandleScope scope;

    // global namespace object
    globalContext->Set(Nan::New<String>("global").ToLocalChecked(), Nan::New<Object>());

    // require(...)

    // get handle to nRequire function
    Local<FunctionTemplate> functionTemplate = Nan::New<FunctionTemplate>(Require::RequireFunction);
    Local<Function> requireFunction = functionTemplate->GetFunction();
    requireFunction->SetName(Nan::New<String>("require").ToLocalChecked());

    // attach function to context
    globalContext->Set(Nan::New<String>("require").ToLocalChecked(), requireFunction);

    // console.log(...)

    // setup console object
    Handle<Object> consoleObject = Nan::New<Object>();

    // get handle to log function
    Local<FunctionTemplate> logTemplate = Nan::New<FunctionTemplate>(ConsoleLog);
    Local<Function> logFunction = logTemplate->GetFunction();
    logFunction->SetName(Nan::New<String>("log").ToLocalChecked());

    // attach log function to console object
    consoleObject->Set(Nan::New<String>("log").ToLocalChecked(), logFunction);

    // attach object to context
    globalContext->Set(Nan::New<String>("console").ToLocalChecked(), consoleObject);

    // get handle to nDLOpen function
    Local<FunctionTemplate> dlOpenFunctionTemplate = Nan::New<FunctionTemplate>(DLOpen::DLOpenFunction);
    Local<Function> dlOpenFunction = dlOpenFunctionTemplate->GetFunction();
    dlOpenFunction->SetName(Nan::New<String>("dlopen").ToLocalChecked());
    
    // attach dlopen function to context
    globalContext->Set(Nan::New<String>("dlopen").ToLocalChecked(), dlOpenFunction);

}

void IsolateContext::UpdateContextFileProperties(Handle<Object> contextObject, const FILE_INFO* fileInfo)
{
    Nan::HandleScope scope;

    // set the file properites on the context
    contextObject->Set(Nan::New<String>("__dirname").ToLocalChecked(), Nan::New<String>(fileInfo->folderPath).ToLocalChecked());
    contextObject->Set(Nan::New<String>("__filename").ToLocalChecked(), Nan::New<String>(fileInfo->fullPath).ToLocalChecked());
}

void IsolateContext::CloneGlobalContextObject(Handle<Object> sourceObject, Handle<Object> cloneObject)
{
    Nan::HandleScope scope;

    // copy global properties
    cloneObject->Set(Nan::New<String>("global").ToLocalChecked(), sourceObject->Get(Nan::New<String>("global").ToLocalChecked()));
    cloneObject->Set(Nan::New<String>("require").ToLocalChecked(), sourceObject->Get(Nan::New<String>("require").ToLocalChecked()));
    cloneObject->Set(Nan::New<String>("console").ToLocalChecked(), sourceObject->Get(Nan::New<String>("console").ToLocalChecked()));
}

void IsolateContext::CreateModuleContext(Handle<Object> contextObject, const FILE_INFO* fileInfo)
{
    Nan::HandleScope scope;

    // create the module/exports within context
    Handle<Object> moduleObject = Nan::New<Object>();
    moduleObject->Set(Nan::New<String>("exports").ToLocalChecked(), Nan::New<Object>());
    contextObject->Set(Nan::New<String>("module").ToLocalChecked(), moduleObject);
    contextObject->Set(Nan::New<String>("exports").ToLocalChecked(), moduleObject->Get(Nan::New<String>("exports").ToLocalChecked())->ToObject());

    // copy file properties
    if(fileInfo != NULL)
    {
        IsolateContext::UpdateContextFileProperties(contextObject, fileInfo);
    }
}
