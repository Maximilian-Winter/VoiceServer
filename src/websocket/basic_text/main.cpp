#include <WebSocketServer.h>


int main() {
    try {
        AsioThreadPool thread_pool;
        WebSocketServer server(thread_pool.get_io_context(), 8080);

        server.set_message_handler([&server](const std::shared_ptr<WebSocketSession>& session, WebSocketOpCode opcode, const std::string& message) {
            std::cout << "Received message: " << message << std::endl;
            // Echo the message back to the client
            session->send(message, opcode);
        });

        thread_pool.run();
        std::cin.get();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}