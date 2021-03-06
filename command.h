#pragma once

#include <map>
#include <vector>
#include <memory>
#include <sstream>
#include <functional>
#include <unistd.h>
#include <math.h>

#include "command_receiver.h"
#include "command_sender.h"
#include "netsnoop.h"

#define MAX_CMD_LENGTH 1024
#define MAX_TOKEN_LENGTH 10
// time in microseconds wait to give a chance for client receive all data
#define STOP_WAIT_TIME 500*1000

// use as an identity of a main command
#define VISIABLE_LATTERS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

class CommandFactory;
class Command;
class NetStat;

extern std::map<std::string, int> g_cmd_map;

using CommandCallback = std::function<void(const Command *, std::shared_ptr<NetStat>)>;

#pragma region CommandFactory

using CommandArgs = std::map<std::string, std::string>;
using Ctor = CommandFactory *;
using CommandContainer = std::map<std::string, Ctor>;

class CommandFactory
{
public:
    static std::shared_ptr<Command> New(const std::string &cmd)
    {
        ASSERT_RETURN(cmd.length() <= MAX_CMD_LENGTH - MAX_TOKEN_LENGTH, NULL, "cmd too long.");
        CommandArgs args;
        std::stringstream ss(cmd);
        std::string name, key, value;
        ss >> name;
        if (Container().find(name) == Container().end())
        {
            LOGWP("illegal command: %s", cmd.c_str());
            return NULL;
        }
        while (ss >> key)
        {
            if (ss >> value)
            {
                ASSERT_RETURN(args.find(key) == args.end(),NULL);
                args[key] = value;
            }
            else
            {
                ASSERT_RETURN(!value.empty(),NULL);
                args[key] = "";
            }
        }
        return Container()[name]->NewCommand(cmd, args);
    }

protected:
    static CommandContainer &Container()
    {
        static CommandContainer commands;
        return commands;
    }
    virtual std::shared_ptr<Command> NewCommand(const std::string &cmd, CommandArgs args) = 0;
};
template <class DerivedType>
class CommandRegister : public CommandFactory
{
public:
    CommandRegister(const std::string &name) : CommandRegister(name, false) {}
    CommandRegister(const std::string &name, bool is_private) : name_(name), is_private_(is_private)
    {
        ASSERT(Container().find(name) == Container().end());
        LOGVP("register command: %s", name.c_str());
        Container()[name] = this;
    }
    std::shared_ptr<Command> NewCommand(const std::string &cmd, CommandArgs args) override
    {
        auto command = std::make_shared<DerivedType>(cmd);
        command->is_private = is_private_;
        if (command->ResolveArgs(args))
        {
            LOGDP("create new command: %s (%s)", command->GetCmd().c_str(),command->ToString().c_str());
            return command;
        }
        return NULL;
    }

private:
    const std::string name_;
    bool is_private_;
};

#pragma endregion

#pragma region NetStat

/**
 * @brief Network State
 * 
 */
struct NetStat
{
    /**
     * @brief Network delay in millseconds
     * 
     */
    int delay;
    int max_delay;
    int min_delay;

    /**
     * @brief Jitter in millseconds
     * 
     */
    int jitter;

    /**
     * @brief The standard deviation of jitter
     * 
     */
    long long jitter_std;

    /**
     * @brief Packet loss percent
     * 
     */
    double loss;

    /**
     * @brief Send/Recv packets count
     * 
     */
    long long send_packets;
    long long recv_packets;

    /**
     * @brief Recved illegal packets count
     * 
     */
    long long illegal_packets;
    long long reorder_packets;
    long long duplicate_packets;
    /**
     * @brief The packets that stay too long in the network.
     * 
     */
    long long timeout_packets;

    /**
     * @brief Send/Recv data length
     * 
     */
    long long send_bytes;
    long long recv_bytes;

    /**
     * @brief Command send/recv time in millseconds
     * 
     */
    int send_time;
    int recv_time;
    /**
     * @brief send/recv speed in Byte/s
     * 
     */
    long long send_speed;
    long long min_send_speed;
    long long max_send_speed;

    long long recv_speed;
    long long min_recv_speed;
    long long max_recv_speed;

    /**
     * @brief send/recv packets per second
     * 
     */
    long long send_pps;
    long long recv_pps;

    /*******************************************************************/
    /********** The below properties are arithmetic property. **********/
    /**
     * @brief average recv speed
     * 
     */
    long long recv_avg_speed;
    long long send_avg_speed;
    int max_send_time;
    int min_send_time;
    int max_recv_time;
    int min_recv_time;

    /**
     * @brief the peers count when the command start
     * 
     */
    int peers_count;
    /**
     * @brief the peers count without the faled peers.
     * 
     */
    int peers_failed;

    std::string ToString() const
    {
        bool istty = isatty(fileno(stdout));
        std::stringstream ss;
#define W(p)              \
    if (!istty || p != 0) \
    ss << #p " " << p << " "
        W(loss);
        W(send_speed);
        W(recv_speed);
        W(send_avg_speed);
        W(recv_avg_speed);
        W(max_send_speed);
        W(max_recv_speed);
        W(min_send_speed);
        W(min_recv_speed);
        W(send_packets);
        W(recv_packets);
        W(illegal_packets);
        W(reorder_packets);
        W(duplicate_packets);
        W(timeout_packets);
        W(send_pps);
        W(recv_pps);
        W(send_bytes);
        W(recv_bytes);
        W(send_time);
        W(recv_time);
        W(max_send_time);
        W(max_recv_time);
        W(min_send_time);
        W(min_recv_time);
        W(delay);
        W(min_delay);
        W(max_delay);
        W(jitter);
        W(jitter_std);
        W(peers_count);
        W(peers_failed);
#undef W
        return ss.str();
    }

    void FromCommandArgs(CommandArgs &args)
    {
#define RI(p) p = atoi(args[#p].c_str())
#define RLL(p) p = atoll(args[#p].c_str())
#define RF(p) p = atof(args[#p].c_str())

        RF(loss);
        RLL(send_speed);
        RLL(recv_speed);
        RLL(send_avg_speed);
        RLL(recv_avg_speed);
        RLL(max_send_speed);
        RLL(max_recv_speed);
        RLL(min_send_speed);
        RLL(min_recv_speed);
        RLL(send_packets);
        RLL(recv_packets);
        RLL(illegal_packets);
        RLL(reorder_packets);
        RLL(duplicate_packets);
        RLL(timeout_packets);
        RLL(send_pps);
        RLL(recv_pps);
        RLL(send_bytes);
        RLL(recv_bytes);
        RI(send_time);
        RI(recv_time);
        RI(max_send_time);
        RI(max_recv_time);
        RI(min_send_time);
        RI(min_recv_time);
        RI(delay);
        RI(min_delay);
        RI(max_delay);
        RI(jitter);
        RLL(jitter_std);
        RI(peers_count);
        RI(peers_failed);
#undef RI
#undef RLL
#undef RF
    }

    // TODO: refactor the code to simplify the logic of 'arithmetic property'.
    NetStat &operator+=(const NetStat &stat)
    {
#define INT(p) p = p + stat.p
#define DOU(p) INT(p)
#define MAX(p) p = std::max(p, stat.p)
#define MIN(p) p = std::min(p, stat.p)
        DOU(loss);
        INT(send_speed);
        INT(recv_speed);
        INT(send_avg_speed);
        INT(recv_avg_speed);
        MAX(max_send_speed);
        MAX(max_recv_speed);
        MIN(min_send_speed);
        MIN(min_recv_speed);
        INT(send_packets);
        INT(recv_packets);
        INT(illegal_packets);
        INT(reorder_packets);
        INT(duplicate_packets);
        INT(timeout_packets);
        INT(send_pps);
        INT(recv_pps);
        INT(send_bytes);
        INT(recv_bytes);
        INT(delay);
        MIN(min_delay);
        MAX(max_delay);
        INT(jitter);
        INT(jitter_std);
        INT(send_time);
        INT(recv_time);
        MAX(max_send_time);
        MAX(max_recv_time);
        MIN(min_send_time);
        MIN(min_recv_time);
        INT(peers_count);
        INT(peers_failed);
#undef INT
#undef DOU
#undef MAX
#undef MIN
        return *this;
    }
    NetStat &operator/=(int num)
    {
#define INT(p) p /= num
#define DOU(p) INT(p)
#define MAX(p) p = p * 1
#define MIN(p) p = p * 1
        DOU(loss);
        INT(send_speed);
        INT(recv_speed);
        INT(send_avg_speed);
        INT(recv_avg_speed);
        MAX(max_send_speed);
        MAX(max_recv_speed);
        MIN(min_send_speed);
        MIN(min_recv_speed);
        INT(send_packets);
        INT(recv_packets);
        INT(illegal_packets);
        INT(reorder_packets);
        INT(duplicate_packets);
        INT(timeout_packets);
        INT(send_pps);
        INT(recv_pps);
        INT(send_bytes);
        INT(recv_bytes);
        INT(delay);
        MIN(min_delay);
        MAX(max_delay);
        INT(jitter);
        INT(jitter_std);
        INT(send_time);
        INT(recv_time);
        MAX(max_send_time);
        MAX(max_recv_time);
        MIN(min_send_time);
        MIN(min_recv_time);
        INT(peers_count);
        INT(peers_failed);
#undef INT
#undef DOU
#undef MAX
#undef MIN
        return *this;
    }
};

#pragma endregion

struct CommandChannel
{
    std::shared_ptr<Command> command_;
    std::shared_ptr<Context> context_;
    std::shared_ptr<Sock> control_sock_;
    std::shared_ptr<Sock> data_sock_;
};

/**
 * @brief A command stands for a type of network test.
 * 
 */
class Command
{
public:
    // TODO: optimize command structure to simplify sub command.
    Command(std::string name, std::string cmd)
        : name(name), cmd(cmd), token('$'),
          is_private(false), is_multicast(false)
    {
    }
    void RegisterCallback(CommandCallback callback)
    {
        if (callback)
            callbacks_.push_back(callback);
    }

    void InvokeCallback(std::shared_ptr<NetStat> netstat)
    {
        for (auto &callback : callbacks_)
        {
            callback(this, netstat);
        }
    }

    virtual bool ResolveArgs(CommandArgs args) { return true; }
    virtual std::shared_ptr<CommandSender> CreateCommandSender(std::shared_ptr<CommandChannel> channel) { return NULL; }
    virtual std::shared_ptr<CommandReceiver> CreateCommandReceiver(std::shared_ptr<CommandChannel> channel) { return NULL; }

    virtual int GetWait() { return STOP_WAIT_TIME; }

    std::string GetCmd() const
    {
        return (cmd.find(" token") != std::string::npos || token == '$') ? cmd : cmd + " token " + token;
    }

    virtual std::string ToString() const 
    {
        return GetCmd();
    };

    std::string name;
    bool is_private;
    bool is_multicast;

    char token;

protected:
    void UpdateToken()
    {
        static unsigned char index = 0;
        token = VISIABLE_LATTERS[index++ % (sizeof(VISIABLE_LATTERS) - 1)];
    }

private:
    std::string cmd;
    std::vector<CommandCallback> callbacks_;

    DISALLOW_COPY_AND_ASSIGN(Command);
};

struct DataHead
{
    // time since epoch in nanoseconds
    int64_t timestamp : 64;
    // sequence number
    uint16_t sequence : 16;
    // data length
    uint16_t length : 16;
    // token used for data validation
    char token;
};

#define ECHO_DEFAULT_COUNT 5
#define ECHO_DEFAULT_INTERVAL 200*1000
#define ECHO_DEFAULT_SIZE 32
#define ECHO_DEFAULT_WAIT 500*1000
#define ECHO_DEFAULT_TIMEOUT 100 // milliseconds
#define ECHO_DEFAULT_SPEED 0 // KByte/s
#define ECHO_DEFAULT_TIME 0*1000 // milliseconds
/**
 * @brief a main command, server send to client and client should echo
 * 
 */
class EchoCommand : public Command
{
public:
    // format: ping [count <num>] [interval <num>] [size <num>]
    // example: ping count 10 interval 100
    EchoCommand(std::string cmd)
        : count_(ECHO_DEFAULT_COUNT),
          interval_(ECHO_DEFAULT_INTERVAL),
          size_(ECHO_DEFAULT_SIZE),
          wait_(ECHO_DEFAULT_WAIT),
          Command("ping", cmd)
    {
        UpdateToken();
    }
    bool ResolveArgs(CommandArgs args) override
    {
        // TODO: optimize these assign.
        count_ = args["count"].empty() ? ECHO_DEFAULT_COUNT : std::stoi(args["count"]);
        interval_ = args["interval"].empty() ? ECHO_DEFAULT_INTERVAL : std::stod(args["interval"]) * 1000;
        size_ = args["size"].empty() ? ECHO_DEFAULT_SIZE : std::stoi(args["size"]);
        wait_ = args["wait"].empty() ? ECHO_DEFAULT_WAIT : std::stoi(args["wait"])*1000;
        timeout_ = args["timeout"].empty() ? ECHO_DEFAULT_TIMEOUT : std::stoi(args["timeout"]);
        if (!args["token"].empty())
            token = args["token"].at(0);
        
        auto speed = args["speed"].empty() ? ECHO_DEFAULT_SPEED : std::stoi(args["speed"]);
        auto time = args["time"].empty() ? ECHO_DEFAULT_TIME : std::stoi(args["time"]);
        if (speed > 0 && time > 0)
        {
            if(size_ == ECHO_DEFAULT_SIZE) size_ = 1472;
            count_ = ceil((speed * 1024) * (time / 1000.0) / size_);
            interval_ = 1000000/((1.0*speed*1024)/size_);
        }
        else if(interval_ > 0 && time > 0)
        {
            count_ = time * 1000.0 / interval_;
        }
        // echo can not have zero delay
        if (interval_ <= 0)
            interval_ = ECHO_DEFAULT_INTERVAL;
        return true;
    }

    std::shared_ptr<CommandSender> CreateCommandSender(std::shared_ptr<CommandChannel> channel) override
    {
        return std::make_shared<EchoCommandSender>(channel);
    }
    std::shared_ptr<CommandReceiver> CreateCommandReceiver(std::shared_ptr<CommandChannel> channel) override
    {
        return std::make_shared<EchoCommandReceiver>(channel);
    }

    std::string ToString() const override
    {
        std::stringstream out;
        out << name << " count " << count_ << " interval " << interval_/1000.0 << " size " << size_ << " wait " << wait_/1000.0 << " timeout " << timeout_;
        return out.str();
    }

    int GetCount() { return count_; }
    int GetInterval() { return interval_; }
    int GetSize() { return size_; }
    int GetWait() override { return wait_; }
    int GetTimeout() { return timeout_; }

private:
    int count_;
    int interval_;
    int size_;
    int wait_;
    int timeout_;

    DISALLOW_COPY_AND_ASSIGN(EchoCommand);
};

#define SEND_DEFAULT_COUNT 100
#define SEND_DEFAULT_INTERVAL 0*1000 // microseconds
#define SEND_DEFAULT_SIZE 1472
#define SEND_DEFAULT_WAIT 500*1000 // microseconds
#define SEND_DEFAULT_TIMEOUT 100 // milliseconds
#define SEND_DEFAULT_SPEED 0 // KByte/s
#define SEND_DEFAULT_TIME 3000 // milliseconds
/**
 * @brief a main command, server send data only and client recv only.
 * 
 */
class SendCommand : public Command
{
public:
    SendCommand(std::string cmd)
        : count_(SEND_DEFAULT_COUNT),
          interval_(SEND_DEFAULT_INTERVAL),
          size_(SEND_DEFAULT_SIZE),
          wait_(SEND_DEFAULT_WAIT),
          timeout_(SEND_DEFAULT_TIMEOUT),
          is_finished(false), Command("send", cmd)
    {
        UpdateToken();
    }

    bool ResolveArgs(CommandArgs args) override
    {
        // TODO: optimize these assign.
        count_ = args["count"].empty() ? SEND_DEFAULT_COUNT : std::stoi(args["count"]);
        interval_ = args["interval"].empty() ? SEND_DEFAULT_INTERVAL : std::stod(args["interval"]) * 1000;
        size_ = args["size"].empty() ? SEND_DEFAULT_SIZE : std::stoi(args["size"]);
        wait_ = args["wait"].empty() ? SEND_DEFAULT_WAIT : std::stoi(args["wait"]) * 1000;
        timeout_ = args["timeout"].empty() ? SEND_DEFAULT_TIMEOUT : std::stoi(args["timeout"]);
        if (!args["token"].empty())
            token = args["token"].at(0);
        is_multicast = !args["multicast"].empty();
        if (is_multicast)
            LOGDP("enable multicast.");
        ASSERT(size_>=sizeof(DataHead));
        
        auto speed = args["speed"].empty() ? SEND_DEFAULT_SPEED : std::stoi(args["speed"]);
        auto time = args["time"].empty() ? SEND_DEFAULT_TIME : std::stoi(args["time"]);
        if (speed > 0 && time > 0)
        {
            count_ = ceil((speed * 1024) * (time / 1000.0) / size_);
            interval_ = 1000000/((1.0*speed*1024)/size_);
        }
        else if(interval_ > 0 && time > 0)
        {
            count_ = time * 1000.0 / interval_;
        }
        return true;
    }

    std::shared_ptr<CommandSender> CreateCommandSender(std::shared_ptr<CommandChannel> channel) override
    {
        return std::make_shared<SendCommandSender>(channel);
    }
    std::shared_ptr<CommandReceiver> CreateCommandReceiver(std::shared_ptr<CommandChannel> channel) override
    {
        return std::make_shared<SendCommandReceiver>(channel);
    }

    std::string ToString() const override
    {
        std::stringstream out;
        out << name << " count " << count_ << " interval " << interval_/1000.0 << " size " << size_ << " wait " << wait_/1000.0 << " timeout " << timeout_;
        return out.str();
    }

    int GetCount() { return count_; }
    /**
     * @brief Get the Interval object in microseconds
     * 
     * @return int 
     */
    int GetInterval() { return interval_; }
    int GetSize() { return size_; }
    /**
     * @brief Get the Wait object in microseconds
     * 
     * @return int 
     */
    int GetWait() override { return wait_; }
    /**
     * @brief Get the Timeout object in milliseconds
     * 
     * @return int 
     */
    int GetTimeout() { return timeout_; }

    bool is_finished;

private:
    int count_;
    int interval_;
    int size_;
    int wait_;
    int timeout_;

    DISALLOW_COPY_AND_ASSIGN(SendCommand);
};

// #define DEFINE_COMMAND(name,typename) \
// class typename : public Command \
// {\
// public:\
//     typename(std::string cmd):Command(#name,cmd){}\
// }

/**
 * @brief every client should response a ack command to a main command
 * 
 */
class AckCommand : public Command
{
public:
    AckCommand() : AckCommand("ack") {}
    AckCommand(std::string cmd) : Command("ack", cmd) {}

    DISALLOW_COPY_AND_ASSIGN(AckCommand);
};

/**
 * @brief notify client the command has finished.
 * 
 */
class StopCommand : public Command
{
public:
    StopCommand() : StopCommand("stop") {}
    StopCommand(std::string cmd) : Command("stop", cmd) {}

    DISALLOW_COPY_AND_ASSIGN(StopCommand);
};

/**
 * @brief send the test result to server
 * 
 */
class ResultCommand : public Command
{
public:
    ResultCommand() : ResultCommand("result") {}
    ResultCommand(std::string cmd) : Command("result", cmd) {}
    bool ResolveArgs(CommandArgs args) override
    {
        netstat = std::make_shared<NetStat>();
        netstat->FromCommandArgs(args);
        return true;
    }
    std::string Serialize(const NetStat &netstat)
    {
        return name + " " + netstat.ToString();
    }

    std::shared_ptr<NetStat> netstat;

    DISALLOW_COPY_AND_ASSIGN(ResultCommand);
};

class ModeCommand : public Command
{
public:
    enum class ModeType
    {
        None,
        UDP,
        Multicast //TODO: TCP
    };

    ModeCommand() : ModeCommand("mode") {}
    ModeCommand(std::string cmd) : mode_(ModeType::None), Command("mode", cmd) {}
    bool ResolveArgs(CommandArgs args) override
    {
        mode_ = !args["udp"].empty() ? ModeType::UDP : !args["multicast"].empty() ? ModeType::Multicast : ModeType::None;
        return mode_ != ModeType::None;
    }
    ModeType GetModeType()
    {
        return mode_;
    }

private:
    ModeType mode_;
};