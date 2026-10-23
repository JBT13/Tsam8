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
#include <poll.h>
#include <ctime>
#include <fstream>

#include <iostream>
#include <sstream>
#include <map>

//output file for client instance
std::ofstream out_file;

// "Human-readable time" log
std::string timestamp(){
    time_t current_t = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&current_t));
    return std::string(ts);
}

// The screen and the file match
void log(const std::string &text){
    std::ofstream out_file("client-output.txt", std::ios::app);

    //error checking
    if (!out_file.is_open()){
        std::cerr << "Error: Could not open the file!" << std::endl;
        return;
    }
    std::string line = "[" + timestamp() + "]" + text;
    out_file << line << std::endl;
    
}


int main(int argc, char* argv[])
{   
    std::string send_buffer;
    std::string receive_buffer;
    bool finished = false; 
    int set = 1;
                            // Toggle for setsockopt
    if(argc < 3 || argc > 4)
    {
        printf("Usage: tsamclient <ip  port>\n");
        printf("Ctrl-C to terminate\n");
        exit(0);
    }

    // Append so restarting the client doesn't erase earlier output
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Server address from command line
    struct sockaddr_in srcaddr;
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_port = htons(atoi(argv[2]));

   
    if (inet_pton(AF_INET, argv[1], &srcaddr.sin_addr) < 0){
        perror("Invalid ip address");
        exit(1);
    }

    // restart the client quickly without the port being stuck
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0){
        printf("Failed to set SO_REUSEADDR for port %s\n", argv[2]);
        perror("setsockopt failed: ");
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr*)&srcaddr, sizeof(srcaddr)) < 0){
        perror("error connecting");
        exit(1);
    }

    log(std::string("Connected to ") + argv[1] + ":" + argv[2]);
    printf("Type CONNECT,<username> to log in\n");
    
    
    // Keyboard and server at the same time typing never block incoming messages
    struct pollfd fds[2];

    //first instance is keyboard input
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = sockfd;
    fds[1].revents = 0;
    int nfds = 2;

    while(!finished){

        fds[1].events = POLLIN;

        // only writes when there is something to write
        // else poll() would return constantly
        if (!send_buffer.empty()){
            fds[1].events |= POLLOUT;
        }

        int n = poll(fds, nfds, -1);

        if(n < 0){
            perror("poll failed: ");
            finished = true;
            continue;
        }

        if(fds[0].revents & POLLIN){ //keyboard input
            // Frame <SOH><length><STX><command><ETX>
            char SOH = 0x01;
            char STX = 0x02;
            char ETX = 0x03;

            char buffer[5005]; //for input
            char frame[5005]; //for sending
            int offset = 0;

            memset(buffer, 0, sizeof(buffer));
            fgets(buffer, sizeof(buffer), stdin); //input

            int cmd_size = strlen(buffer);

            if (cmd_size > 5000){
                printf("Message too long");
                continue;
            }

            // Length covers the whole frame command + 5 framing bytes,
            uint16_t total_size =  htons(cmd_size + 5);

            memcpy(frame + offset, &SOH, sizeof(SOH));
            offset += sizeof(SOH);

            memcpy(frame + offset, &total_size, sizeof(total_size));
            offset += sizeof(total_size);

            memcpy(frame + offset, &STX, sizeof(STX));
            offset += sizeof(STX);

            memcpy(frame + offset, buffer, cmd_size);
            offset += cmd_size;

            memcpy(frame + offset, &ETX, sizeof(ETX));
            offset += sizeof(ETX);

            // Queued here, actually sent when POLLOUT is triggered
            send_buffer.append(frame, offset);
        }

        if (fds[1].revents & POLLOUT){ //data ready to be sent

            if (!send_buffer.empty()){
                int sent = send(sockfd, send_buffer.data(), send_buffer.size(), MSG_DONTWAIT);

                // Only part may have been sent, so keep the rest for later
                if (sent > 0){
                    send_buffer.erase(0, sent);
                }
                else if (sent < 0){
                    perror("Error sending");
                    finished = true;
                }
            }

        }

        if (fds[1].revents & POLLIN){ //data available to read
            char respond[5005];
            int bytes_recv = recv(sockfd, respond, sizeof(respond), MSG_DONTWAIT);
            if (bytes_recv > 0){
                receive_buffer.append(respond, bytes_recv);

                // pull out every complete frame currently in the buffer 
                while (true){
                    if (receive_buffer.size() < 4){
                        break; //can't read frame for length 
                    }

                    // wrong markers = lost track of frame
                    if (receive_buffer[0] != 0x01 || receive_buffer[3] != 0x02){
                        printf("invalid frame");
                        finished = true;
                        break;
                    }

                    uint16_t frame_length;

                    memcpy(&frame_length, receive_buffer.data() + 1, sizeof(frame_length));

                    frame_length = ntohs(frame_length);

                    if (frame_length < 5){
                        break;
                    }

                    if (receive_buffer.size() < frame_length){ //waiting for the complete frame to start proccessing command
                        break;
                    }

                    if (receive_buffer[frame_length - 1] != 0x03){
                        break;
                    }

                    // Strip the framing so only the server's reply is shown
                    std::string response = receive_buffer.substr(4, frame_length - 5);
                    log("RECEIVED: " + response);
                    std::cout << response << std::endl;
                    receive_buffer.erase(0, frame_length);
                }
            }
            
            // the server closed the connection
            else if (bytes_recv == 0){
                log("Disconnected from server");
                printf("Disconnected server");
                finished = true;
            }

            else{
                perror("Error receiving ");
                finished = true;
            }

        }

    }
    close(sockfd);
    return 0;
}