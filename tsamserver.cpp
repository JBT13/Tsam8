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
//structure that stores information about/for client
//TODO: 
//ef ég fæ sendmsg senda á alla server sem ég er connected to 
//þótt það sé frá client eða öðrum server)
//Finna út hvernig ég á að sjá mun á server og client 
//Hversu margir clients
//TODO: 24.10
//Gera Helo og Servers
//hvað ef client sendir HELO?
//hafa client count?
//laga server log fyrir send 
struct connected{
    std::string group_id;
    int socket;
    std::string server_ip;
    int server_port;
    std::string type;
    std::string receive_buffer;
    std::string send_buffer;

    bool helo_sent = false;
};

//structure that keeps track of the clients inbox
struct message {
    std::vector<std::string> msg_from;
    std::vector<std::string> message_box;
};

//response from servers
struct server_list{
    std::string group;
    std::string ip;
    std::string port;
};

//keeping track of different information for both connected servers and client
//ef server listi með 8 þá sleppa
std::map<std::string, message> msgbox; //the key is the clients udername
std::map<std::string, message> stored_message; //storing message for a group if connection is not found 
std::map<int, connected> connections; //the key is the server socket number
std::map <std::string, std::vector<struct server_list>> server_net; //map where it connects a group to it's connected servers
std::map <int, bool> handshake; //keeping track of after accept to be able to complete protocol with server
bool client_connected;


// "Human-readable time" log
std::string timestamp(){
    time_t current_t = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&current_t));
    return std::string(ts);
}

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
        perror("Failed to bind to socket:");
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



void send_get_msg(int sock){
    //construction the message that gets sent for GETMSQ
    std::string get_msg;
    std::string username = connections[sock].group_id;

    std::vector<std::string> message = msgbox[username].message_box;
    std::vector<std::string> from = msgbox[username].msg_from;

    //if the inbox has a message stored for the client then get the front message 
    //and who sent that corresponding message
    if (!msgbox[username].message_box.empty()){
        std::string msg_send = message.front();
        std::string from_user = from.front();
        
        get_msg = "From: " + from_user + "\n" + "Message: " + msg_send;
    
        //erasing the message from the clients inbox
        message.erase(message.begin()); 
        from.erase(from.begin());

        //now it stores the messagebox with the message deleted
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
}



void send_statusreq(int sock){
    std::string status_req; //construction the message that gets sent for STATUSREQ

    //iterating over each clients inbox
    for (const auto& [name, msg] : msgbox){
        int count = msg.message_box.size();  //getting the number of messages the client has stored

        //only add the clients who has a message stored to the response string
        if (count > 0){
            if (!status_req.empty()) { //so , does not get added to the front
                    status_req += ",";
            }
            status_req += name + ":" + std::to_string(count);

        }      
    }

    if (status_req.empty()){
        status_req = "EMPTY";
    }
    
    //creating the buffer for send
    auto buffer = create_msg(status_req);
    //appending the buffer to the client struct
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
        
}

void send_confirmation(int sock, std::string message){
    //sending confirmation to client
    auto buffer = create_msg(message);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
}

void send_error(int sock, std::string message){
    //sending the error message to client
    auto buffer = create_msg(message);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
}

void send_servers(int sock){
    std::string connected_servers = "SERVERS";

    
    for (const auto& [key, servers] : connections){
        if (servers.type == "CLIENT" || servers.group_id.empty()){
            continue;
        }
        
        if (!connected_servers.empty()){
            connected_servers += ",";
        }
        connected_servers += servers.group_id + "," + servers.server_ip + "," + std::to_string(servers.server_port);
    }

    auto buffer = create_msg(connected_servers);
    //appending the buffer to the connection struct
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
}

void send_helo(int sock){

    if (connections[sock].helo_sent){
        return;
    }
    std::string group_name = GROUP_ID;
    std::string helo_msg = "HELO," + group_name;
    
    auto buffer = create_msg(helo_msg);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());
    connections[sock].helo_sent = true;
}


void to_send(int sock, std::string msg, std::string send_to){
    //first checking if we are directly connected to the group
    std::string forward = "";
    for (const auto& [key, servers] : connections){
        if (servers.group_id == send_to){
            auto buffer = create_msg(msg);
            connections[key].send_buffer.append(buffer.data(), buffer.size());
            return;
        }
    }

    //else checking if one of our connection is connected to it
    for (const auto& [key, servers] : server_net){
        for (const auto& neighbor: servers){
            if (neighbor.group == send_to){
                forward = key;
                break;
            }
        }
        if (!forward.empty()){
            break;
        }
    }

    //finding which server is the next hop to forward the message 
    for (const auto& [key, servers] : connections){
        if (forward == servers.group_id){
            auto buffer = create_msg(msg);
            connections[key].send_buffer.append(buffer.data(), buffer.size());
            log_note("not directly connected to group, forwarding to %s", servers.group_id.c_str());
            return;
        }
    }

    //no connection to group found
    //TODO: figure this out later 
    stored_message[send_to].message_box.push_back(msg);
    log_note("couldn't find route to group %s, storing its message for now", send_to.c_str());
}

void forward_stored_msg(int sock, std::string group_name){
    std::vector<std::string> message = stored_message[group_name].message_box;
    if (message.empty()){
        return;
    }
    std::string msg_send = message.front();

    //TODO: hafa for loopu ef hann skildi vera með mörg message 
    auto buffer = create_msg(msg_send);
    connections[sock].send_buffer.append(buffer.data(), buffer.size());

    //erasing the message from the clients inbox
    message.erase(message.begin()); 
       
    //now it stores the messagebox with the message deleted
    stored_message[group_name].message_box = message;

}
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
    
    if (tokens[0] == "SERVERS"){
        main_connection = connections[sock].group_id;
        //TODO: prenta og server loga listann
        tokens.erase(tokens.begin());
        //TODO: ath n modulus 3?
        while (tokens.size() >= 3){
            std::string group_id = tokens[0];
            std::string group_ip = tokens[1];
            std::string group_port = tokens[2];
            server_net[main_connection].push_back({group_id, group_ip, group_port});
            //mjög ljótur kóði
            if (!stored_message.empty()){
                for (const auto& [name, messages] : stored_message){
                    if (name == group_id){
                        forward_stored_msg(sock, name);
                        break;
                    }
                }
            }
            tokens.erase(tokens.begin());
            tokens.erase(tokens.begin());
            tokens.erase(tokens.begin());
        }
    }
                                        //ef þetta er frá client þá bæta við timestamp
    else if (tokens[0] == "SENDMSG"){  //TODO: ath hvort það sé til okkar group þá setja í client message box annars áfram senda:
        //extracting appropriate fields from tokens         
         std::string message_send;

        if (tokens.size() < 4){
            error_msg = "invalid command for SENDMSG";
            send_error(sock, error_msg);
            return;
        }
        from = tokens[1];
        send_to = tokens[2];
        

        for (size_t i = 4; i < tokens.size(); i++){
            msg_send += "," + tokens[i]; //if message included , that got split. Putting it back together
        }

        if (connections[sock].type == "CLIENT"){
            //connected user does not match the from user in the command
            if (connections[sock].group_id != from){
                error_msg = "sender does not match connected user";
                send_error(sock, error_msg);
                return;
            }
            msg_send = tokens[3];
            std::string curr_time = timestamp();
            message_send = "SENDMSG," + from + "," + send_to + "," + curr_time + "," + msg_send; 
            to_send(sock, message_send, send_to);

        }        
       
        else if (send_to == GROUP_ID){
            msgbox[send_to].message_box.push_back(msg_send);
            msgbox[send_to].msg_from.push_back(from);
        }
        
        else{
            std::string timestamp = tokens[3];
            msg_send = tokens[4];
            message_send = "SENDMSG," + from + "," + send_to + "," + timestamp + "," + msg_send;
            to_send(sock, message_send, send_to);
        }
        
    }
        
    else if(tokens[0] == "GETMSG"){
        from = tokens[1];

        //client must connect as a user before sending a message
        if (connections[sock].group_id.empty()){
            error_msg = "must connect as a user before";
            send_error(sock, error_msg);
            return;
        }
         
        //names do not match
        if (connections[sock].group_id != from){
            error_msg = "requested user does not match connected user";
            send_error(sock, error_msg);
            return;
        }  

        send_get_msg(sock);
    }

    else if(tokens[0] == "STATUSREQ"){
        ("STATUSREQ sent to: " + connections[sock].group_id);
        send_statusreq(sock);
 
    }

    //unknown command from the client 
    else{
        std::cout << "Unknown command from client" << std::endl;
        error_msg = "Unknown command\nValid commands: CONNECT, SENDMSG, GETMSG, STATUSREQ"; //TODO: LAGA ÞETTA
        send_error(sock, error_msg);
        return;
    }
    
}

void first_command(int sock, std::vector<std::string> tokens){
    std::string error_msg;

    if (tokens.empty()){ //no command
        return;
    }

    if (tokens[0] == "HELO"){
        connections[sock].group_id = tokens[1];
        connections[sock].type = "SERVER";
        handshake[sock] = true;
        send_helo(sock);
        send_servers(sock);
        return;
    }

    if (tokens[0] == "CONNECT"){
        if (client_connected == true){
            error_msg = "Client already connected";
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
        }

        connections[sock].type = "CLIENT";
        connections[sock].group_id = username;
        handshake[sock] = true;
        client_connected = true;
        std::cout << GROUP_ID <<" connected to socket: " << sock << std::endl;
        std::string msg_confirmation = "Successfully connected"; //reporting to client that the connection was succesful
        (tokens[1] + " connected to server");
        send_confirmation(sock, msg_confirmation);
        return;
    }

    else{
        std::cout << "Unknown command" << std::endl;
        error_msg = "Unknown command";
        send_error(sock, error_msg);
        return;
    }
}

void client_command(int sock){
    //refrencing the clients buffer to read its data
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

        log_event_bytes("RECV", ip_addr.c_str(), port, GROUP_ID, command.data(), command.size());
        //removing new line to get the right amount of bytes
        if (command.back() == '\n'){
            command.pop_back();
        }

        data.erase(0, frame_length);
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

            //receiving something from the client
            int sock = fds[i].fd;
            //setting the recv to nonblocking with MSG_DONTWAIT 
            //if there is no data available it does not wait
            
            
            ssize_t bytes_recv = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
            
            //appending to the clients buffer the bytes received
            if (bytes_recv > 0){
                connections[sock].receive_buffer.append(buffer, bytes_recv);
                client_command(sock); //processing the command client sent
            }

            else if (bytes_recv == 0){ //when client has called close(socket)
                //closing the client socket and erasing it from the list
                if (connections[sock].type == "CLIENT"){
                    client_connected = false;
                }
                close(sock);
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
            std::string &data = connections[sock].send_buffer; //getting the buffer which contains the response to client

            std::string ip_addr = connections[sock].server_ip;
            int port = connections[sock].server_port;

            //sending buffered data to the client, remaining data remains for POLLOUT
            if (!data.empty()){
                //MSG_DONTWAIT for send to be nonblocking
                int sent = send(sock, data.data(), data.size(), MSG_DONTWAIT);
                
                if (sent > 0){
                    log_event_bytes("SEND", ip_addr.c_str(), port, GROUP_ID, data.data(), sent);
                    data.erase(0, sent); 
                }
                else if (sent <= 0){
                    //closing the client socket and erasing it from the list
                    if (connections[sock].type == "CLIENT"){
                        client_connected = false;
                    }
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
    
    // Server address from command line
    struct sockaddr_in srcaddr;
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_port = htons(5001);

    std::string tsam_ip = "130.208.246.98";

    if (inet_pton(AF_INET, tsam_ip.c_str(), &srcaddr.sin_addr) < 0){
        perror("Invalid ip address");
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (connect(sockfd, (struct sockaddr*)&srcaddr, sizeof(srcaddr)) < 0){
        perror("error connecting");
        exit(1);
    }

    else{
        fds.push_back({sockfd, POLLIN, 0});
        log_event("CONN", tsam_ip.c_str(), 5001, NULL, "Connecting to");
        connections[sockfd].socket = listen_sock;
        connections[sockfd].server_ip = tsam_ip;
        connections[sockfd].server_port = 5001;
        handshake[sockfd] = false;
        send_helo(sockfd);
    }


    while (!finished){
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
        int n = poll(fds.data(), fds.size(), -1);

        //stopping the loop if poll returns an error
        if(n < 0){
            perror("poll failed");
            finished = true;
            break;
        }

        //checking if the listening socket has clients ready to connect
        if (fds[0].revents & POLLIN){
            dest_len = sizeof(dest_addr);

            sock_acc = accept(listen_sock,  (struct sockaddr *)&dest_addr, &dest_len);
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
