#include <async/basic.hpp>
#include <async/queue.hpp>
#include <async/result.hpp>
#include <async/mutex.hpp>
#include <cstring>
#include <frg/std_compat.hpp>
#include <memory>
#include <netinet/ip.h>
#include <optional>
#include <string_view>
#include <uv.h>

namespace uvpp {
struct loop_service {
    explicit loop_service(uv_loop_t *loop) : loop(loop) {}
    loop_service() : loop_service(uv_default_loop()) {}

    void wait();

private:
    struct loop_deleter {
        void operator()(uv_loop_t *loop) {
            assert(!uv_loop_close(loop));
            delete loop;
        }
    };
    std::unique_ptr<uv_loop_t, loop_deleter> loop;

    friend struct tcp;
};

void loop_service::wait() { uv_run(loop.get(), UV_RUN_ONCE); }

struct loop_service_wrapper {
    loop_service &sv;
    void wait() { sv.wait(); }
};

namespace {
void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
    buf->base = static_cast<char *>(operator new(size));
    buf->len = size;
}
} // namespace

struct tcp {
    // Basic stuff
    struct [[nodiscard]] received {
        std::unique_ptr<const char> data;
        size_t bufsize;
        ssize_t nread;

        bool error() {
            return data == nullptr || bufsize <= 0;
        }
        bool is_broken() {
            return nread < 0;
        }
    };

    template <class T> void setDataMember(T &t) {
        t.data = static_cast<void *>(this);
    }

    explicit tcp(loop_service &loop) {
        uv_tcp_init(loop.loop.get(), uv_tcp.get());
        setDataMember(*uv_tcp);
    }

    // Receive
    void recv_start() {
        if (recv_started) {
            return;
        }
        uv_read_start(reinterpret_cast<uv_stream_t *>(uv_tcp.get()), &alloc_buffer,
                      &on_read);
        recv_started = true;
    }

    void recv_stop() {
        if (!recv_started) {
            return;
        }
        uv_read_stop(reinterpret_cast<uv_stream_t *>(uv_tcp.get()));
        recv_started = false;
    }

    auto recv() { return recvQueue.async_get(); }

    // Send
    auto send(const char *data, size_t len) {
        uv_buf_t buffer[] = {{.base = const_cast<char *>(data), .len = len}};
        auto pWrite = new uv_write_t;
        setDataMember(*pWrite);
        uv_write(pWrite, reinterpret_cast<uv_stream_t *>(uv_tcp.get()), buffer, 1,
                 &on_write);

        return sendQueue.async_get();
    }

    template <class T> auto send(const T &data) {
        return send(data.data(), data.size());
    }

    // Connect
    auto connect(const char *host, int port) {
        uv_tcp_keepalive(uv_tcp.get(), 1, 60);

        struct sockaddr_in dest;
        uv_ip4_addr(host, port, &dest);

        auto pConn = new uv_connect_t;
        setDataMember(*pConn);
        uv_tcp_connect(pConn, uv_tcp.get(), (const struct sockaddr *)&dest,
                       &on_connect);

        return connectQueue.async_get();
    }

    // Close
    auto close() {
        recv_stop();
        closeLock.try_lock();
        uv_close(reinterpret_cast<uv_handle_t*>(uv_tcp.get()), &on_close);
        return closeLock.async_lock();
    }
    ~tcp() { recv_stop(); }

private:
    // Basic stuff
    std::unique_ptr<uv_tcp_t> uv_tcp{new uv_tcp_t};
    bool recv_started = false;

    // Async queues

    async::queue<received, frg::stl_allocator> recvQueue;
    async::queue<int, frg::stl_allocator> sendQueue;
    async::queue<int, frg::stl_allocator> connectQueue;
    async::mutex closeLock;

    // Callbacks
    static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
        received r{std::unique_ptr<const char>{buf->base}, buf->len, nread};
        auto self = static_cast<tcp *>(handle->data);
        self->recvQueue.emplace(std::move(r));
    }

    static void on_write(uv_write_t *handle, int status) {
        auto self = static_cast<tcp *>(handle->data);
        self->sendQueue.emplace(status);
        delete handle;
    }

    static void on_connect(uv_connect_t *handle, int status) {
        auto self = static_cast<tcp *>(handle->data);
        self->connectQueue.emplace(status);
        delete handle;
    }

    static void on_close(uv_handle_t* handle) {
        auto self = static_cast<tcp *>(handle->data);
        self->closeLock.unlock();
    }
};
} // namespace uvpp
