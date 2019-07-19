# minnet-quickjs
This package aims to provide simple, minimal and essential networking infrastructures for QuickJS.  
Currently, it can:
- Creating WebSocket `server`
- Creating WebSocket `client`
- `fetch`

## Usage
Requirements:
- `clang`
- `libwebsockets`
- `libcurl`

To use `minnet-quickjs` in your QuickJS project, run following commands:
```bash
cd your_project_dir
git clone https://github.com/khanhas/minnet-quickjs
cd minnet-quickjs
make
```

In your JS script:
```javascript
import * as minnet from "./minnet-quickjs/minnet.so"
```

### `minnet.server(options)`: Create a WebSocket server and listen to host:port.  
`options`: an object with following properties:
- `port`: *number*, *optional*, *default = `7981`*
- `host`: *string*, *optional*, *default = `"localhost"`*
- `onConnect`: *function*, *optional*  
    Calls when a client connects to server. Returns client's `MinnetWebsocket` instance in parameter. Syntax:
```javascript
onConnect: (client_socket) => {
    print("A client connected")
}
```
- `onClose(why)`: *function*, *optional*  
    Call when client disconnects from server. Returns disconnect reason in parameter. Syntax:
```javascript
onClose: (why) => {
    print("Client disconnected. Reason: ", why)
}
```
- `onMessage`: *function*, *optional*  
   Call when client sends a message to server. Returns client's `MinnetWebsocket` instance and received message in parameters. Syntax:
```javascript
onMessage: (client_socket, message) => {
    print("Received: ", message)
}
```

### `minnet.client(options)`: Create a WebSocket client and connect to a server.  
`options`: an object with following properties:
- `port`: *number*, *optional*, *default = `7981`*
- `host`: *string*, *optional*, *default = `"localhost"`*
- `onConnect`: *function*, *optional*  
    Calls when client connects to server succesfully. Returns server's `MinnetWebsocket` instance in parameter. Syntax:
```javascript
onConnect: (server_socket) => {
    print("Connected to server")
}
```
- `onClose(why)`: *function*, *optional*  
    Call when client disconnects from server. Returns disconnect reason in parameter. Syntax:
```javascript
onClose: (why) => {
    print("Disconnected from server. Reason: ", why)
}
```
- `onMessage`: *function*, *optional*  
   Call when server sends message to client. Returns server's `MinnetWebsocket` instance and received message in parameters. Syntax:
```javascript
onMessage: (server_socket, message) => {
    print("Received: ", message)
}
```

#### `MinnetWebsocket` instance
contains socket to a server or client. You can use these methods to communicate:
- `.send(message)`: `message` can be string or ArrayBuffer
- `.ping(data)`: `data` must be ArrayBuffer
- `.pong(data)`: `data` must be ArrayBuffer

### `fetch(url)`: Get resources from `url`
`url`: a string to download resources from.  
Returns `MinnetResponse` object that you can use these methods:
- `.text()`: Get body text as string
- `.json()`: Get body text, parse as JSON and returns parsed object.
- `.arrayBuffer()`: Get body as an `ArrayBuffer`

Check out [example.mjs](./example.mjs)
