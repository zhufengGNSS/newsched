//#include <gnuradio/scheduler.hpp>
#include <gnuradio/scheduler.hpp>
// #include <boost/circular_buffer.hpp>
#include <gnuradio/domain_adapter.hpp>
#include <gnuradio/simplebuffer.hpp>
#include <thread> // std::thread
namespace gr {

namespace schedulers {

// This is a terrible scheduler just to pass data through the blocks
// and demonstrate the pluggable scheduler concept

class scheduler_simplestream : public scheduler
{
private:
    std::string _name;

public:
    static const int s_fixed_buf_size = 100;
    static const int s_min_items_to_process = 1;
    static constexpr int s_max_buf_items = s_fixed_buf_size / 2;

    scheduler_simplestream(const std::string name = "simplestream") : scheduler(name) {}
    ~scheduler_simplestream(){

    };

    void initialize(flat_graph_sptr fg)
    {
        d_fg = fg;

        // if (fg->is_flat())  // flatten

        // not all edges may be used
        for (auto e : fg->edges()) {
            // every edge needs a buffer
            d_edge_catalog[e.identifier()] = e;

            auto src_da_cast = std::dynamic_pointer_cast<domain_adapter>(e.src().node());
            auto dst_da_cast = std::dynamic_pointer_cast<domain_adapter>(e.dst().node());

            // Fixed assumption that buffer is contained in downstream domain_adapter
            // TODO - read the setting from the domain_adapter
            if (src_da_cast != nullptr) {
                if (src_da_cast->buffer_location() == buffer_location_t::LOCAL) {
                    auto buf = simplebuffer::make(s_fixed_buf_size, e.itemsize());
                    src_da_cast->set_buffer(buf);
                    auto tmp = std::dynamic_pointer_cast<buffer>(src_da_cast);
                    d_edge_buffers[e.identifier()] = tmp;
                } else {
                    d_edge_buffers[e.identifier()] =
                        std::dynamic_pointer_cast<buffer>(src_da_cast);
                }
            } else if (dst_da_cast != nullptr) {
                if (dst_da_cast->buffer_location() == buffer_location_t::LOCAL) {
                    auto buf = simplebuffer::make(s_fixed_buf_size, e.itemsize());
                    dst_da_cast->set_buffer(buf);
                    auto tmp = std::dynamic_pointer_cast<buffer>(dst_da_cast);
                    d_edge_buffers[e.identifier()] = tmp;
                } else {
                    d_edge_buffers[e.identifier()] =
                        std::dynamic_pointer_cast<buffer>(dst_da_cast);
                }

            } else {
                d_edge_buffers[e.identifier()] =
                    simplebuffer::make(s_fixed_buf_size, e.itemsize());
            }

            d_edge_buffers[e.identifier()]->set_name(e.identifier());
        }

        for (auto& b : fg->calc_used_blocks()) {
            b->set_scheduler(base());
            d_blocks.push_back(b);

            port_vector_t input_ports = b->input_stream_ports();
            port_vector_t output_ports = b->output_stream_ports();

            unsigned int num_input_ports = input_ports.size();
            unsigned int num_output_ports = output_ports.size();

            for (auto p : input_ports) {
                d_block_buffers[p] = std::vector<buffer_sptr>{};
                edge_vector_t ed = d_fg->find_edge(p);
                for (auto e : ed)
                    d_block_buffers[p].push_back(d_edge_buffers[e.identifier()]);
            }

            for (auto p : output_ports) {
                d_block_buffers[p] = std::vector<buffer_sptr>{};
                edge_vector_t ed = d_fg->find_edge(p);
                for (auto e : ed)
                    d_block_buffers[p].push_back(d_edge_buffers[e.identifier()]);
            }
        }
    }

    void start()
    {
        for (auto& b : d_blocks) {
            b->start();
        }
        d_thread = std::thread(thread_body, this);
    }

    void stop()
    {
        d_thread_stopped = true;
        d_thread.join();
        for (auto& b : d_blocks) {
            b->stop();
        }
    }

    void wait()
    {
        d_thread.join();
        for (auto& b : d_blocks) {
            b->done();
        }
    }

    void run()
    {
        start();
        wait();
    }

private:
    flat_graph_sptr d_fg;
    std::vector<block_sptr> d_blocks;
    std::map<std::string, edge> d_edge_catalog;
    std::map<std::string, buffer_sptr> d_edge_buffers;
    // std::map<block_sptr, std::map<block::io, std::map<int, simplebuffer::sptr>>>
    //     d_block_buffers; // store the block buffers
    std::map<port_sptr, std::vector<buffer_sptr>> d_block_buffers;
    // std::map<port_sptr, simplebuffer::sptr> d_block_buffers; // store the block buffers
    std::thread d_thread;
    bool d_thread_stopped = false;

    static void thread_body(scheduler_simplestream* top)
    {
        int num_empty = 0;
        while (!top->d_thread_stopped) {
            // std::cout << top->name() << ":while" << std::endl;

            // do stuff with the blocks
            bool did_work = false;
            bool go_on_ahead = false;
            // TODO - line up at_sample numbers with work functions
            for (auto const& b : top->d_blocks) {
                // handle parameter changes - queues need to be made thread safe
                while (!top->param_change_queue.empty()) {
                    auto item = top->param_change_queue.front();
                    if (item.block_id == b->alias()) {
                        b->on_parameter_change(item.param_action);

                        if (item.cb_fcn != nullptr)
                            item.cb_fcn(item.param_action);

                        top->param_change_queue.pop();
                    } else {
                        // no parameter changes for this block
                        go_on_ahead = true;
                        break;
                    }
                }

                // if (go_on_ahead)
                // continue;

                // handle parameter queries
                while (!top->param_query_queue.empty()) {
                    auto item = top->param_query_queue.front();
                    if (item.block_id == b->alias()) {
                        b->on_parameter_query(item.param_action);

                        if (item.cb_fcn != nullptr)
                            item.cb_fcn(item.param_action);

                        top->param_query_queue.pop();
                    } else {
                        // no parameter changes for this block
                        go_on_ahead = true;
                        break;
                    }
                }

                // handle general callbacks
                while (!top->callback_queue.empty()) {
                    auto item = top->callback_queue.front();
                    if (item.block_id == b->alias()) {
                        auto cbs = item.cb_struct;
                        auto ret = b->callbacks()[cbs.callback_name](cbs.args);

                        cbs.return_val = ret;
                        if (item.cb_fcn != nullptr)
                            item.cb_fcn(cbs);

                        top->callback_queue.pop();
                    } else {
                        // no parameter changes for this block
                        go_on_ahead = true;
                        break;
                    }
                }

                std::vector<block_work_input> work_input;   //(num_input_ports);
                std::vector<block_work_output> work_output; //(num_output_ports);

                std::vector<buffer_sptr> bufs;
                // for each input port of the block
                bool ready = true;
                for (auto p : b->input_stream_ports()) {
                    auto p_buf = top->d_block_buffers[p][0];

                    auto read_info = p_buf->read_info();
                    bufs.push_back(p_buf);

                    if (read_info.n_items < s_min_items_to_process) {
                        ready = false;
                        break;
                    }

                    std::vector<tag_t> tags; // needs to be associated with edge buffers
                    work_input.push_back(
                        block_work_input(read_info.n_items, 0, read_info.ptr, tags));
                }

                if (!ready) {
                    // clean up the buffers that we now won't be using
                    for (auto buf : bufs) {
                        buf->cancel();
                    }
                    continue;
                }

                // for each output port of the block
                for (auto p : b->output_stream_ports()) {

                    // When a block has multiple output buffers, it adds the restriction
                    // that the work call can only produce the minimum available across
                    // the buffers.

                    int max_output_buffer = std::numeric_limits<int>::max();

                    void* write_ptr = nullptr;
                    for (auto p_buf : top->d_block_buffers[p]) {
                        auto write_info = p_buf->write_info();
                        bufs.push_back(p_buf);

                        if (write_info.n_items < s_max_buf_items) {
                            ready = false;
                            break;
                        }

                        int tmp_buf_size = write_info.n_items;
                        if (tmp_buf_size < max_output_buffer)
                            max_output_buffer = tmp_buf_size;

                        // store the first buffer
                        if (!write_ptr)
                            write_ptr = write_info.ptr;
                    }

                    max_output_buffer = std::min(max_output_buffer, s_max_buf_items);
                    std::vector<tag_t> tags; // needs to be associated with edge buffers

                    work_output.push_back(
                        block_work_output(max_output_buffer, 0, write_ptr, tags));
                }

                if (!ready) {
                    // clean up the buffers that we now won't be using
                    for (auto buf : bufs) {
                        buf->cancel();
                    }
                    continue;
                }

                if (ready) {
                    // std::cout << top->name() << ":" << b->name() << ":work" << std::endl;
                    work_return_code_t ret = b->do_work(work_input, work_output);
                    if (ret == work_return_code_t::WORK_OK) {

                        int i = 0;
                        for (auto p : b->input_stream_ports()) {
                            auto p_buf =
                                top->d_block_buffers[p]
                                                    [0]; // only one buffer per input port

                            p_buf->post_read(work_input[i].n_consumed);
                            i++;
                        }

                        i = 0;
                        for (auto p : b->output_stream_ports()) {
                            int j = 0;
                            for (auto p_buf : top->d_block_buffers[p]) {
                                if (j > 0) {
                                    p_buf->copy_items(top->d_block_buffers[p][0],
                                                      work_output[i].n_produced);
                                }
                                j++;
                            }
                            for (auto p_buf : top->d_block_buffers[p]) {
                                p_buf->post_write(work_output[i].n_produced);
                            }
                            i++;
                        }
                        // update the buffers according to the items produced

                        did_work = true;
                    } else {
                        for (auto buf : bufs) {
                            buf->cancel();
                        }
                    }
                }
            }


            if (!did_work) {
                // break;  // TODO - make a timeout instead of immediate stop
                num_empty++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (num_empty >= 10) {
                    break;
                }
            }
        }
    }
};
} // namespace schedulers
} // namespace gr
