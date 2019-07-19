#include "minnet.h"
#include <curl/curl.h>
#include <libwebsockets.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

JSModuleDef *JS_INIT_MODULE(JSContext *ctx, const char *module_name)
{
	JSModuleDef *m;
	m = JS_NewCModule(ctx, module_name, js_minnet_init);
	if (!m)
		return NULL;
	JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

	// Add class Response
	JS_NewClassID(&minnet_response_class_id);
	JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id,
				&minnet_response_class);
	JSValue response_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, response_proto, minnet_response_proto_funcs,
							   countof(minnet_response_proto_funcs));
	JS_SetClassProto(ctx, minnet_response_class_id, response_proto);

	// Add class WebSocket
	JS_NewClassID(&minnet_ws_class_id);
	JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
	JSValue websocket_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, websocket_proto, minnet_ws_proto_funcs,
							   countof(minnet_ws_proto_funcs));
	JS_SetClassProto(ctx, minnet_ws_class_id, websocket_proto);

	return m;
}

#define GETCB(opt, cb_ptr)                                                     \
	if (JS_IsFunction(ctx, opt)) {                                             \
		struct minnet_ws_callback cb = {ctx, &this_val, &opt};                 \
		cb_ptr = &cb;                                                          \
	}
#define SETLOG lws_set_log_level(LLL_ERR, NULL);

static JSValue create_websocket_obj(JSContext *ctx, struct lws *wsi)
{
	JSValue ws_obj = JS_NewObjectClass(ctx, minnet_ws_class_id);
	if (JS_IsException(ws_obj))
		return JS_EXCEPTION;

	MinnetWebsocket *res;
	res = js_mallocz(ctx, sizeof(*res));

	if (!res) {
		JS_FreeValue(ctx, ws_obj);
		return JS_EXCEPTION;
	}

	res->lwsi = wsi;
	JS_SetOpaque(ws_obj, res);
	return ws_obj;
}

static JSValue call_ws_callback(minnet_ws_callback *cb, int argc, JSValue *argv)
{
	return JS_Call(cb->ctx, *(cb->func_obj), *(cb->this_obj), argc, argv);
}

static int lws_server_callback(struct lws *wsi,
							   enum lws_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		break;
	case LWS_CALLBACK_ESTABLISHED: {
		if (server_cb_connect) {
			JSValue ws_obj = create_websocket_obj(server_cb_connect->ctx, wsi);
			call_ws_callback(server_cb_connect, 1, &ws_obj);
		}
	} break;
	case LWS_CALLBACK_CLOSED: {
		if (server_cb_close) {
			call_ws_callback(server_cb_close, 0, NULL);
		}
	} break;
	case LWS_CALLBACK_SERVER_WRITEABLE: {
		lws_callback_on_writable(wsi);
	} break;
	case LWS_CALLBACK_RECEIVE: {
		if (server_cb_message) {
			JSValue ws_obj = create_websocket_obj(server_cb_message->ctx, wsi);
			JSValue msg = JS_NewStringLen(server_cb_message->ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(server_cb_message, 2, cb_argv);
		}
	} break;
	case LWS_CALLBACK_RECEIVE_PONG: {
		if (server_cb_pong) {
			JSValue ws_obj = create_websocket_obj(server_cb_pong->ctx, wsi);
			JSValue msg = JS_NewArrayBufferCopy(server_cb_pong->ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(server_cb_pong, 2, cb_argv);
		}
	} break;
	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static struct lws_protocols lws_server_protocols[] = {
	{"minnet", lws_server_callback, 0, 0},
	{NULL, NULL, 0, 0},
};

static JSValue minnet_ws_server(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv)
{
	int a = 0;
	int port = 7981;
	const char *host;
	struct lws_context *context;
	struct lws_context_creation_info info;

	JSValue options = argv[0];

	JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
	JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
	JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
	JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
	JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
	JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");

	if (JS_IsNumber(opt_port))
		JS_ToInt32(ctx, &port, opt_port);

	if (JS_IsString(opt_host))
		host = JS_ToCString(ctx, opt_host);
	else
		host = "localhost";

	GETCB(opt_on_pong, server_cb_pong)
	GETCB(opt_on_close, server_cb_close)
	GETCB(opt_on_connect, server_cb_connect)
	GETCB(opt_on_message, server_cb_message)

	SETLOG

	memset(&info, 0, sizeof info);

	info.port = port;
	info.protocols = lws_server_protocols;
	info.vhost_name = host;
	info.options =
		LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
		LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

	context = lws_create_context(&info);

	if (!context) {
		lwsl_err("Libwebsockets init failed\n");
		return JS_EXCEPTION;
	}

	while (a >= 0) {
		a = lws_service(context, 1000);
	}

	lws_context_destroy(context);

	return JS_NewInt32(ctx, 0);
}

static struct lws_context *client_context;
static struct lws *client_wsi;
static int port = 7981;
static const char *client_server_address = "localhost";

static int connect_client(void)
{
	struct lws_client_connect_info i;

	memset(&i, 0, sizeof(i));

	i.context = client_context;
	i.port = port;
	i.address = client_server_address;
	i.path = "/";
	i.host = i.address;
	i.origin = i.address;
	i.ssl_connection = 0;
	i.protocol = "minnet";
	i.pwsi = &client_wsi;

	return !lws_client_connect_via_info(&i);
}

static int lws_client_callback(struct lws *wsi,
							   enum lws_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT: {
		connect_client();
	} break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
		client_wsi = NULL;
		if (client_cb_close) {
			JSValue why = JS_NewString(client_cb_close->ctx, in);
			call_ws_callback(client_cb_close, 1, &why);
		}
	} break;
	case LWS_CALLBACK_CLIENT_ESTABLISHED: {
		if (client_cb_connect) {
			JSValue ws_obj = create_websocket_obj(client_cb_connect->ctx, wsi);
			call_ws_callback(client_cb_connect, 1, &ws_obj);
		}
	} break;
	case LWS_CALLBACK_CLIENT_WRITEABLE: {
		lws_callback_on_writable(wsi);
	} break;
	case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: {
		client_wsi = NULL;
		if (client_cb_close) {
			JSValue why = JS_NewString(client_cb_close->ctx, in);
			call_ws_callback(client_cb_close, 1, &why);
		}
	} break;
	case LWS_CALLBACK_CLIENT_RECEIVE: {
		if (client_cb_message) {
			JSValue ws_obj = create_websocket_obj(client_cb_message->ctx, wsi);
			JSValue msg = JS_NewStringLen(client_cb_message->ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(client_cb_message, 2, cb_argv);
		}
	} break;
	case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
		if (client_cb_pong) {
			JSValue ws_obj = create_websocket_obj(client_cb_pong->ctx, wsi);
			JSValue data = JS_NewArrayBufferCopy(client_cb_pong->ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, data};
			call_ws_callback(client_cb_pong, 2, cb_argv);
		}
	} break;
	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols client_protocols[] = {
	{"minnet", lws_client_callback, 0, 0},
	{NULL, NULL, 0, 0},
};

static JSValue minnet_ws_client(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv)
{
	struct lws_context_creation_info info;
	int n = 0;

	SETLOG

	memset(&info, 0, sizeof info);
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = client_protocols;

	JSValue options = argv[0];
	JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
	JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
	JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
	JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
	JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
	JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");

	if (JS_IsString(opt_host))
		client_server_address = JS_ToCString(ctx, opt_host);

	if (JS_IsNumber(opt_port))
		JS_ToInt32(ctx, &port, opt_port);

	GETCB(opt_on_pong, client_cb_pong)
	GETCB(opt_on_close, client_cb_close)
	GETCB(opt_on_connect, client_cb_connect)
	GETCB(opt_on_message, client_cb_message)

	client_context = lws_create_context(&info);
	if (!client_context) {
		lwsl_err("Libwebsockets init failed\n");
		return JS_EXCEPTION;
	}

	while (n >= 0) {
		n = lws_service(client_context, 1000);
	}

	lws_context_destroy(client_context);

	return JS_EXCEPTION;
}

static JSValue minnet_ws_send(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	const char *msg;
	uint8_t *data;
	size_t len;
	int m, n;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	if (JS_IsString(argv[0])) {
		msg = JS_ToCString(ctx, argv[0]);
		len = strlen(msg);
		uint8_t buffer[LWS_PRE + len];

		n = lws_snprintf((char *)&buffer[LWS_PRE], len + 1, "%s", msg);
		m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
		if (m < n) {
			// Sending message failed
			return JS_EXCEPTION;
		}
		return JS_UNDEFINED;
	}

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[LWS_PRE + len];
		memcpy(&buffer[LWS_PRE], data, len);

		m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_BINARY);
		if (m < len) {
			// Sending data failed
			return JS_EXCEPTION;
		}
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_ping(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	uint8_t *data;
	size_t len;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[len + LWS_PRE];
		memcpy(&buffer[LWS_PRE], data, len);

		int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PING);
		if (m < len) {
			// Sending ping failed
			return JS_EXCEPTION;
		}
	} else {
		uint8_t buffer[LWS_PRE];
		lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PING);
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_pong(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	uint8_t *data;
	size_t len;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[len + LWS_PRE];
		memcpy(&buffer[LWS_PRE], data, len);

		int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PONG);
		if (m < len) {
			// Sending pong failed
			return JS_EXCEPTION;
		}
	} else {
		uint8_t buffer[LWS_PRE];
		lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PONG);
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_close(JSContext *ctx, JSValueConst this_val, int argc,
							   JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	// TODO: Find out how to clsoe connection

	return JS_UNDEFINED;
}

static void minnet_ws_finalizer(JSRuntime *rt, JSValue val)
{
	MinnetWebsocket *ws_obj = JS_GetOpaque(val, minnet_ws_class_id);
	if (ws_obj)
		js_free_rt(rt, ws_obj);
}

static JSValue minnet_fetch(JSContext *ctx, JSValueConst this_val, int argc,
							JSValueConst *argv)
{
	CURL *curl;
	CURLcode curlRes;
	const char *url;
	FILE *fi;
	MinnetResponse *res;
	uint8_t *buffer;
	long bufSize;

	if (JS_IsString(argv[0]))
		url = JS_ToCString(ctx, argv[0]);

	curl = curl_easy_init();
	if (!curl)
		return JS_EXCEPTION;

	fi = tmpfile();

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);

	curlRes = curl_easy_perform(curl);
	if (curlRes != CURLE_OK) {
		curl_easy_cleanup(curl);
		fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
		return JS_EXCEPTION;
	}
	curl_easy_cleanup(curl);

	bufSize = ftell(fi);
	rewind(fi);
	buffer = calloc(1, bufSize + 1);
	if (!buffer) {
		fclose(fi), fputs("memory alloc fails", stderr);
		return JS_EXCEPTION;
	}

	/* copy the file into the buffer */
	if (1 != fread(buffer, bufSize, 1, fi)) {
		fclose(fi), free(buffer), fputs("entire read fails", stderr);
		return JS_EXCEPTION;
	}

	fclose(fi);

	JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
	if (JS_IsException(resObj))
		return resObj;

	res = js_mallocz(ctx, sizeof(*res));
	res->buffer = buffer;
	res->size = bufSize;

	if (!res) {
		JS_FreeValue(ctx, resObj);
		return JS_EXCEPTION;
	}

	JS_SetOpaque(resObj, res);

	return resObj;
}

static JSValue minnet_response_buffer(JSContext *ctx, JSValueConst this_val,
									  int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res) {
		JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer, res->size);
		return val;
	}

	return JS_EXCEPTION;
}

static JSValue minnet_response_json(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res)
		return JS_ParseJSON(ctx, (char *)res->buffer, res->size, "<input>");

	return JS_EXCEPTION;
}

static JSValue minnet_response_text(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res)
		return JS_NewStringLen(ctx, (char *)res->buffer, res->size);

	return JS_EXCEPTION;
}

static void minnet_response_finalizer(JSRuntime *rt, JSValue val)
{
	MinnetResponse *res = JS_GetOpaque(val, minnet_response_class_id);
	if (res) {
		if (res->buffer)
			free(res->buffer);
		js_free_rt(rt, res);
	}
}