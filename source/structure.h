#ifndef _STRUCTURE_H_
#define _STRUCTURE_H_

#include <node.h>

class IData {

public:

    virtual ~IData()
    {
    }

    virtual v8::Handle<v8::Value> GetV8Value() = 0;
};

IData *createDataFromValue(v8::Handle<v8::Value> value);

#endif /* STRUCTURE_H */