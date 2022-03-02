#include <iostream>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include <algorithm>
#include "client.h"
#include <pthread.h>
#include <thread>
#include <time.h>
#include <google/protobuf/util/time_util.h>

#include "sns.grpc.pb.h"

using namespace std;

using grpc::CreateChannel;
using grpc::InsecureChannelCredentials;
using grpc::ClientContext;
using grpc::Channel;
using grpc::Status;
using grpc::ClientReaderWriter;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;
using csce438::SNSService;
using csce438::Request;
using csce438::Reply;
using csce438::Message;

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), port(p)
            {}

    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
        void send_handle_loop();
        void recv_handle_loop();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
        std::unique_ptr<SNSService::Stub> stub_;
        unique_ptr<ClientReaderWriter<Message, Message>> stream_;
};

int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
            case 'h':
                hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to create a stub so that
    // you call service methods in the processCommand/porcessTimeline
    // functions. That is, the stub should be accessible when you want
    // to call any service methods in those functions.
    // I recommend you to have the stub as
    // a member variable in your own Client class.
    // Please refer to gRpc tutorial how to create a stub.
	// ------------------------------------------------------------
    auto channel = CreateChannel(hostname + ":" + port, InsecureChannelCredentials());

    stub_ = SNSService::NewStub(channel);

    Request request;
    request.set_username(username);
    ClientContext context;
    Reply reply;

    Status status = stub_->Login(&context, request, &reply);

    if (status.ok()){
        return 1;
    }

    return -1; // return 1 if success, otherwise return -1
}

IReply Client::processCommand(std::string& input)
{
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse the given input
    // command and create your own message so that you call an 
    // appropriate service method. The input command will be one
    // of the followings:
	//
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
    // TIMELINE
	//
	// ------------------------------------------------------------
	
    // ------------------------------------------------------------
	// GUIDE 2:
	// Then, you should create a variable of IReply structure
	// provided by the client.h and initialize it according to
	// the result. Finally you can finish this function by returning
    // the IReply.
	// ------------------------------------------------------------
    
	// ------------------------------------------------------------
    // HINT: How to set the IReply?
    // Suppose you have "Follow" service method for FOLLOW command,
    // IReply can be set as follow:
    // 
    //     // some codes for creating/initializing parameters for
    //     // service method
    //     IReply ire;
    //     grpc::Status status = stub_->Follow(&context, /* some parameters */);
    //     ire.grpc_status = status;
    //     if (status.ok()) {
    //         ire.comm_status = SUCCESS;
    //     } else {
    //         ire.comm_status = FAILURE_NOT_EXISTS;
    //     }
    //      
    //      return ire;
    // 
    // IMPORTANT: 
    // For the command "LIST", you should set both "all_users" and 
    // "following_users" member variable of IReply.
    // ------------------------------------------------------------

    IReply ire;
    Status status;

    ClientContext context;
    Request request;
    Reply reply;

    request.set_username(username);

    string command;
    string arguments;

    int pos = input.find(' ');

    if (pos != string::npos){
        command = input.substr(0, pos);
        arguments = input.erase(0, pos + 1);
        arguments.erase(remove(arguments.begin(), arguments.end(), ' '), arguments.end());
        request.add_arguments(arguments);
    }
    else {
        command = input;
    }

    if (command == "LIST"){
        status = stub_->List(&context, request, &reply);
        for (auto x: reply.all_users()){
            ire.all_users.push_back(x);
        }
        for (auto x: reply.following_users()){
            ire.following_users.push_back(x);
        }
    }
        
    
    else if (command == "FOLLOW"){
        status = stub_->Follow(&context, request, &reply);
    }

    else if (command == "UNFOLLOW"){
        status = stub_->UnFollow(&context, request, &reply);
    }

    else if (command == "TIMELINE"){
        ire.grpc_status = Status::OK;
        ire.comm_status = SUCCESS;
        return ire;
    }

    ire.grpc_status = status;
    if (status.ok()) {
        if (reply.msg() == "SUCCESS"){
            ire.comm_status = SUCCESS;
        }
        else if (reply.msg() == "FAILURE_ALREADY_EXISTS"){
            ire.comm_status = FAILURE_ALREADY_EXISTS;
        }
		else if (reply.msg() == "FAILURE_NOT_EXISTS"){
            ire.comm_status = FAILURE_NOT_EXISTS;
        }
		else if (reply.msg() == "FAILURE_INVALID_USERNAME"){
            ire.comm_status = FAILURE_INVALID_USERNAME;
        }
		else if (reply.msg() == "FAILURE_INVALID"){
            ire.comm_status = FAILURE_INVALID;
        }
		else if (reply.msg() == "FAILURE_UNKNOWN"){
            ire.comm_status = FAILURE_UNKNOWN;
        }
    } 
    else {
        ire.comm_status = FAILURE_UNKNOWN;
    }
    
    return ire;
}

void Client::send_handle_loop(){
    Message send_msg;

    while(1){
        string msg_str = getPostMessage();
        send_msg.set_username(username);
        send_msg.set_msg(msg_str);

        stream_->Write(send_msg);
    }
}

void Client::recv_handle_loop(){
    Message recv_msg;
    while(1){
        if (stream_->Read(&recv_msg) > 0){
            string sender = recv_msg.username();
            string msg = recv_msg.msg();
            time_t time = TimeUtil::TimestampToTimeT(recv_msg.timestamp());
            displayPostMessage(sender, msg, time);
        }
        
    }
}

void Client::processTimeline()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions
    // for both getting and displaying messages in timeline mode.
    // You should use them as you did in hw1.
	// ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
	// ------------------------------------------------------------

    ClientContext context;
    Message msg;
    msg.set_username(username);
    stream_ = stub_->Timeline(&context);
    stream_->Write(msg);

    thread send_msg(&Client::send_handle_loop, this);
    thread recv_msg(&Client::recv_handle_loop, this);

    send_msg.join();
    recv_msg.join();
}
