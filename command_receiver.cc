
#include <algorithm>
#include <sys/socket.h>

#include "command.h"
#include "command_receiver.h"

CommandReceiver::CommandReceiver(std::shared_ptr<CommandChannel> channel)
    : context_(channel->context_),control_sock_(channel->control_sock_),data_sock_(channel->data_sock_)
{
}

int CommandReceiver::RecvPrivateCommand(std::shared_ptr<Command> command)
{
    LOGE("we don't expect to recv any private command.(%s)\n",command->cmd.c_str());
    ASSERT(0);
    return -1;
}

EchoCommandReceiver::EchoCommandReceiver(std::shared_ptr<CommandChannel> channel)
    : send_count_(0),recv_count_(0), running_(false),is_stopping_(false),
      command_(std::dynamic_pointer_cast<EchoCommand>(channel->command_)), CommandReceiver(channel)
{
}

int EchoCommandReceiver::Start()
{
    running_ = true;
    LOGV("EchoCommandReceiver Start.\n");
    context_->SetReadFd(context_->data_fd);
    return 0;
}
int EchoCommandReceiver::Stop()
{
    ASSERT(running_);
    running_ = false;
    LOGV("EchoCommandReceiver Stop.\n");
    context_->ClrReadFd(context_->data_fd);
    context_->ClrWriteFd(context_->data_fd);
    // allow send result
    context_->SetWriteFd(context_->control_fd);
    return 0;
}
int EchoCommandReceiver::Send()
{
    LOGV("EchoCommandReceiver Send.\n");
    int result=0;
    context_->ClrWriteFd(context_->data_fd);
    ASSERT(data_queue_.size()>0);
    while (data_queue_.size()>0)
    {
        auto buf = data_queue_.front();
        // use sync method to send extra data.
        if ((result = data_sock_->Send(&buf[0], buf.length())) < 0)
        {
            return -1;
        }
        send_count_++;
        data_queue_.pop();    
    }
    
    return result;
}
int EchoCommandReceiver::Recv()
{
    LOGV("EchoCommandReceiver Recv.\n");
    int result;
    ASSERT(running_);
    std::string buf(1024*64,0);
    if ((result = data_sock_->Recv(&buf[0], buf.length())) < 0)
    {
        return -1;
    }
    buf.resize(result);
    data_queue_.push(buf);
    context_->SetWriteFd(context_->data_fd);
    // context_->ClrReadFd(context_->data_fd);
    recv_count_++;
    return 0;
}

int EchoCommandReceiver::SendPrivateCommand()
{
    int result;
    if(data_queue_.size()>0)
    {
        LOGW("echo stop: drop %ld data.\n",data_queue_.size());
        ASSERT(0);
    }

    context_->ClrWriteFd(context_->control_fd);
    auto command = std::make_shared<ResultCommand>();
    NetStat stat = {};
    stat.recv_packets = recv_count_;
    stat.send_packets = send_count_;
    auto cmd = command->Serialize(stat);
    if ((result = control_sock_->Send(cmd.c_str(), cmd.length())) < 0)
    {
        return -1;
    }
    return result;
}

RecvCommandReceiver::RecvCommandReceiver(std::shared_ptr<CommandChannel> channel)
    : length_(0), recv_count_(0), recv_bytes_(0), speed_(0), min_speed_(-1), max_speed_(0), 
      running_(false),is_stopping_(false),
      command_(std::dynamic_pointer_cast<RecvCommand>(channel->command_)), CommandReceiver(channel) {}

int RecvCommandReceiver::Start()
{
    LOGV("RecvCommandReceiver Start.\n");
    context_->SetReadFd(context_->data_fd);
    start_ = high_resolution_clock::now();
    begin_ = high_resolution_clock::now();
    running_ = true;
    return 0;
}
int RecvCommandReceiver::Stop()
{
    LOGV("RecvCommandReceiver Stop.\n");
    context_->ClrReadFd(context_->data_fd);
    context_->ClrWriteFd(context_->data_fd);
    //context_->ClrReadFd(context_->control_fd);
    // allow to send stop command.
    context_->SetWriteFd(context_->control_fd);
    running_ = false;
    return 0;
}
int RecvCommandReceiver::Recv()
{
    LOGV("RecvCommandReceiver Recv.\n");
    ASSERT(running_);
    int result;
    if ((result = data_sock_->Recv(buf_, sizeof(buf_))) < 0)
    {
        return -1;
    }
    end_ = high_resolution_clock::now();
    stop_ = high_resolution_clock::now();
    recv_bytes_ += result;
    recv_count_++;
    auto seconds = duration_cast<duration<double>>(end_ - begin_).count();
    if (seconds >= 1)
    {
        int64_t speed = recv_bytes_ / seconds;
        min_speed_ = min_speed_ == -1 ? speed : std::min(min_speed_, speed);
        max_speed_ = std::max(max_speed_, speed);
        begin_ = high_resolution_clock::now();
    }
    return result;
}

int RecvCommandReceiver::SendPrivateCommand()
{
    int result;
    context_->ClrWriteFd(context_->control_fd);
    NetStat stat = {};
    stat.recv_bytes = recv_bytes_;
    stat.recv_packets = recv_count_;
    auto seconds = duration_cast<duration<double>>(stop_ - start_).count();
    if (seconds >= 0.001)
    {
        stat.recv_time = seconds * 1000;
        stat.recv_speed = recv_bytes_ / seconds;
        stat.max_recv_speed = max_speed_;
        if (min_speed_ > 0)
            stat.min_recv_speed = min_speed_;
    }

    auto command = std::make_shared<ResultCommand>();
    auto cmd = command->Serialize(stat);
    if ((result = Sock::Send(context_->control_fd, cmd.c_str(), cmd.length())) < 0)
    {
        return -1;
    }
    return 0;
}