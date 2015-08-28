#include "json_utility.h"

// C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// custom source
#include "utilities.h"

Nan::Utf8String* JsonUtility::Stringify(Handle<Value> valueHandle)
{
    Nan::HandleScope scope;

    // get reference to JSON object
    Handle<Object> contextObject = Nan::GetCurrentContext()->Global();
    Handle<Object> jsonObject = contextObject->Get(Nan::New<String>("JSON").ToLocalChecked())->ToObject();
    Handle<Function> stringifyFunc = jsonObject->Get(Nan::New<String>("stringify").ToLocalChecked()).As<Function>();

    // execute stringify
    Handle<Value> stringifyResult = stringifyFunc->Call(jsonObject, 1, &valueHandle);
    return new Nan::Utf8String(stringifyResult);
}

Handle<Value> JsonUtility::Parse(char* objectString)
{
    Nan::EscapableHandleScope scope;

    // short circuit if bad object
    if(objectString == NULL)
    {
        return scope.Escape(Nan::Undefined());
    }

    // get reference to JSON object
    Handle<Object> contextObject = Nan::GetCurrentContext()->Global();
    Handle<Object> jsonObject = contextObject->Get(Nan::New<String>("JSON").ToLocalChecked())->ToObject();
    Handle<Function> parseFunc = jsonObject->Get(Nan::New<String>("parse").ToLocalChecked()).As<Function>();

    // execute parse
    Handle<Value> jsonString = Nan::New<String>(objectString).ToLocalChecked();
    Local<Value> valueHandle = parseFunc->Call(jsonObject, 1, &jsonString);

    return scope.Escape(valueHandle);
}
