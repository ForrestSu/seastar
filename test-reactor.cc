/*
 * test-reactor.cc
 *
 *  Created on: Aug 2, 2014
 *      Author: avi
 */

#include "reactor.hh"
#include <iostream>

struct test {
    std::unique_ptr<pollable_fd> listener;
    struct connection {
        connection(std::unique_ptr<pollable_fd> fd) : fd(std::move(fd)) {
            printf("on new connecting fd %p \n!", this->fd.get());
        }
        std::unique_ptr<pollable_fd> fd;
        char buffer[8192];
        void copy_data() {
            puts("copy_data()...");
            fd->read_some(buffer, sizeof(buffer)).then([this] (size_t n) {
                printf("Received %ld bytes! \n", n);
                if (n) {
                    fd->write_all(buffer, n).then([this, n] (size_t w) {
                        if (w == n) {
                            printf("下一轮 copy_data()\n");
                            copy_data();
                        }
                    });
                } else {
                    printf("Received 0 bytes!  delete this connect.\n");
                    delete this;
                }
            });
        }
    };
    void new_connection(accept_result&& accepted) {
        auto c = new connection(std::move(std::get<0>(accepted)));
        c->copy_data();
    }
    void start_accept() {
        puts("start_accept()...");
        the_reactor.accept(*listener).then([this] (accept_result&& ar) {
            puts("接收到一个新connect, 启动一个新流程；然后第一个 fd 继续start_accept()");
            new_connection(std::move(ar));
            start_accept();
        });
    }
};

int main(int ac, char** av)
{
    test t;
    ipv4_addr addr{{}, 10000};
    listen_options lo;
    lo.reuse_address = true;
    t.listener = the_reactor.listen(make_ipv4_address(addr), lo);
    t.start_accept();
    the_reactor.run();
    return 0;
}

