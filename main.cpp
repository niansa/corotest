#include "uvpp.hpp"
#include <iostream>
#include <string>
#include <string_view>
using namespace std;


async::result<void> test(uvpp::loop_service &s, const char *ip, int port) {
    uvpp::tcp socket{s};
    co_await socket.connect(ip, port);
    co_await socket.send(std::string_view("Hello world!\n"));
    socket.recv_start();
    while (true) {
        // Read
        auto data = co_await socket.recv();
        assert(data);
        std::clog << data->nread << std::endl;
        // Check for general error
        if (data->error()) {
            continue;
        }
        // Check for broken connection
        if (data->is_broken()) {
            break;
        }
        // Make string
        auto dataStr = std::string_view{data->data.get(), data->nread};
        // Print it
        std::cout << dataStr << std::flush;
    }
}

int main() {
    using namespace uvpp;
    async::run_queue rq;
    async::queue_scope qs{&rq};
    loop_service service;

    async::detach(test(service, "127.0.0.1", 1234));
    async::detach(test(service, "127.0.0.1", 1235));
    async::run_forever(rq.run_token(), loop_service_wrapper{service});
}
