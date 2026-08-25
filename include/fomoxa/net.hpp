#ifndef FOMOXA_NET_HPP
#define FOMOXA_NET_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fomoxa/net.h"

namespace fomoxa {

inline uint64_t now_ms() {
    return fmx_now_ms();
}

inline fmx_config default_config() {
    fmx_config config;
    fmx_config_defaults(&config);
    return config;
}
class ByteView {
public:
    ByteView() = default;
    ByteView(const uint8_t *data, size_t size) : data_(data), size_(size) {}
    ByteView(const std::vector<uint8_t> &bytes) : data_(bytes.data()), size_(bytes.size()) {}

    const uint8_t *data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const uint8_t *begin() const { return data_; }
    const uint8_t *end() const { return data_ + size_; }

private:
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
};
class Event {
public:
    explicit Event(const fmx_event &raw) : raw_(&raw) {}

    fmx_event_kind kind() const { return raw_->kind; }
    uint64_t peer() const { return raw_->peer; }
    uint32_t message_id() const { return raw_->message_id; }

    ByteView payload() const { return ByteView(raw_->payload, raw_->payload_len); }

    std::vector<uint8_t> payload_copy() const {
        return std::vector<uint8_t>(raw_->payload, raw_->payload + raw_->payload_len);
    }

    fmx_disconnect disconnect_reason() const { return (fmx_disconnect)raw_->reason; }
    fmx_handshake_failure handshake_failure() const { return (fmx_handshake_failure)raw_->reason; }

    const fmx_event &c_ref() const { return *raw_; }

private:
    const fmx_event *raw_;
};

class Events {
public:
    class iterator {
    public:
        explicit iterator(const fmx_event *cursor) : cursor_(cursor) {}
        Event operator*() const { return Event(*cursor_); }
        iterator &operator++() {
            ++cursor_;
            return *this;
        }
        bool operator!=(const iterator &other) const { return cursor_ != other.cursor_; }

    private:
        const fmx_event *cursor_;
    };

    Events(const fmx_event *first, size_t count) : first_(first), count_(count) {}

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    Event operator[](size_t index) const { return Event(first_[index]); }
    iterator begin() const { return iterator(first_); }
    iterator end() const { return iterator(first_ + count_); }

private:
    const fmx_event *first_;
    size_t count_;
};
class Transport {
public:
    Transport() { clear(); }
    ~Transport() { reset(); }

    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;

    Transport(Transport &&other) noexcept : raw_(other.raw_) { other.clear(); }
    Transport &operator=(Transport &&other) noexcept {
        if (this != &other) {
            reset();
            raw_ = other.raw_;
            other.clear();
        }
        return *this;
    }

    static std::optional<Transport> tcp(const std::string &host, uint16_t port) {
        fmx_transport raw;
        if (fmx_tcp_connect(host.c_str(), port, &raw) != FMX_OK) {
            return std::nullopt;
        }
        return Transport(raw);
    }

    static std::optional<Transport> udp(const std::string &host, uint16_t port) {
        fmx_transport raw;
        if (fmx_udp_connect(host.c_str(), port, &raw) != FMX_OK) {
            return std::nullopt;
        }
        return Transport(raw);
    }

    bool valid() const { return raw_.vtable != nullptr; }
    bool is_stream() const { return raw_.vtable->kind(&raw_) == FMX_TRANSPORT_STREAM; }

    fmx_transport release() {
        fmx_transport raw = raw_;
        clear();
        return raw;
    }

    void reset() {
        if (raw_.vtable != nullptr) {
            raw_.vtable->close_hard(&raw_);
            clear();
        }
    }

private:
    explicit Transport(fmx_transport raw) : raw_(raw) {}
    void clear() {
        raw_.vtable = nullptr;
        raw_.state = nullptr;
    }

    fmx_transport raw_;
};
class Listener {
public:
    Listener() { clear(); }
    ~Listener() { reset(); }

    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;

    Listener(Listener &&other) noexcept : raw_(other.raw_), port_(other.port_) { other.clear(); }
    Listener &operator=(Listener &&other) noexcept {
        if (this != &other) {
            reset();
            raw_ = other.raw_;
            port_ = other.port_;
            other.clear();
        }
        return *this;
    }

    static std::optional<Listener> tcp(const std::string &host, uint16_t port) {
        fmx_listener raw;
        if (fmx_tcp_listen(host.c_str(), port, &raw) != FMX_OK) {
            return std::nullopt;
        }
        return Listener(raw, fmx_tcp_listener_port(&raw));
    }

    static std::optional<Listener> udp(const std::string &host, uint16_t port) {
        fmx_listener raw;
        if (fmx_udp_listen(host.c_str(), port, &raw) != FMX_OK) {
            return std::nullopt;
        }
        return Listener(raw, fmx_udp_listener_port(&raw));
    }

    bool valid() const { return raw_.vtable != nullptr; }
    uint16_t port() const { return port_; }

    fmx_listener release() {
        fmx_listener raw = raw_;
        clear();
        return raw;
    }

    void reset() {
        if (raw_.vtable != nullptr) {
            raw_.vtable->close(&raw_);
            clear();
        }
    }

private:
    Listener(fmx_listener raw, uint16_t port) : raw_(raw), port_(port) {}
    void clear() {
        raw_.vtable = nullptr;
        raw_.state = nullptr;
        port_ = 0;
    }

    fmx_listener raw_;
    uint16_t port_ = 0;
};
class Connection {
public:
    Connection() = default;
    ~Connection() { reset(); }

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    Connection(Connection &&other) noexcept : raw_(other.raw_) { other.raw_ = nullptr; }
    Connection &operator=(Connection &&other) noexcept {
        if (this != &other) {
            reset();
            raw_ = other.raw_;
            other.raw_ = nullptr;
        }
        return *this;
    }

    static std::optional<Connection> create(Transport &&transport, const fmx_schema *schema,
                                            const fmx_config *config = nullptr,
                                            uint64_t started_ms = fmx_now_ms()) {
        fmx_transport raw = transport.release();
        if (raw.vtable == nullptr) {
            return std::nullopt;
        }
        fmx_connection *connection = fmx_connection_create(raw, schema, config, started_ms);
        if (connection == nullptr) {
            raw.vtable->close_hard(&raw);
            return std::nullopt;
        }
        return Connection(connection);
    }

    Events tick(uint64_t current_ms) {
        size_t count = 0;
        fmx_connection_tick(raw_, current_ms);
        const fmx_event *events = fmx_connection_events(raw_, &count);
        return Events(events, count);
    }

    fmx_result send(uint32_t message_id, ByteView payload) {
        return fmx_connection_send(raw_, message_id, payload.data(), payload.size());
    }

    void close() { fmx_connection_close(raw_); }

    fmx_state state() const { return fmx_connection_state(raw_); }
    bool ready() const { return fmx_connection_ready(raw_); }
    bool closed() const { return fmx_connection_state(raw_) == FMX_STATE_CLOSED; }
    bool congested() const { return fmx_connection_congested(raw_); }

    void shrink_to_fit() { fmx_connection_shrink(raw_); }

    bool valid() const { return raw_ != nullptr; }
    fmx_connection *c_ptr() const { return raw_; }

    void reset() {
        if (raw_ != nullptr) {
            fmx_connection_destroy(raw_);
            raw_ = nullptr;
        }
    }

private:
    explicit Connection(fmx_connection *raw) : raw_(raw) {}

    fmx_connection *raw_ = nullptr;
};
class Server {
public:
    Server() = default;
    ~Server() { reset(); }

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    Server(Server &&other) noexcept : raw_(other.raw_) { other.raw_ = nullptr; }
    Server &operator=(Server &&other) noexcept {
        if (this != &other) {
            reset();
            raw_ = other.raw_;
            other.raw_ = nullptr;
        }
        return *this;
    }

    static std::optional<Server> create(Listener &&listener, const fmx_schema *schema,
                                        const fmx_config *config = nullptr) {
        fmx_listener raw = listener.release();
        if (raw.vtable == nullptr) {
            return std::nullopt;
        }
        fmx_server *server = fmx_server_create(raw, schema, config);
        if (server == nullptr) {
            raw.vtable->close(&raw);
            return std::nullopt;
        }
        return Server(server);
    }

    Events tick(uint64_t current_ms) {
        size_t count = 0;
        fmx_server_tick(raw_, current_ms);
        const fmx_event *events = fmx_server_events(raw_, &count);
        return Events(events, count);
    }

    fmx_result send(uint64_t peer, uint32_t message_id, ByteView payload) {
        return fmx_server_send(raw_, peer, message_id, payload.data(), payload.size());
    }

    void broadcast(uint32_t message_id, ByteView payload) {
        fmx_server_broadcast(raw_, message_id, payload.data(), payload.size());
    }

    void disconnect(uint64_t peer) { fmx_server_disconnect(raw_, peer); }

    size_t peer_count() const { return fmx_server_peer_count(raw_); }
    uint64_t peer_at(size_t index) const { return fmx_server_peer_at(raw_, index); }
    bool peer_ready(uint64_t peer) const { return fmx_server_peer_ready(raw_, peer); }

    void shrink_to_fit() { fmx_server_shrink(raw_); }

    bool valid() const { return raw_ != nullptr; }
    fmx_server *c_ptr() const { return raw_; }

    void reset() {
        if (raw_ != nullptr) {
            fmx_server_destroy(raw_);
            raw_ = nullptr;
        }
    }

private:
    explicit Server(fmx_server *raw) : raw_(raw) {}

    fmx_server *raw_ = nullptr;
};

} // namespace fomoxa

#endif /* FOMOXA_NET_HPP */
