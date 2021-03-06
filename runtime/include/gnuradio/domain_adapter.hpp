#pragma once

#include <zmq.hpp>
#include <thread>

#include <gnuradio/blocklib/node.hpp>
#include <gnuradio/buffer.hpp>

namespace gr {


enum class buffer_location_t { LOCAL = 0, REMOTE };
enum class buffer_preference_t { UPSTREAM, DOWNSTREAM };


enum class da_request_t : uint32_t {
    CANCEL = 0,
    WRITE_INFO,
    READ_INFO,
    POST_WRITE,
    POST_READ
};

/**
 * @brief Domain Adapter used internally by flowgraphs to handle domain crossings
 *
 * The Domain Adapter is both a node in that it is connected to blocks at the edges of a
 * subgraph as well as a buffer, since it is used for the scheduler to get the address
 * needed to read from or write to
 *
 * It holds a pointer to a buffer object which may be null if the adapter is not hosting
 * the buffer and relying on its peer to host the buffer
 */
class domain_adapter : public node, public buffer
{
protected:
    buffer_sptr _buffer = nullptr;
    buffer_location_t _buffer_loc;

    domain_adapter(buffer_location_t buf_loc)
        : node("domain_adapter"), _buffer_loc(buf_loc)
    {
    }

public:
    void set_buffer(buffer_sptr buf) { _buffer = buf; }
    buffer_sptr buffer() { return _buffer; }

    buffer_location_t buffer_location() { return _buffer_loc; }
    void set_buffer_location(buffer_location_t buf_loc) { _buffer_loc = buf_loc; }
};

typedef std::shared_ptr<domain_adapter> domain_adapter_sptr;

/**
 * @brief Uses a simple zmq socket to communicate buffer pointers between domains
 *
 */
class domain_adapter_zmq_rep_svr : public domain_adapter
{
private:
    zmq::context_t* d_context;
    zmq::socket_t* d_socket;

    bool d_connected = false;
    std::thread d_thread;

public:
    typedef std::shared_ptr<domain_adapter_zmq_rep_svr> sptr;
    static sptr make(const std::string& endpoint_uri, port_sptr other_port)
    {
        auto ptr = std::make_shared<domain_adapter_zmq_rep_svr>(
            domain_adapter_zmq_rep_svr(endpoint_uri));

        ptr->add_port(port_base::make("output",
                                      port_direction_t::OUTPUT,
                                      other_port->data_type(),
                                      port_type_t::STREAM,
                                      other_port->dims()));

        ptr->start_thread(ptr); // start thread with reference to shared pointer

        return ptr;
    }

    domain_adapter_zmq_rep_svr(const std::string& endpoint_uri)
        : domain_adapter(buffer_location_t::LOCAL)
    {
        d_context = new zmq::context_t(1);
        d_socket = new zmq::socket_t(*d_context,
                                     zmq::socket_type::rep); // server is a rep socket
        int time = 0;
        d_socket->setsockopt(ZMQ_LINGER, &time, sizeof(time));
        d_socket->bind(endpoint_uri);
    }

    void start_thread(sptr ptr) { d_thread = std::thread(run_thread, ptr); }

    static void run_thread(sptr top);

    virtual void* read_ptr() { return nullptr; }
    virtual void* write_ptr() { return nullptr; }

    // virtual int capacity() = 0;
    // virtual int size() = 0;

    virtual buffer_info_t read_info() { return _buffer->read_info(); }
    virtual buffer_info_t write_info()
    {
        // If I am the server, I am the buffer host
        return _buffer->write_info();
        // should not get called
        // throw std::runtime_error("write_info not valid for da_svr block"); // TODO
        // logging buffer_info_t ret; return ret;
    }
    virtual void cancel() { _buffer->cancel(); }

    virtual void post_read(int num_items) { return _buffer->post_read(num_items); }
    virtual void post_write(int num_items) { return _buffer->post_write(num_items); }

    // This is not valid for all buffers, e.g. domain adapters
    virtual void copy_items(buffer_sptr from, int nitems) {}
};


class domain_adapter_zmq_req_cli : public domain_adapter
{
    zmq::context_t* d_context;
    zmq::socket_t* d_socket;

public:
    typedef std::shared_ptr<domain_adapter_zmq_req_cli> sptr;
    static sptr make(const std::string& endpoint_uri, port_sptr other_port)
    {
        auto ptr = std::make_shared<domain_adapter_zmq_req_cli>(
            domain_adapter_zmq_req_cli(endpoint_uri));

        // Type of port is not known at compile time
        ptr->add_port(port_base::make("input",
                                      port_direction_t::INPUT,
                                      other_port->data_type(),
                                      port_type_t::STREAM,
                                      other_port->dims()));

        return ptr;
    }
    domain_adapter_zmq_req_cli(const std::string& endpoint_uri)
        : domain_adapter(buffer_location_t::REMOTE)
    {
        d_context = new zmq::context_t(1);
        d_socket = new zmq::socket_t(*d_context,
                                     zmq::socket_type::req); // server is a rep socket
        int time = 0;
        d_socket->setsockopt(ZMQ_LINGER, &time, sizeof(time));
        d_socket->connect(endpoint_uri);
    }

    void test();

    virtual void* read_ptr() { return nullptr; }
    virtual void* write_ptr() { return nullptr; }

    // virtual int capacity() = 0;
    // virtual int size() = 0;

    virtual buffer_info_t read_info();
    virtual buffer_info_t write_info();
    virtual void cancel();
    virtual void post_read(int num_items);
    virtual void post_write(int num_items);

    // This is not valid for all buffers, e.g. domain adapters
    // Currently domain adapters require fanout, and cannot copy from a shared output
    // across multiple domains
    virtual void copy_items(buffer_sptr from, int nitems) {}
};

} // namespace gr