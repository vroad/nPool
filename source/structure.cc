#include "structure.h"
#include <vector>
#include <nan.h>

using namespace v8;
using namespace std;

class ObjectStructure : public IData {

public:

    ObjectStructure(Handle<Object> obj)
    {
        NanScope();
        Local<Array> keys = obj->GetPropertyNames();
        for (uint32_t i = 0; i < keys->Length(); ++i)
        {
            Local<Value> key = keys->Get(i);
            Local<Value> value = obj->Get(key);
            properties.push_back(make_pair(createDataFromValue(key), createDataFromValue(value)));
        }
    }

    ~ObjectStructure()
    {
        for (size_t i = 0; i < properties.size(); ++i)
        {
            delete properties[i].first;
            delete properties[i].second;
        }
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        Local<Object> obj = NanNew<Object>();
        for (size_t i = 0; i < properties.size(); ++i)
            obj->Set(properties[i].first->GetV8Value(), properties[i].second->GetV8Value());
        
        return NanEscapeScope(obj);
    }

    vector<pair<IData*, IData*>> properties;
};

class ArrayStructure : public IData {

public:

    ArrayStructure(Handle<Array> arr)
    {
        for (uint32_t i = 0; i < arr->Length(); ++i)
            elements.push_back(createDataFromValue(arr->Get(i)));
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        Local<Array> arr = NanNew<Array>();
        for (size_t i = 0; i < elements.size(); ++i)
            arr->Set(i, elements[i]->GetV8Value());

        return NanEscapeScope(arr);
    }

    vector<IData*> elements;
};

class StringData : public IData {
    
public:

    StringData(Handle<String> str)
        : str(str)
    {
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNew<String>(*str, str.length()));
    }

    String::Utf8Value str;
};

class Int32Data : public IData {

public:

    Int32Data(Handle<Value> value)
    {
        integer = value->Int32Value();
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNew<Int32>(integer));
    }

    int32_t integer;
};

class UInt32Data : public IData {

public:

    UInt32Data(Handle<Value> value)
    {
        integer = value->Uint32Value();
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNew<Uint32>(integer));
    }

    uint32_t integer;
};

class NumberData : public IData {

public:

    NumberData(Handle<Value> value)
    {
        number = value->NumberValue();
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNew<Number>(number));
    }

    double number;
};


class BoolData : public IData {

public:

    BoolData(Handle<Value> value)
    {
        boolValue = value->BooleanValue();
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNew<Boolean>(boolValue));
    }

    bool boolValue;
};

class NullData : public IData {

public:

    NullData()
    {
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanNull());
    }
};

class UndefinedData : public IData {

public:

    UndefinedData()
    {
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        return NanEscapeScope(NanUndefined());
    }

};

class TypedArrayData : public IData {

public:

    TypedArrayData(Handle<TypedArray> value)
    {
        data = value->GetIndexedPropertiesExternalArrayData();
        length = value->GetIndexedPropertiesExternalArrayDataLength();
        type = value->GetIndexedPropertiesExternalArrayDataType();
        NanAssignPersistent(holder, value);
    }

    Handle<Value> GetV8Value()
    {
        NanEscapableScope();
        Handle<ArrayBuffer> ab = ArrayBuffer::New(Isolate::GetCurrent(), length);
        Handle<TypedArray> ret;
        switch (type)
        {
        case kExternalUint8Array:
        default:
            ret = Uint8Array::New(ab, 0, length);
        }
        memcpy(ret->GetIndexedPropertiesExternalArrayData(), data, length);

        return NanEscapeScope(ret);
    }

    void *data;
    uint32_t length;
    ExternalArrayType type;
    Persistent<TypedArray> holder;
};

IData *createDataFromValue(Handle<Value> value)
{
    if (value->IsInt32())
        return new Int32Data(value);
    else if (value->IsUint32())
        return new UInt32Data(value);
    else if (value->IsNumber())
        return new NumberData(value);
    else if (value->IsString())
        return new StringData(value.As<String>());
    else if (value->IsBoolean())
        return new BoolData(value);
    else if (value->IsTypedArray())
        return new TypedArrayData(value.As<TypedArray>());
    else if (value->IsObject())
        return new ObjectStructure(value->ToObject());
    else if (value->IsArray())
        return new ArrayStructure(value.As<Array>());
    else if (value->IsUndefined())
        return new UndefinedData();
    else if (value->IsNull())
        return new NullData();
    else
    {
        printf("Couldn't serialize %s\n", *String::Utf8Value(value));
        return 0;
    }
}