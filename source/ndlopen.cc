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
	NanScope();
	static Local<String> exports_string = NanNew<String>("exports");
	uv_lib_t lib;

	if (args.Length() < 2) {
		NanThrowError("dlopen takes exactly 2 arguments.");
		return;
	}

	Local<Object> module = args[0]->ToObject();  // Cast
	String::Utf8Value filename(args[1]);  // Cast

	Local<Object> exports = module->Get(exports_string)->ToObject();

	if (uv_dlopen(*filename, &lib)) {
		Local<String> errmsg = NanNew<String>(uv_dlerror(&lib));
#ifdef _WIN32
		// Windows needs to add the filename into the error message
		errmsg = String::Concat(errmsg, args[1]->ToString());
#endif  // _WIN32
		NanThrowError(Exception::Error(errmsg));
		return;
	}

	String::Utf8Value name(args[2]);
	addon_init_func func = 0;
	if (uv_dlsym(&lib, "Init", (void**)&func)) {
		Local<String> errmsg = NanNew<String>(uv_dlerror(&lib));
#ifdef _WIN32
		// Windows needs to add the filename into the error message
		errmsg = String::Concat(errmsg, args[1]->ToString());
#endif  // _WIN32
		NanThrowError(Exception::Error(errmsg));
		return;
	}
	func(exports);
	uv_dlclose(&lib);
	NanReturnUndefined();
}