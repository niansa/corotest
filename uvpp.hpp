#include <memory>
#include <optional>
#include <string_view>
#include <cstring>
#include <netinet/ip.h>
#include <async/result.hpp>
#include <async/basic.hpp>
#include <async/queue.hpp>
#include <frg/std_compat.hpp>
#include <uv.h>

namespace uvasync {
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

    friend struct udp;
};

void loop_service::wait() {
    uv_run(loop.get(), UV_RUN_ONCE);
}

struct loop_service_wrapper {
    loop_service &sv;
    void wait() { sv.wait(); }
};

namespace {
void alloc_buffer(uv_handle_t * handle, size_t size, uv_buf_t *buf) {
    // malloc + delete are ub
    buf->base = static_cast<char *>(operator new(size));
    buf->len = size;
}
} // namespace

struct udp {
    struct [[nodiscard]] received {
        std::unique_ptr<const char> buffer;
        size_t buffer_len;
        ssize_t nread;
        struct sockaddr_in addr;
        unsigned flags;
    };

    explicit udp(loop_service &loop) {
        uv_udp_init(loop.loop.get(), uv_udp.get());
        uv_udp->data = static_cast<void *>(this);
    }

    void recv_start() {
        if (recv_started) {
            return;
        }
        uv_udp_recv_start(uv_udp.get(), &alloc_buffer, &callback);
        recv_started = true;
    }

    void recv_stop() {
        if (!recv_started) {
            return;
        }
        uv_udp_recv_stop(uv_udp.get());
        recv_started = false;
    }

    int bind(uint16_t port) {
        struct sockaddr_in addr;
        assert(!uv_ip4_addr("0.0.0.0", port, &addr));
        return uv_udp_bind(uv_udp.get(), (sockaddr*)&addr, 0);
    }

    auto async_recv() {
        return queue.async_get();
    }

    ~udp() {
        recv_stop();
    }


private:
    std::unique_ptr<uv_udp_t> uv_udp { new uv_udp_t };
    async::queue<received, frg::stl_allocator> queue;
    bool recv_started = false;

    static void callback(uv_udp_t *handle, ssize_t nread,
        const uv_buf_t *buf, const struct sockaddr *addr,
        unsigned flags) {
        received r {
            // what the fuck
            std::unique_ptr<const char> { buf->base },
            buf->len,
            nread, {}, flags
        };
        if (addr != NULL && addr->sa_family == AF_INET) {
            std::memcpy(&r.addr, addr, sizeof(r.addr));
        }
        auto self = static_cast<udp *>(handle->data);
        self->queue.emplace(std::move(r));
    }
};
} // namespace uvasync
