#include <iostream>
#include "uvpp.hpp"
using namespace std;


async::result<void> udp_recv_test(uvasync::loop_service &s) {
    uvasync::udp socket { s };
    socket.bind(1234);
    socket.recv_start();
    while (true) {
        auto odgram = co_await socket.async_recv();
        assert(odgram);
        auto dgram = std::move(*odgram);
        if (dgram.buffer == NULL || dgram.nread <= 0) {
            continue;
        }
        std::cout << std::string_view {
            dgram.buffer.get(),
            // dont ask, not sure why either, I'll check in the
            // morning
            static_cast<std::string_view::size_type>(dgram.nread)
        }
        << std::endl;
    }
}

int main() {
    using namespace uvasync;
    async::run_queue rq;
    async::queue_scope qs{&rq};
    loop_service service;

    async::detach(udp_recv_test(service));
    async::run_forever(rq.run_token(), loop_service_wrapper{service});
}
