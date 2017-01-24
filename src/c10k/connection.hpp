//
// Created by lz on 1/24/17.
//

#ifndef C10K_SERVER_CONNECTION_HPP
#define C10K_SERVER_CONNECTION_HPP


#include "event_loop.hpp"
#include <mutex>
#include <memory>
#include <queue>
#include <vector>
#include <iterator>
#include <cstddef>
#include <atomic>
#include <functional>
#include <system_error>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace c10k
{
    namespace detail
    {
        struct ConnWReq
        {
            std::function<void()> callback;
            using CallbackT = decltype(callback);

            void exec_callback()
            {
                if (callback)
                    callback();
            }
            const std::vector<char> buf;
            int offset = 0;

            template<typename ItT>
            ConnWReq(ItT st, ItT ed):
                    buf(st, ed)
            {}

            template<typename ItT>
            ConnWReq(ItT st, ItT ed, CallbackT cb):
                    buf(st, ed), callback(cb)
            {}

        };

        struct ConnRReq
        {
            std::vector<char> buf;
            std::function<void(char *, char *)> callback;
            using CallbackT = decltype(callback);

            const int requested_len;

            ConnRReq(int requested_len, CallbackT cb):
                    callback(cb), requested_len(requested_len)
            {
                buf.reserve(requested_len);
            }

            void exec_callback(char *st, char *ed)
            {
                if (callback)
                    callback(st, ed);
            }
        };
    }
    class Connection: std::enable_shared_from_this<Connection>
    {
    private:
        int fd;
        EventLoop &el;
        std::queue<detail::ConnWReq> w_buffer;
        std::queue<detail::ConnRReq> r_buffer;
        std::recursive_mutex mutex;
        std::atomic_bool registered;

        std::shared_ptr<spdlog::logger> logger;
        using LoggerT = decltype(logger);
    public:
        Connection(int fd, EventLoop &el, const LoggerT &logger, bool registered = true):
                fd(fd), el(el), logger(logger), registered(registered)
        {
            logger->debug("New connection created with fd={}", fd);
        }

        int getFD() const
        {
            return fd;
        }

        template<typename InputIt>
        void write_async(InputIt st, InputIt ed)
        {
            logger->debug("Write_async called");
            detail::ConnWReq wReq {st, ed};
            std::lock_guard<std::recursive_mutex> lk(mutex);
            w_buffer.push(std::move(wReq));
            register_event();
        }

        template<typename InputIt>
        void write_async_then(InputIt st, InputIt ed, detail::ConnWReq::CallbackT callback)
        {
            logger->debug("write_async_then called");
            detail::ConnWReq wReq {st, ed, callback};
            std::lock_guard<std::recursive_mutex> lk(mutex);
            w_buffer.push(std::move(wReq));
            register_event();
        }

        // read_async: read data then write it into OIterator iit
        // iit could be a char* or back_inserter, etc.
        template<typename InsertItT>
        void read_async(InsertItT iit, int len)
        {
            logger->debug("read_async called with len={}", len);
            detail::ConnRReq rReq {len, [iit] (char *st, char *ed) {
                std::move(st, ed, iit);
            }};
            std::lock_guard<std::recursive_mutex> lk(mutex);
            r_buffer.push(rReq);
            register_event();
        }

        template<typename InsertItT>
        void read_async_then(InsertItT iit, int len, detail::ConnRReq::CallbackT callback)
        {
            logger->debug("read_async_then called with len={}", len);
            detail::ConnRReq rReq {len, [iit, callback] (char *st, char *ed) {
                std::move(st, ed, iit);
                callback(st, ed);
            }};
            std::lock_guard<std::recursive_mutex> lk(mutex);
            r_buffer.push(rReq);
            register_event();
        }

        void register_event();

        void remove_event();
    private:

        void handle_read();

        void handle_write();

        void event_handler(const Event &e);

        friend class ConnectionTester1;
        friend class ConnectionTester2;

    };
}
#endif //C10K_SERVER_CONNECTION_HPP