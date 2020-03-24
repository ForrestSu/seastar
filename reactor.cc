/*
 * reactor.cc
 *
 *  Created on: Aug 1, 2014
 *      Author: avi
 */

#include "reactor.hh"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>

reactor::reactor()
    : _epollfd(epoll_create1(EPOLL_CLOEXEC)) {
    assert(_epollfd != -1);
}

reactor::~reactor() {
    ::close(_epollfd);
}

void reactor::epoll_add_in(pollable_fd& pfd, std::unique_ptr<task> t) {
    auto ctl = pfd.events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    pfd.events |= EPOLLIN;
    assert(!pfd.pollin);
    pfd.pollin = std::move(t);
    ::epoll_event eevt;
    eevt.events = pfd.events;
    eevt.data.ptr = &pfd;
    int r = ::epoll_ctl(_epollfd, ctl, pfd.fd, &eevt);
    assert(r == 0);
    printf("\033[47;34m为 pollable_fd = %p 关联一个 in_task %p \033[0m\n", &pfd, pfd.pollin.get());
}

void reactor::epoll_add_out(pollable_fd& pfd, std::unique_ptr<task> t) {
    auto ctl = pfd.events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    pfd.events |= EPOLLOUT;
    assert(!pfd.pollout);
    pfd.pollout = std::move(t);
    ::epoll_event eevt;
    eevt.events = pfd.events;
    eevt.data.ptr = &pfd;
    int r = ::epoll_ctl(_epollfd, ctl, pfd.fd, &eevt);
    assert(r == 0);
    printf("\033[46;34m为 pollable_fd = %p 关联一个 out_task %p \033[0m\n", &pfd, pfd.pollout.get());
}

void reactor::forget(pollable_fd& fd) {
    if (fd.events) {
        ::epoll_ctl(_epollfd, EPOLL_CTL_DEL, fd.fd, nullptr);
    }
}

std::unique_ptr<pollable_fd>
reactor::listen(socket_address sa, listen_options opts) {
    int fd = ::socket(sa.u.sa.sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(fd != -1);
    if (opts.reuse_address) {
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }
    int r = ::bind(fd, &sa.u.sa, sizeof(sa.u.sas));
    assert(r != -1);
    ::listen(fd, 100);
    return std::unique_ptr<pollable_fd>(new pollable_fd(fd));
}

void reactor::run() {
    puts("reactor::run()...");
    std::vector<std::unique_ptr<task>> current_tasks;
    while (true) {
        printf("pending_task有 %ld 个任务!\n", _pending_tasks.size());
        //因为每执行完一批任务,可能产生新的task,这里将执行完所有的 pending_task
        while (!_pending_tasks.empty()) {
            printf("========= Exec batch task %ld ========\n", _pending_tasks.size());
            std::swap(_pending_tasks, current_tasks);
            for (auto&& tsk : current_tasks) {
                auto task_ptr = tsk.get();
                printf("\033[31m >>>>>>> 执行 task %p ...\033[0m\n", task_ptr);
                tsk->run();
                tsk.reset();
                printf("<<<<<<<<<< task %p 执行完毕\n", task_ptr);
            }
            current_tasks.clear();
        }
        puts("epoll_wait()....");
        std::array<epoll_event, 128> eevt;
        int nr = ::epoll_wait(_epollfd, eevt.data(), eevt.size(), -1);
        printf("\033[36m收到事件 epoll_wait() nready = %d \033[0m\n", nr);
        if(nr == -1) {
            printf("nr == -1, errno = %d continue...\n", errno);
            continue;
        }
        assert(nr != -1);
        for (int i = 0; i < nr; ++i) {
            auto& evt = eevt[i];
            auto pfd = static_cast<pollable_fd*>(evt.data.ptr);
            auto events = evt.events;
            std::unique_ptr<task> t_in, t_out;
            if (events & EPOLLIN) {
                pfd->events &= ~EPOLLIN;
                add_task(std::move(pfd->pollin));
            }
            if (events & EPOLLOUT) {
                pfd->events &= ~EPOLLOUT;
                add_task(std::move(pfd->pollout));
            }
            //TODO 这里代码很严谨: 如果我们同时关心一个fd的读和写，如果只触发了一个事件
            //此时op应该为EPOLL_CTL_MOD，如果触发了，则需要从红黑树中删除fd，等待下一个时间绑定。
            evt.events = pfd->events;
            auto op = evt.events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ::epoll_ctl(_epollfd, op, pfd->fd, &evt);
        }
    }
}

socket_address make_ipv4_address(ipv4_addr addr) {
    socket_address sa;
    sa.u.in.sin_family = AF_INET;
    sa.u.in.sin_port = htons(addr.port);
    std::memcpy(&sa.u.in.sin_addr, addr.host, 4);
    return sa;
}

reactor the_reactor;
