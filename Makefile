CC=clang
FLAGS=-Wall -fPIC -shared -std=gnu17
LIB=-lwebsockets -lcurl -L/usr/lib/quickjs/ -lquickjs 
DEFINE=-DJS_SHARED_LIBRARY
all:
	${CC} ${FLAGS} ${DEFINE} ${LIB} minnet.c -o minnet.so