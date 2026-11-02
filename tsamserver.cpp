#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <list>
#include <poll.h>
#include "log_event.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <fstream>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include "identity.h"

#define BACKLOG  5
//structure that stores information about/for client server 
struct connected{
    std::string group_id;
    int socket;
    std::string server_ip;
    int server_port;
    std::string type;
    std::string receive_buffer;
    std::string send_buffer;

    time_t last_keepalive = 0;
    time_t last_statusreq = 0;
    bool helo_sent = false;
};

//structure that keeps track of the clients inbox
struct message {
    std::vector<std::string> msg_from;
    std::vector<std::string> message_box;
    std::vector<std::string> time;
};

//response from servers
//peer advertised by another server
struct server_list{
    std::string group;
    std::string ip;
    std::string port;
};

int listening_port;
std::string listening_ip;
//keeping track of different information for both connected servers and client
//ef server listi með 8 þá sleppa
std::map<std::string, message> msgbox; //the key is the clients udername

std::map<std::string, message> stored_message; //storing message for a group if connection is not found 
std::map<int, connected> connections; //the key is the server socket number
std::map <std::string, std::vector<struct server_list>> server_net; //map where it connects a group to it's connected servers
std::map <int, bool> handshake; //keeping track of after accept to be able to complete protocol with server
bool client_connected; //keeping track to be able to complete protocol with servers


// "Human-readable time" log
std::string timestamp(){
    return std::to_string(time(nullptr));
}

//Create and bind a reusable listening socket
int open_socket(int port){
    struct sockaddr_in sk_addr;   // address settings for bind()
    int sock;                     // socket opened for this port
    int set = 1;                  //toggle for setsockopt

    //creating a TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    //error handling
    if (sock < 0){
        perror("error opening socket");
        return(-1);
    }

    //allows the socket to be reused after the server is closed 
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0){
        perror("Failed to set SO_REUSEADDR:");
        close(sock);
        return(-1);
    }
    
    memset(&sk_addr, 0, sizeof(sk_addr));

    sk_addr.sin_family      = AF_INET;
    sk_addr.sin_addr.s_addr = INADDR_ANY;
    sk_addr.sin_port        = htons(port);

    
    //binding the socket and listen for connection from the client
    if (bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0){
        perror("Failed to bind to socket");
        close(sock);
        return(-1);
    }
    return sock;
}


std::vector<char> create_msg(const std::string& message){
    // Frame <SOH><length><STX><command><ETX>
    char SOH = 0x01; 
    char STX = 0x02;
    char ETX = 0x03;

    size_t mes_len = message.length();
    uint16_t total_size = htons(mes_len + 5);

    //creating a buffer that holds the message and 5 framing bytes
    std::vector<char> buffer(mes_len + 5);
    int offset = 0;

    memcpy(buffer.data() + offset, &SOH, sizeof(SOH));
    offset += sizeof(SOH);

    memcpy(buffer.data() + offset, &total_size, sizeof(total_size));
    offset += sizeof(total_size);

    memcpy(buffer.data() + offset, &STX, sizeof(STX));
    offset += sizeof(STX);

    memcpy(buffer.data() + offset, message.data(), message.size());
    offset += message.size();

    memcpy(buffer.data() + offset, &ETX, sizeof(ETX));
    offset += sizeof(ETX);

    return buffer;
}

//Periodically queue KEEPALIVE for connected servers
void check_keepalive(){
    time_t now =  time(nullptr);

    for (auto& [sock, conn] : connections){

        if (conn.type == "CLIENT" || conn.group_id.empty()){
            continue;
        }

        //Sends every 60 seconds 
        if (now - conn.last_keepalive >= 60){
            int msg_count = stored_message[conn.group_id].message_box.size();
            std::string msg = "KEEPALIVE," + std::to_string(msg_count);
            auto buffer = create_msg(msg);
            conn.send_buffer.append(buffer.data(), buffer.size());
            log_event_bytes("SEND", conn.server_ip.c_str(), conn.server_port, conn.group_id.c_str(), msg.data(), msg.size());
            conn.last_keepalive = now; 
        }
    }
}

//Response to the connected client GETMSG
void send_get_msg(int sock){

     //construction the message that gets sent for GETMSQ
    std::string get_msg;
    std::string username = connections[sock].group_id;

    std::vector<std::string> message = msgbox[username].message_box;
    std::vector<std::string> from = msgbox[username].msg_from;
    std::vector<std::string> timest = msgbox[username].time;


    //if the inbox has a message stored for the client then get the front message 
    //and who sent that corresponding message
    if (!msgbox[username].message_box.empty()){
        std::string msg_send = message.front();
        std::string from_user = from.front();
        std::string timestamp = timest.front();
        
        get_msg = "From: " + from_user + "\n" + "Message: " + msg_send;
    
        //erasing the message from the clients inbox
        message.erase(message.begin()); 
        from.erase(from.begin());
        timest.erase(timest.begin());

        //now it stores the messagebox with the message deleted
        msgbox[username].time = timest;
        msgbox[username].message_box = message;
        msgbox[username].msg_from = from;

       
    }
    else{
        //message for a client that has no messages 
        get_msg = ("No new messages");
    }
    //creating the buffer for the response and putting it in the client buffer
    auto buffer = create_msg(get_msg);
    connections[sock].send_buffer.append(buffer.data(),buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), get_msg.data(), get_msg.size());

   
    
}

//Periodically sends statusreq to connected servers
void send_statusreq(){
    time_t now =  time(nullptr);

    for (auto& [sock, conn] : connections){
        if (conn.type == "CLIENT" || conn.group_id.empty()){
            continue;
        }

        if (now - conn.last_statusreq >= 60){
            std::string status = "STATUSREQ";
            auto buffer = create_msg(status);
            conn.send_buffer.append(buffer.data(), buffer.size());
            log_event_bytes("SEND", conn.server_ip.c_str(), conn.server_port, conn.group_id.c_str(), status.data(), status.size());
            conn.last_statusreq = now; 
        }
    }
    
}

//Constructing STATUSRESP which is counts of messages held for other groups
void send_statusresp(int sock){
    std::string status_req = "STATUSRESP"; //construction the message that gets sent for STATUSRESP

    //iterating over each clients inbox
    for (const auto& [name, msg] : stored_message){
        int count = msg.message_box.size();  //getting the number of messages the group has stored

        //only add the clients who has a message stored to the response string
        if (count > 0){
            if (!status_req.empty()) { //so , does not get added to the front
                    status_req += ",";
            }
            status_req += name + ":" + std::to_string(count);

        }      
    }

    if (status_req == "STATUSRESP"){
        status_req += ",EMPTY";
    }
    
    //creating the buffer for send
    auto buffer = create_msg(status_req);
    //appending the buffer to the client struct
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), status_req.data(), status_req.size());
        
}
/*
void send_confirmation(int sock, std::string message){
    //sending confirmation to client
    auto buffer = create_msg(message);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());

}
*/

//sending the error message 
void send_error(int sock, std::string message){
    std::string error_msg = "ERROR," + message;
    auto buffer = create_msg(error_msg);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), error_msg.data(), error_msg.size());
}

//Response to HELO list of our connected servers
void send_servers(int sock){
    std::string connected_servers = "SERVERS," + std::string(GROUP_ID) + "," + listening_ip + "," + std::to_string(listening_port) + ";";
    for (const auto& [key, servers] : connections){
        if (servers.type == "CLIENT" || servers.group_id.empty()){
            continue;
        }
        connected_servers += servers.group_id + "," + servers.server_ip + "," + std::to_string(servers.server_port) + ";";
    }

    auto buffer = create_msg(connected_servers);
    //appending the buffer to the connection struct
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), connected_servers.data(), connected_servers.size());
}

//Constructing HELO which is first command to another server when connecting/accepting
void send_helo(int sock){
    if (connections[sock].helo_sent){
        return;
    }
    std::string group_name = GROUP_ID;
    std::string helo_msg = "HELO," + group_name;
    
    auto buffer = create_msg(helo_msg);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, group_name.c_str(), helo_msg.data(), helo_msg.size());
    connections[sock].helo_sent = true;
}

//Constructing SENDMSG
void to_send(int sock, std::string msg, std::string send_to){
    //first checking if we are directly connected to the group
    std::string forward = "";
    bool send_found = false;
    for (const auto& [key, servers] : connections){
        if (servers.group_id == send_to){
            auto buffer = create_msg(msg);
            connections[key].send_buffer.append(buffer.data(), buffer.size());
            log_event_bytes("SEND", servers.server_ip.c_str(), servers.server_port, send_to.c_str(), msg.data(), msg.size());
            send_found = true;
            return;
        }
    }

    //no connection to group found so storing it's message
    if (send_found == false){
        stored_message[send_to].message_box.push_back(msg);
        log_note("couldn't find route to group %s, storing its message for now", send_to.c_str());
    }
}


//Queue all messages held for a group when connection is found
void forward_stored_msg(int sock, std::string group_name){
    std::vector<std::string> message = stored_message[group_name].message_box;
    while (!message.empty()){
        std::string msg_send = message.front();

        auto buffer = create_msg(msg_send);
        connections[sock].send_buffer.append(buffer.data(), buffer.size());
        log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), msg_send.data(), msg_send.size());

        //erasing the message from the clients inbox
        message.erase(message.begin()); 
        
    }
  
    //now it stores the messagebox with the message deleted
    stored_message[group_name].message_box = message;
}

//Request messages for a group our server is connected to
void send_getmsg(int sock, std::string group_name){
    std::string get_msg = "GETMSGS," + group_name;
    auto buffer = create_msg(get_msg);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    log_event_bytes("SEND", connections[sock].server_ip.c_str(), connections[sock].server_port, group_name.c_str(), get_msg.data(), get_msg.size());
}

//Process commands after the first command
void proc_command(int sock, std::vector<std::string> tokens){
    std::string send_to;
    std::string from;
    std::string msg_send;
    std::string error_msg;
    std::string main_connection;
    std::vector<std::string> servers_from;

    if (tokens.empty()){ //no command
        return;
    }

    if (tokens[0] == "ERROR"){
        return;
    }

    if (tokens[0] == "SERVERS"){
        if (connections[sock].type == "CLIENT"){
            send_servers(sock);
        }

        else{
        main_connection = connections[sock].group_id;
        }
        
        //extracting information to store in a structure
        tokens.erase(tokens.begin());
        while (tokens.size() >= 3){
            std::string group_id = tokens[0];
            std::string group_ip = tokens[1];
            std::string group_port = tokens[2];

            tokens.erase(tokens.begin());
            tokens.erase(tokens.begin());
            tokens.erase(tokens.begin());
            
            if (group_id == main_connection){
                continue;
            }
            server_net[main_connection].push_back({group_id, group_ip, group_port});
        }
    }
  
    else if (tokens[0] == "SENDMSG"){  
        if (tokens.size() < 4){
            error_msg = "invalid command for SENDMSG";
            send_error(sock, error_msg);
            return;
        }
        from = tokens[1];
        send_to = tokens[2];

        if (connections[sock].type == "CLIENT"){
            //connected user does not match the from user in the command
            if (connections[sock].group_id != from){
                error_msg = "sender does not match connected user";
                send_error(sock, error_msg);
                return;
            }

            msg_send = tokens[3];
            for (size_t i = 4; i < tokens.size(); i++){
                    msg_send += "," + tokens[i]; //if message included , that got split. Putting it back together
            }

            std::string curr_time = timestamp(); //adding the timestamp if SENDMSG came from a client
            std::string message_send = "SENDMSG," + from + "," + send_to + "," + curr_time + "," + msg_send;
            to_send(sock, message_send, send_to);
            return;
        }        
       
    
        else{
            if (tokens.size() < 5){
                error_msg = "invalid command for SENDMSG";
                send_error(sock, error_msg);
                return;
            }
            std::string timestamp = tokens[3];
            msg_send = tokens[4];

            for (size_t i = 5; i < tokens.size(); i++){
                msg_send += "," + tokens[i]; //if message included , that got split. Putting it back together
            }

            if (send_to == GROUP_ID){
                msgbox[send_to].message_box.push_back(msg_send);
                msgbox[send_to].msg_from.push_back(from);
                msgbox[send_to].time.push_back(timestamp);

            }
            else{
                std::string message_send = "SENDMSG," + from + "," + send_to + "," + timestamp + "," + msg_send; 
                to_send(sock, message_send, send_to);
            }
        }
        
    }
    else if (tokens[0] == "STATUSRESP"){
        for (size_t i = 1; i + 1 < tokens.size(); i+= 2){
            std::string group_dest = tokens[i];

            if (group_dest == "EMPTY"){
                return;
            }

            //if our group is mentioned in STATUSRESP from a group fetch our message
            if (group_dest == GROUP_ID){
                send_getmsg(sock, group_dest);
                continue;
            }

            //if our group is directly connected to a group mentioned in STATUSRESP we fetch it
            for (const auto& [key, servers] : connections){
                if (servers.group_id == group_dest && servers.type == "SERVER" && key != sock){
                    send_getmsg(sock, servers.group_id); 
                    break;
                }
            
            }
        }
        

    }

    else if (tokens[0] == "HELO"){
        if (connections[sock].type == "SERVER"){
            send_servers(sock);
        }
    }

    
    else if (tokens[0] == "KEEPALIVE"){
        if (tokens.size() < 2){
	    error_msg = "invalid KEEPALIVE";
            send_error(sock, error_msg);
            return;
	    }
        if (std::stoi(tokens[1]) > 0 && tokens.size() >= 2){
            send_getmsg(sock, GROUP_ID);
        }
    }
    
    //GETMSG from client and GETMSGS from servers
    else if(tokens[0] == "GETMSG" || tokens[0] == "GETMSGS"){
        if (tokens.size() < 2){
            error_msg = "invalid command for GETMSGS";
                send_error(sock, error_msg);
                return;
        }
        from = tokens[1];

        if (connections[sock].type == "CLIENT"){
             //names do not match
            if (tokens[0] != "GETMSG"){
                error_msg = "client must use GETMSG ";
                send_error(sock, error_msg);
                return;
            }

            if (connections[sock].group_id != from){
                error_msg = "requested user does not match connected user";
                send_error(sock, error_msg);
                return;
            }  

            send_get_msg(sock);
        }
        else{
            if (tokens[0] != "GETMSGS"){
                error_msg = "servers must use GETMSGS";
                send_error(sock, error_msg);
                return;
            }
            forward_stored_msg(sock, from);
        }
         
       
    }

    else if(tokens[0] == "STATUSREQ"){
        send_statusresp(sock);
    }

    //unknown command 
    else{
        error_msg = "Unknown command"; 
        send_error(sock, error_msg);
        log_event("DROP", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), "Unknown command");
        return;
    }
    
}

//New connection protocol for a server or client
void first_command(int sock, std::vector<std::string> tokens){
    std::string error_msg;

    if (tokens.empty()){ //no command
        return;
    }

    //Helo protocol with servers
    if (tokens[0] == "HELO"){
        connections[sock].group_id = tokens[1];
        connections[sock].type = "SERVER";
        connections[sock].last_keepalive = time(nullptr); // First KEEPALIVE in 60s
        handshake[sock] = true;
        send_helo(sock);
        send_servers(sock);
        return;
    }

    //Clients send CONNECT,group_Name
    if (tokens[0] == "CONNECT"){
        if (client_connected == true){
            error_msg = "Client already connected";
            send_error(sock, error_msg);
            return;
        }

        if (tokens.size() < 2){
            error_msg = "invalid command for CONNECT";
            send_error(sock, error_msg);
            return;
        }

        std::string username = tokens[1];

        if (username != GROUP_ID){
            error_msg = "invalid client";
            send_error(sock, error_msg);
            return;
        }

        connections[sock].type = "CLIENT";
        connections[sock].group_id = username;
        handshake[sock] = true;
        client_connected = true;
        std::cout << GROUP_ID <<" connected to socket: " << sock << std::endl;
        return;
    }

    else{
        error_msg = "Unknown command";
        send_error(sock, error_msg);
        return;
    }
}

//Parsing complete frames in the sockets receive buffer
void client_command(int sock){
    //refrencing a connected buffer to read its data
    std::string &data = connections[sock].receive_buffer;
    std::string command_msg;
    std::vector<std::string> tokens;

    std::string ip_addr = connections[sock].server_ip;
    int port = connections[sock].server_port;
    

    while(true){
        tokens.clear();
        if (data.size() < 4){
            return; //can't read frame for length 
        }


        if (data[0] != 0x01 || data[3] != 0x02){
            return;
        }

        uint16_t frame_length; //to get the frame length, length is 2 bytes

        memcpy(&frame_length, data.data() + 1, sizeof(frame_length)); //copying the receive buffer into the frame length

        frame_length = ntohs(frame_length); //converting to host byte network 

        //the frame includes 5 bytes
        if (frame_length < 5){
            return;
        }

        if (data.size() < frame_length){ //waiting for the complete frame to start proccessing command
            return;
        }

        if (data[frame_length - 1] != 0x03){
            return;
        }

        //extracting the command starting at byte 4 and excluding the 5 framing bytes
        std::string command = data.substr(4, frame_length - 5);

        log_event_bytes("RECV", ip_addr.c_str(), port, connections[sock].group_id.c_str(), command.data(), command.size());
        //removing new line to get the right amount of bytes
        if (command.back() == '\n'){
            command.pop_back();
        }

        data.erase(0, frame_length);
        //Replacing ; with , because of SERVERS format
        if (command.substr(0, 8) == "SERVERS,"){
            std::replace(command.begin(), command.end(), ';', ',');
        }

        //Replacing : with , because of STATUSRESP format
        if (command.substr(0, 11) == "STATUSRESP,"){
            std::replace(command.begin(), command.end(), ':', ',');
        }

        std::stringstream stream(command);
        while (std::getline(stream, command_msg, ',')){ //splitting the command at , and putting it in tokens
            tokens.push_back(command_msg);
        }

        if (handshake[sock] == false){
            first_command(sock, tokens); //handling the first command from either client or server
        }
        else{
            proc_command(sock, tokens); //processing which command it is
        }
    }
}

void handle_client_data(std::vector<struct pollfd> &fds){
    char buffer[5005]; //buffer to read from client 

    for (size_t i = 1; i < fds.size(); i++){
        if(fds[i].revents & POLLIN){ //socket is readable 

            //receiving something from a client/server
            int sock = fds[i].fd;
            //setting the recv to nonblocking with MSG_DONTWAIT 
            //if there is no data available it does not wait
            ssize_t bytes_recv = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
            
            //appending to the connection buffer the bytes received
            if (bytes_recv > 0){
                connections[sock].receive_buffer.append(buffer, bytes_recv);
                client_command(sock); //processing the commands received
            }

            else if (bytes_recv == 0){ //when client/server has called close(socket)
                //closing the client socket and erasing it from the list
                if (connections[sock].type == "CLIENT"){
                    client_connected = false;
                }
                close(sock);
                log_event("CLOSE", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), "connection closed");
                server_net.erase(connections[sock].group_id);
                connections.erase(sock);
                handshake.erase(sock);
                fds.erase(fds.begin() + i);
                i--;
                continue;
            }
        }

        if(fds[i].revents & POLLOUT){ //socket is writeable

            int sock = fds[i].fd;
            std::string &data = connections[sock].send_buffer; //getting the buffer which contains the response 

            std::string ip_addr = connections[sock].server_ip;


            //sending buffered data to the client, remaining data remains for POLLOUT
            if (!data.empty()){
                //MSG_DONTWAIT for send to be nonblocking
                int sent = send(sock, data.data(), data.size(), MSG_DONTWAIT);
                
                if (sent > 0){
                    data.erase(0, sent); 
                }
                else if (sent <= 0){
                    //closing the connected socket and erasing it from the list
                    if (connections[sock].type == "CLIENT"){
                        client_connected = false;
                    }
                    log_event("CLOSE", connections[sock].server_ip.c_str(), connections[sock].server_port, connections[sock].group_id.c_str(), "connection closed");
                    (connections[sock].group_id + " Disconnected");
                    close(sock);
                    handshake.erase(sock);
                    server_net.erase(connections[sock].group_id);
                    connections.erase(sock);
                    fds.erase(fds.begin() + i);
                    i--;
                    continue;
                }
                
            }
        }

    }
}

//Connecting to another peer
int connect_to(int port, std::string ip){
    // Server address from command line
    struct sockaddr_in srcaddr;
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_port = htons(port);

  

    if (inet_pton(AF_INET, ip.c_str(), &srcaddr.sin_addr) < 0){
        perror("Invalid ip address");
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (connect(sockfd, (struct sockaddr*)&srcaddr, sizeof(srcaddr)) < 0){
        perror("error connecting");
        close(sockfd);
        return -1;
    }
    return sockfd;
}
int main(int argc, char* argv[]){
    bool finished = false; //For the while loop
    int listen_sock;   // Socket for connections to server
    int sock_acc;   //Socket client connects to
    
    std::vector<struct pollfd> fds; //vector for poll() to keep track of the clients

    if(argc != 2)
    {
        printf("Usage: tsamserver <ip port>\n");
        exit(0);
    }

    int port = atoi(argv[1]);
    listening_port = port;

    listen_sock = open_socket(port); //setting up the listening socket

    //error handling
    if(listen_sock < 0){
        exit(1);
    }

    if (listen(listen_sock, BACKLOG) < 0){
        perror("listen failed");
        close(listen_sock);
        exit(1);
    }
    else{
        fds.push_back({listen_sock, POLLIN, 0}); //listening socket at index 0 in the list
        std::string msg = "listening on port " + std::to_string(port);
        log_event("START",NULL,0,GROUP_ID,msg.c_str());
    }

    struct sockaddr_in dest_addr; //for setting up connection to server or client
    socklen_t dest_len;
    
    std::string tsam_ip = "130.208.246.98";
    int tsam_port = 5001;

   
    int sockfd = connect_to(tsam_port, tsam_ip);
    if (sockfd < 0){
        perror("error connecting");
    }

    else{
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        getsockname(sockfd, (struct sockaddr*)&addr, &len);
        listening_ip = inet_ntoa(addr.sin_addr);

        fds.push_back({sockfd, POLLIN, 0});
        log_event("CONN", tsam_ip.c_str(), tsam_port, NULL, "connecting");
        connections[sockfd].socket = sockfd;
        connections[sockfd].server_ip = tsam_ip;
        connections[sockfd].server_port = tsam_port;
        handshake[sockfd] = false;
        send_helo(sockfd);
    }


    while (!finished){
        check_keepalive();
        send_statusreq();
        //iterating through the client sockets, skipping the listening socket
        for (size_t i = 1; i < fds.size(); i++){
            int sock = fds[i].fd;
            fds[i].events = POLLIN; //poll lets know when data is available to read for that client
            
            //if the buffer did not send all at once trigger pollout to send the rest of the buffer
            if (!connections[sock].send_buffer.empty()){ 
                fds[i].events |= POLLOUT;
            }
        }

        //wait indefinetly until poll reports a socket is ready for reading/writing 
        //-1 in timeout parameter for indefinetly
        int n = poll(fds.data(), fds.size(), 1000);

        //stopping the loop if poll returns an error
        if(n < 0){
            perror("poll failed");
            finished = true;
            break;
        }
        // The limit is 7 here (vs 8 in accept) so we have always a free slot for someone to connect
        if (connections.size() < 7 && server_net.size() != 0){
            // server_net maps a group to the list of peers that the group send to us
            for (const auto& [key, servers] : server_net){
                for (const auto& neighbor: servers){
                    if (connections.size() >= 7){
                        break; // connection limits
                    }

                    // copy out the peer's details 
                    std::string group = neighbor.group;
                    std::string ip = neighbor.ip;
                    int port = std::stoi(neighbor.port);
                    bool connected = false;
                    // check if we are connected to this peer by matching group_id or ip+port
                    for (const auto& [sock, server] : connections){
                        if ((ip == server.server_ip && port == server.server_port) || group == server.group_id){
                            connected = true;
                        }
                    }

                    // skip peers we already connected to 
                    if (connected || group == GROUP_ID){
                        continue;
                    }


                    int sockfd = connect_to(port, ip);
                    if (sockfd < 0){
                        perror("error connecting");
                    }

                    else{
                        fds.push_back({sockfd, POLLIN, 0});
                        log_event("CONN", ip.c_str(), port, NULL, "connecting");

                        //create connection entry group_id and type
                        connections[sockfd].socket = sockfd;
                        connections[sockfd].server_ip = ip;
                        connections[sockfd].server_port = port;
                        handshake[sockfd] = false;
                        log_note("Able to connect to %s", neighbor.group.c_str());
                        //send helo to the server
                        send_helo(sockfd);
                    }
                }
            }
        }

        
        //checking if the listening socket has clients ready to connect
        if ((fds[0].revents & POLLIN) && connections.size() < 8){
            dest_len = sizeof(dest_addr);

            sock_acc = accept(listen_sock, (struct sockaddr *)&dest_addr, &dest_len);
            if (sock_acc < 0){
                perror("accept failed");
            }
            else{
                fds.push_back({sock_acc, POLLIN, 0}); //adding the socket to fds vector
                std::string ip_addr = inet_ntoa(dest_addr.sin_addr); //converting the ip address to string
                int port = ntohs(dest_addr.sin_port);
                connections[sock_acc].socket = sock_acc;
                connections[sock_acc].server_ip = ip_addr;
                connections[sock_acc].server_port = port;
                handshake[sock_acc] = false;
                log_event("ACCEPT", ip_addr.c_str(), port, NULL, "incoming connection" );
            }
        }
        handle_client_data(fds); //processing client data
    }
}
