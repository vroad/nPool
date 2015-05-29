#ifndef _NDLOPEN_H_
#define _NDLOPEN_H_

// node
#include <node.h>
#include <v8.h>
using namespace v8;

#include <nan.h>

#define DLOPEN_FUNCTION_NAME "dlopen"

class DLOpen
{
public:

	static NAN_METHOD(DLOpenFunction);

};

#endif /* _NDLOPEN_H_ */
