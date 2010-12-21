#ifndef SRC_BINDINGS_H_
#define SRC_BINDINGS_H_

#include "node_usb.h"
#include "libusb.h"

// Taken from node-libmysqlclient
#define OBJUNWRAP ObjectWrap::Unwrap
#define V8STR(str) String::New(str)

#ifdef ENABLE_DEBUG
  #define DEBUG_HEADER fprintf(stderr, "node-usb [%s:%s() %d]: ", __FILE__, __FUNCTION__, __LINE__); 
  #define DEBUG_FOOTER fprintf(stderr, "\n");
  #define DEBUG(STRING) DEBUG_HEADER fprintf(stderr, "%s", STRING); DEBUG_FOOTER
  #define DEBUG_OPT(...) DEBUG_HEADER fprintf(stderr, __VA_ARGS__); DEBUG_FOOTER
  #define DUMP_BYTE_STREAM(STREAM, LENGTH) DEBUG_HEADER for (int i = 0; i < LENGTH; i++) { fprintf(stderr, "0x%02X ", STREAM[i]); }
#else
  #define DEBUG(str)
  #define DEBUG_OPT(...)
  #define DUMP_BYTE_STREAM(STREAM, LENGTH)
#endif

#define THROW_BAD_ARGS(FAIL_MSG) return ThrowException(Exception::TypeError(V8STR(FAIL_MSG)));
#define THROW_ERROR(FAIL_MSG) return ThrowException(Exception::Error(V8STR(FAIL_MSG))));
#define THROW_NOT_YET return ThrowException(Exception::Error(String::Concat(String::New(__FUNCTION__), String::New("not yet supported"))));
#define CREATE_ERROR_OBJECT_AND_CLOSE_SCOPE(ERRNO) \
		Local<Object> error = Object::New();\
		error->Set(V8STR("errno"), Integer::New(ERRNO));\
		error->Set(V8STR("error"), errno_exception(ERRNO));\
		return scope.Close(error);\

#define CHECK_USB(r, scope) \
	if (r < LIBUSB_SUCCESS) { \
		CREATE_ERROR_OBJECT_AND_CLOSE_SCOPE(r); \
	}

#define OPEN_DEVICE_HANDLE_NEEDED(scope) \
	if (self->device_container->handle_status == UNINITIALIZED) {\
		if ((self->device_container->last_error = libusb_open(self->device_container->device, &(self->device_container->handle))) < 0) {\
			self->device_container->handle_status = FAILED;\
		} else {\
			self->device_container->handle_status = OPENED;\
		}\
	}\
	if (self->device_container->handle_status == FAILED) { \
		CREATE_ERROR_OBJECT_AND_CLOSE_SCOPE(self->device_container->last_error) \
	} \

#define LOCAL(TYPE, VARNAME, REF) \
		HandleScope scope;\
		TYPE *VARNAME = OBJUNWRAP<TYPE>(REF);
		
#define	EIO_CAST(TYPE, VARNAME) struct TYPE *VARNAME = reinterpret_cast<struct TYPE *>(req->data);
#define	EIO_NEW(TYPE, VARNAME) struct TYPE *VARNAME = (struct TYPE *) calloc(1, sizeof(struct TYPE));
#define EIO_DELEGATION(VARNAME, CALLBACK_ARG_IDX) \
		Local<Function> callback; \
		if (args[CALLBACK_ARG_IDX]->IsFunction()) { \
			callback = Local<Function>::Cast(args[CALLBACK_ARG_IDX]); \
		} \
		if (!VARNAME) { \
			V8::LowMemoryNotification(); \
		} \
		VARNAME->callback = Persistent<Function>::New(callback); \
		VARNAME->error = Persistent<Object>::New(Object::New()); \

#define EIO_AFTER(VARNAME) HandleScope scope; \
		ev_unref(EV_DEFAULT_UC); \
		if (sizeof(VARNAME->callback) > 0) { \
			Local<Value> argv[1]; \
			argv[0] = Local<Value>::New(scope.Close(VARNAME->error)); \
			VARNAME->callback->Call(Context::GetCurrent()->Global(), 1, argv); \
			VARNAME->callback.Dispose(); \
		}
		
#define TRANSFER_REQUEST_FREE(STRUCT)\
		EIO_CAST(STRUCT, transfer_req)\
		EIO_AFTER(transfer_req)\
		free(transfer_req);\
		return 0;

#define INIT_TRANSFER_CALL(MINIMUM_ARG_LENGTH, CALLBACK_ARG_IDX, TIMEOUT_ARG_IDX) \
		libusb_endpoint_direction modus; \
		uint32_t timeout = 0; \
		int32_t buflen = 0; \
		unsigned char *buf; \
\
		if (args.Length() < MINIMUM_ARG_LENGTH || !args[CALLBACK_ARG_IDX]->IsFunction()) { \
			THROW_BAD_ARGS("Endpoint::Transfer expects at least MINIMUM_ARG_LENGTH arguments [mixed data, function:callback[data, status] [, uint:timeout_in_ms, uint:transfer_flags, uint32_t:iso_packets]]!") \
		} \
\
		if (args[0]->IsArray()) { \
			modus = LIBUSB_ENDPOINT_OUT; \
		} else { \
			modus = LIBUSB_ENDPOINT_IN; \
			if (!args[0]->IsUint32()) { \
			      THROW_BAD_ARGS("Endpoint::Transfer in READ mode expects uint32_t as first parameter") \
			} \
		} \
		if (modus == LIBUSB_ENDPOINT_OUT) {\
		  	Local<Array> _buffer = Local<Array>::Cast(args[0]);\
			buflen = _buffer->Length(); \
			buf = new unsigned char[buflen]; \
			for (int i = 0; i < buflen; i++) { \
				Local<Value> val = _buffer->Get(i); \
				buf[i] = (uint8_t)val->Uint32Value();\
			}\
			DEBUG("Dumping byte stream...")\
			DUMP_BYTE_STREAM(buf, buflen);\
		}\
		else {\
			buflen = args[0]->Uint32Value();\
			buf = new unsigned char[buflen];\
		}\
		if (args.Length() >= (TIMEOUT_ARG_IDX + 1)) {\
			if (!args[TIMEOUT_ARG_IDX]->IsUint32()) {\
				THROW_BAD_ARGS("Endpoint::Transfer expects unsigned int as timeout parameter")\
			} else {\
				timeout = args[TIMEOUT_ARG_IDX]->Uint32Value();\
			}\
		}

namespace NodeUsb  {
	// status of device handle
	enum nodeusb_device_handle_status { 
		UNINITIALIZED, 
		FAILED, 
		OPENED
	};

	// structure for device and device handle
	struct nodeusb_device_container {
		libusb_device *device;
		libusb_device_handle *handle;
		libusb_config_descriptor *config_descriptor;
		nodeusb_device_handle_status handle_status;
		int last_error;
	};

	struct nodeusb_endpoint_selection {
		int interface_number;
		int interface_alternate_setting;
		int endpoint_number;
	};	

	// intermediate EIO structure for device
	struct device_request {
		Persistent<Function> callback;
		Persistent<Object> error;
		libusb_device *device;
	};

	// intermediate EIO structure for device handle
	struct device_handle_request:device_request {
		libusb_device_handle *handle;
	};

	struct nodeusb_transfer:device_handle_request {
		unsigned char *data;
		unsigned int timeout;
	};



	static inline Local<Value> errno_exception(int errorno) {
		Local<Value> e  = Exception::Error(String::NewSymbol(strerror(errorno)));
		Local<Object> obj = e->ToObject();
		std::string err = "";

		obj->Set(NODE_PSYMBOL("errno"), Integer::New(errorno));
		// taken from pyusb
		switch (errorno) {
			case LIBUSB_ERROR_IO:
				err = "Input/output error";
				break;
			case LIBUSB_ERROR_INVALID_PARAM:
				err  = "Invalid parameter";
				break;
			case LIBUSB_ERROR_ACCESS:
				err  = "Access denied (insufficient permissions)";
				break;
			case LIBUSB_ERROR_NO_DEVICE:
				err = "No such device (it may have been disconnected)";
				break;
			case LIBUSB_ERROR_NOT_FOUND:
				err = "Entity not found";
				break;
			case LIBUSB_ERROR_BUSY:
				err = "Resource busy";
				break;
			case LIBUSB_ERROR_TIMEOUT:
				err = "Operation timed out";
				break;
			case LIBUSB_ERROR_OVERFLOW:
				err = "Overflow";
				break;
			case LIBUSB_ERROR_PIPE:
				err = "Pipe error";
				break;
			case LIBUSB_ERROR_INTERRUPTED:
				err = "System call interrupted (perhaps due to signal)";
				break;
			case LIBUSB_ERROR_NO_MEM:
				err = "Insufficient memory";
				break;
			case LIBUSB_ERROR_NOT_SUPPORTED:
				err = "Operation not supported or unimplemented on this platform";
				break;
			default:
				err = "Unknown error";
				break;
		}
		// convert err to const char* with help of c_str()
		obj->Set(NODE_PSYMBOL("msg"), String::New(err.c_str()));
		return e;
	}
}
#endif
