import { client, server, fetch } from "minnet.so"

function CreateServer() {
    print("SERVER")
    server({
        port: 7981,
        onConnect: (socket) => {
            print("Client connected")
            socket.send("Hello from server")

        },
        onMessage: (socket, msg) => {
            print("Received: ", msg)
        },
        onClose: (why) => {
            print("Client disconnected. Reason: ", why)
        },
        onPong: (socket, data) => {
            print("Pong: ", data)
        }
    });
}

function CreateClient() {
    print("CLIENT")
    client({
        port: 7981,
        server: "localhost",
        onConnect: (socket) => {
            print("Connected to server")
            socket.send("Hello from client")
        },
        onMessage: (socket, msg) => {
            print("Received from server: ", msg)
        },
        onClose: (why) => {
            print("Disconnected from server. Reason: ", why)
        },
        onPong: (socket, data) => {
            print("Pong: ", data)
        }
    });
}

function getDownloadCount() {
    const res = fetch("https://api.github.com/repos/khanhas/spicetify-cli/releases").json();
    const dl_count = res.reduce((total, tag) => {
        return total += tag.assets.reduce((tag_total, asset) => {
            return tag_total += asset.download_count
        }, 0)
    }, 0)
    return `${dl_count}`
}

switch (scriptArgs[1]) {
    case "s": CreateServer(); break;
    case "c": CreateClient(); break;
    case "f": getDownloadCount(); break;
}