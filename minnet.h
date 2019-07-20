#include <quickjs/quickjs.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

typedef struct minnet_ws_callback {
	JSContext *ctx;
	JSValueConst *this_obj;
	JSValue *func_obj;
} minnet_ws_callback;

static struct minnet_ws_callback *server_cb_message;
static struct minnet_ws_callback *server_cb_connect;
static struct minnet_ws_callback *server_cb_close;
static struct minnet_ws_callback *server_cb_pong;

static JSValue minnet_ws_server(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv);

static struct minnet_ws_callback *client_cb_message;
static struct minnet_ws_callback *client_cb_connect;
static struct minnet_ws_callback *client_cb_close;
static struct minnet_ws_callback *client_cb_pong;

static JSValue minnet_ws_client(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv);

static JSValue minnet_fetch(JSContext *ctx, JSValueConst this_val, int argc,
							JSValueConst *argv);

static const JSCFunctionListEntry minnet_funcs[] = {
	JS_CFUNC_DEF("server", 1, minnet_ws_server),
	JS_CFUNC_DEF("client", 1, minnet_ws_client),
	JS_CFUNC_DEF("fetch", 1, minnet_fetch),
};

static int js_minnet_init(JSContext *ctx, JSModuleDef *m)
{
	return JS_SetModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));
}

/* class WebSocket */

typedef struct {
	struct lws *lwsi;
} MinnetWebsocket;

static JSValue minnet_ws_send(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv);
static JSValue minnet_ws_ping(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv);
static JSValue minnet_ws_pong(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv);
static JSValue minnet_ws_close(JSContext *ctx, JSValueConst this_val, int argc,
							   JSValueConst *argv);
static void minnet_ws_finalizer(JSRuntime *rt, JSValue val);

static JSClassDef minnet_ws_class = {
	"MinnetWebSocket",
	.finalizer = minnet_ws_finalizer,
};

static const JSCFunctionListEntry minnet_ws_proto_funcs[] = {
	JS_CFUNC_DEF("send", 1, minnet_ws_send),
	JS_CFUNC_DEF("ping", 1, minnet_ws_ping),
	JS_CFUNC_DEF("pong", 1, minnet_ws_pong),
	JS_CFUNC_DEF("close", 1, minnet_ws_close),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetWebSocket",
					   JS_PROP_CONFIGURABLE),
};

JSClassID minnet_ws_class_id;

static JSValue create_websocket_obj(JSContext *ctx, struct lws *wsi);

/* class Response */

typedef struct {
	uint8_t *buffer;
	long size;
	JSValue status;
	JSValue ok;
	JSValue url;
	JSValue type;
} MinnetResponse;

static JSValue minnet_response_buffer(JSContext *ctx, JSValueConst this_val,
									  int argc, JSValueConst *argv);
static JSValue minnet_response_json(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv);
static JSValue minnet_response_text(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv);
static JSValue minnet_response_getter_ok(JSContext *ctx, JSValueConst this_val);
static JSValue minnet_response_getter_url(JSContext *ctx,
										  JSValueConst this_val);
static JSValue minnet_response_getter_status(JSContext *ctx,
											 JSValueConst this_val);
static JSValue minnet_response_getter_type(JSContext *ctx,
										   JSValueConst this_val);
static void minnet_response_finalizer(JSRuntime *rt, JSValue val);

static JSClassDef minnet_response_class = {
	"MinnetResponse",
	.finalizer = minnet_response_finalizer,
};

static const JSCFunctionListEntry minnet_response_proto_funcs[] = {
	JS_CFUNC_DEF("arrayBuffer", 0, minnet_response_buffer),
	JS_CFUNC_DEF("json", 0, minnet_response_json),
	JS_CFUNC_DEF("text", 0, minnet_response_text),
	JS_CGETSET_DEF("ok", minnet_response_getter_ok, NULL),
	JS_CGETSET_DEF("url", minnet_response_getter_url, NULL),
	JS_CGETSET_DEF("status", minnet_response_getter_status, NULL),
	JS_CGETSET_DEF("type", minnet_response_getter_type, NULL),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "MinnetResponse",
					   JS_PROP_CONFIGURABLE),
};

JSClassID minnet_response_class_id;
