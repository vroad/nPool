#include "ndlopen.h"

using namespace v8;

typedef void(*addon_init_func)(
	v8::Handle<v8::Object> exports
	);

// Copied from node.cc
// DLOpen is process.dlopen(module, filename).
// Used to load 'module.node' dynamically shared objects.
//
// FIXME(bnoordhuis) Not multi-context ready. TBD how to resolve the conflict
// when two contexts try to load the same shared object. Maybe have a shadow
// cache that's a plain C list or hash table that's shared across contexts?
NAN_METHOD(DLOpen::DLOpenFunction) {
	Nan::HandleScope scope;
	static Local<String> exports_string = Nan::New<String>("exports").ToLocalChecked();
	uv_lib_t lib;

	if (info.Length() < 2) {
		Nan::ThrowError("dlopen takes exactly 2 arguments.");
		return;
	}

	Local<Object> module = info[0]->ToObject();  // Cast
	String::Utf8Value filename(info[1]);  // Cast

	Local<Object> exports = module->Get(exports_string)->ToObject();

	if (uv_dlopen(*filename, &lib)) {
		Local<String> errmsg = Nan::New<String>(uv_dlerror(&lib)).ToLocalChecked();
#ifdef _WIN32
		// Windows needs to add the filename into the error message
		errmsg = String::Concat(errmsg, info[1]->ToString());
#endif  // _WIN32
		Nan::ThrowError(Exception::Error(errmsg));
		return;
	}

	String::Utf8Value name(info[2]);
	addon_init_func func = 0;
	if (uv_dlsym(&lib, "Init", (void**)&func)) {
		Local<String> errmsg = Nan::New<String>(uv_dlerror(&lib)).ToLocalChecked();
#ifdef _WIN32
		// Windows needs to add the filename into the error message
		errmsg = String::Concat(errmsg, info[1]->ToString());
#endif  // _WIN32
		Nan::ThrowError(Exception::Error(errmsg));
		return;
	}
	func(exports);
	uv_dlclose(&lib);
	return;
}