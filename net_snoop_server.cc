
#include <unistd.h>
#include <sys/un.h>

#include <memory>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <functional>
#include <thread>

#include "context2.h"
#include "net_snoop_server.h"

int NetSnoopServer::Run()
{
    int result;
    int time_millseconds = 0;
    timespec timeout = {0, 0};
    timespec *timeout_ptr = NULL;
    fd_set read_fdsets, write_fdsets;
    FD_ZERO(&read_fdsets);
    FD_ZERO(&write_fdsets);

    result = pipe(pipefd_);
    ASSERT(result == 0);

    result = StartListen();
    ASSERT(result >= 0);

    context_->SetReadFd(pipefd_[0]);
    LOGV("pipe fd: %d,%d\n", pipefd_[0], pipefd_[1]);

    high_resolution_clock::time_point begin = high_resolution_clock::now();
    high_resolution_clock::time_point start, end;
    while (true)
    {
        if (timeout_ptr)
        {
            for (auto &peer : context_->peers)
            {
                end = high_resolution_clock::now();
                peer->Timeout(duration_cast<milliseconds>(end - start).count());
                //ASSERT(result>=0);
            }
        }
        memcpy(&read_fdsets, &context_->read_fds, sizeof(read_fdsets));
        memcpy(&write_fdsets, &context_->write_fds, sizeof(write_fdsets));
        timeout_ptr = NULL;
        time_millseconds = INT32_MAX;

        for (auto &peer : context_->peers)
        {
            if (peer->GetTimeout() > 0 && time_millseconds > peer->GetTimeout())
            {
                time_millseconds = peer->GetTimeout();
                timeout.tv_sec = time_millseconds / 1000;
                timeout.tv_nsec = (time_millseconds % 1000) * 1000 * 1000;
                timeout_ptr = &timeout;
                start = high_resolution_clock::now();
                LOGV("Set timeout: %ld\n", timeout.tv_sec * 1000 + timeout.tv_nsec / 1000 / 1000);
            }
        }

#ifdef _DEBUG
        for (int i = 0; i < sizeof(fd_set); i++)
        {
            if (FD_ISSET(i, &context_->read_fds))
            {
                LOGV("want read: %d\n", i);
            }
            if (FD_ISSET(i, &context_->write_fds))
            {
                LOGV("want write: %d\n", i);
            }
        }
#endif
        //LOGV("selecting\n");
        result = pselect(context_->max_fd + 1, &read_fdsets, &write_fdsets, NULL, timeout_ptr, NULL);
        LOGV("selected---------------\n");
#ifdef _DEBUG
        for (int i = 0; i < sizeof(fd_set); i++)
        {
            if (FD_ISSET(i, &read_fdsets))
            {
                LOGV("can read: %d\n", i);
            }
            if (FD_ISSET(i, &write_fdsets))
            {
                LOGV("can write: %d\n", i);
            }
        }
#endif
        if (result < 0)
        {
            // Todo: close socket
            LOGE("select error: %s(errno: %d)\n", strerror(errno), errno);
            return -1;
        }

        if (result == 0)
        {
            LOGV("time out: %d\n", time_millseconds);
            continue;
        }
        if (FD_ISSET(pipefd_[0], &read_fdsets))
        {
            std::string cmd(64, '\0');
            result = read(pipefd_[0], &cmd[0], cmd.length());
            ASSERT(result > 0);
            cmd.resize(result);
            LOGV("Pipe read data: %s\n", cmd.c_str());
            auto command = Command::CreateCommand(cmd);
            ASSERT(command);

            for (auto &peer : context_->peers)
            {
                peer->SetCommand(command);
            }
        }
        if (FD_ISSET(context_->control_fd, &read_fdsets))
        {
            result = AceeptNewPeer();
            ASSERT(result >= 0);
        }

        for (auto &peer : context_->peers)
        {
            LOGV("peer: cfd= %d, dfd= %d\n", peer->GetControlFd(), peer->GetDataFd());
            if (FD_ISSET(peer->GetControlFd(), &write_fdsets))
            {
                LOGV("Sending Command: cfd=%d\n", peer->GetControlFd());
                peer->SendCommand();
            }
            if (FD_ISSET(peer->GetControlFd(), &read_fdsets))
            {
                LOGV("Recving Command: cfd=%d\n", peer->GetControlFd());
                peer->RecvCommand();
            }
            if (peer->GetDataFd() < 0)
                continue;
            if (FD_ISSET(peer->GetDataFd(), &write_fdsets))
            {
                LOGV("Sending Data: dfd=%d\n", peer->GetDataFd());
                peer->SendData();
            }
            if (FD_ISSET(peer->GetDataFd(), &read_fdsets))
            {
                LOGV("Recving Data: dfd=%d\n", peer->GetDataFd());
                peer->RecvData();
            }
        }
    }

    return 0;
}
int NetSnoopServer::SendCommand(std::string cmd)
{
    auto command = Command::CreateCommand(cmd);
    if (!command)
    {
        LOGE("error command: %s\n",cmd.c_str());
        return ERR_ILLEGAL_PARAM;
    }
    LOGV("Send cmd: %s\n", cmd.c_str());
    write(pipefd_[1], cmd.c_str(), cmd.length());
    return 0;
}

int NetSnoopServer::StartListen()
{
    int result;
    result = listen_tcp_->Initialize();
    ASSERT(result >= 0);
    result = listen_tcp_->Bind(option_->ip_local, option_->port);
    ASSERT(result >= 0);
    result = listen_tcp_->Listen(MAX_CLINETS);
    ASSERT(result >= 0);

    LOGW("listen on %s:%d\n",option_->ip_local,option_->port);

    context_->control_fd = listen_tcp_->GetFd();
    context_->SetReadFd(listen_tcp_->GetFd());

    return 0;
}
int NetSnoopServer::AceeptNewPeer()
{
    int fd;

    if ((fd = listen_tcp_->Accept()) <= 0)
    {
        return -1;
    }

    auto tcp = std::make_shared<Tcp>(fd);
    auto peer = std::make_shared<Peer>(tcp, context_);
    context_->peers.push_back(peer);
    context_->SetReadFd(fd);

    std::string ip;
    int port;
    tcp->GetPeerAddress(ip,port);
    return fd;
}
