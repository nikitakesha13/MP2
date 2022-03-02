#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <string>
#include <dirent.h>
#include <thread>

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

using namespace std;

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    string username = request->username();
    string list_users = "";
    DIR *dr;
    struct dirent *en;
    dr = opendir(".");
    if (dr){
      while((en = readdir(dr)) != NULL){
        if (((string)(en->d_name)).find(".txt") != string::npos 
            && ((string)(en->d_name)).find("_timeline") == string::npos){
          string temp = en->d_name;
          int pos = temp.find(".txt");
          temp.erase(pos, pos+4);
          list_users += temp;
          list_users += ", ";
        }
      }
      closedir(dr);
    }

    string followers;
    ifstream ifs(username + ".txt");
    int i = 0;
    while(getline(ifs, followers)){
      if (i == 1){
        followers.erase(0, 11);
        break;
      }
      ++i;
    }
    ifs.close();


    reply->add_all_users(list_users);
    reply->add_following_users(followers);

    reply->set_msg("SUCCESS");
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to follow one of the existing
    // users
    // ------------------------------------------------------------
    string username = request->username();
    string args;
    for (auto x: request->arguments()){
      args += x;
    }
    if (username == args){
      reply->set_msg("FAILURE_INVALID_USERNAME");
      return Status::OK;
    }

    ifstream ifs(args + ".txt");
    if (!ifs.is_open()){
      reply->set_msg("FAILURE_NOT_EXISTS");
      return Status::OK; 
    }

    string args_username;
    getline(ifs, args_username);

    string args_followers;
    getline(ifs, args_followers);
    bool exists = false;
    int pos = args_followers.find(username);
    if (pos == string::npos){
      args_followers += username + ", ";
      exists = true;
    }

    string args_following;
    getline(ifs, args_following);

    ifs.close();

    ofstream ofs(args + ".txt");
    ofs << args_username << "\n" << args_followers << "\n" << args_following << "\n";

    ofs.close();

    if (exists) {
      reply->set_msg("SUCCESS");
    }
    else {
      reply->set_msg("FAILURE_INVALID_USERNAME");
    }
    
    return Status::OK;
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------
    string username = request->username();
    string args;
    for (auto x: request->arguments()){
      args += x;
    }
    if (username == args){
      reply->set_msg("FAILURE_INVALID_USERNAME");
      return Status::OK;
    }

    ifstream ifs(args + ".txt");
    if (!ifs.is_open()){
      reply->set_msg("FAILURE_NOT_EXISTS");
      return Status::OK; 
    }

    string args_username;
    getline(ifs, args_username);

    string args_followers;
    getline(ifs, args_followers);
    bool exists = false;
    int pos = args_followers.find(username);
    if (pos != string::npos){
      args_followers.erase(pos, args.length()+2);
      exists = true;
    }

    string args_following;
    getline(ifs, args_following);

    ofstream ofs(args + ".txt");
    ofs << args_username << "\n" << args_followers << "\n" << args_following << "\n";

    ofs.close();

    if (exists) {
      reply->set_msg("SUCCESS");
    }
    else {
      reply->set_msg("FAILURE_INVALID_USERNAME");
    }

    return Status::OK;
  }
  
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    string username = request->username();
    bool exists = false;

    DIR *dr;
    struct dirent *en;
    dr = opendir(".");
    if (dr){
      while((en = readdir(dr)) != NULL){
        if (((string)(en->d_name)) == (username + ".txt")){
          exists = true;
        }
      }
      closedir(dr);
    }

    if (!exists){
      ofstream ofs;
      ofs.open(username + ".txt");
      ofs << "Username: " << username << "\n";
      ofs << "Followers: " << username << ", \n";
      ofs << "Following: \n"; 
      ofs.close();
    }

    reply->set_msg("SUCCESS");
    return Status::OK;
  }

  Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
    // ifstream ifs;
    Message msg;
    stream->Read(&msg);
    string username = msg.username();
    int past_file_size = 0;

    thread recv_msg_thread([stream]{
      Message recv_msg;
      ifstream ifs;
      string username;
      while(stream->Read(&recv_msg)){

        time_t time_now = time(0);
        string current_time = ctime(&time_now);

        username = recv_msg.username();
        ifs.open(username + ".txt");
        string followers;
        int i = 0;
        while(getline(ifs, followers)){
          if (i == 1){
            followers.erase(0, 11 + username.size() + 2);
            break;
          }
          ++i;
        }
        ifs.close();

        if (followers.size() > 0){
          vector<string> list_followers;
          size_t pos = 0;

          while((pos = followers.find(',')) != string::npos){
            list_followers.push_back(followers.substr(0, pos));
            followers.erase(0, pos + 2); // need to delete comma and a space
          }

          for (auto x: list_followers){
            vector<string> file_msg;
            ifs.open(x + "_timeline.txt");
            if (ifs.is_open()){
              string line_msg;
              while(getline(ifs, line_msg)){
                file_msg.push_back(line_msg);
              }
            }
            ifs.close();

            string clean_msg = recv_msg.msg();
            size_t pos = clean_msg.find('\n');
            if (pos != string::npos){
              clean_msg.erase(clean_msg.begin()+pos);
            }
            
            pos = current_time.find('\n');
            if (pos != string::npos){
              current_time.erase(current_time.begin()+pos);
            }

            ofstream ofs(x + "_timeline.txt");
            ofs << username << " *break* " << clean_msg << " *break* " << current_time;
            for (auto k: file_msg){
              ofs << '\n' << k;
            }
            
            ofs.close();
          }
        }
      }
    });

    thread send_msg_thread([stream](string username, int past_file_size) {
      Message send_msg;
      int new_file_size;
      string path = username + "_timeline.txt";
      ifstream ifs;
      int counter = 0;
      bool first_run = true;
      vector<vector<string>> list_messages;

      while(1){
        ifs.open(path);
        if (!ifs.is_open()){
          continue;
        }

        ifs.seekg(0, ios::end);
        new_file_size = ifs.tellg();
        if (new_file_size > past_file_size){
          ifs.clear();
          ifs.seekg (0, ios::beg);

          string timeline_msg;
          while (getline(ifs, timeline_msg)){
            timeline_msg += " *break* ";
            int pos;
            vector<string> user_msg;
            while((pos = timeline_msg.find(" *break* ")) != string::npos){
              user_msg.push_back(timeline_msg.substr(0, pos));
              timeline_msg.erase(0, pos+9);
            }
            list_messages.push_back(user_msg);
            
            ++counter;
            if (counter == 20){
              break;
            }
          }

          int start;
          int finish;

          if (first_run){
            start = 0;
            finish = list_messages.size();
          }
          else{
            start = list_messages.size()-1;
            finish = list_messages.size();
          }

          for (int i = start; i < finish; ++i){

            send_msg.set_username(list_messages[i][0]);
            send_msg.set_msg(list_messages[i][1]);
            struct tm tm;
            strptime((list_messages[i][2]).c_str(), "%A %B %d %H:%M:%S %Y", &tm);
            time_t get_time = mktime(&tm);

            Timestamp time_write_stamp;
            time_write_stamp = TimeUtil::TimeTToTimestamp(get_time);
            send_msg.set_allocated_timestamp(&time_write_stamp);

            stream->Write(send_msg);
            send_msg.release_timestamp();
          }

          counter = 19;
          past_file_size = new_file_size;
          first_run = false;
        }
        ifs.close();
      }
    }, username, past_file_size);

    send_msg_thread.join();
    recv_msg_thread.join();

    return Status::OK;

  }

};

void RunServer(std::string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
  std::string server_address("localhost:" + port_no); 
  SNSServiceImpl service;
  
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  
  std::string port = "3010";
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }
  RunServer(port);
  return 0;
}
