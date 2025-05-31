#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "chat.h"

/*#define PORT 12345*/
#define SERVER_ADDR "127.0.0.1"

void error(const char* msg) {
    perror(msg);
    exit(1);
}

int settingUpClientSocket(int PORT) {
    
    struct sockaddr_in serverAddress = setupServer(PORT);
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        error("Error creating socket");
    }
    if (connect(sockfd, (struct sockaddr*)&serverAddress,
            sizeof(struct sockaddr_in))
        != 0) {
        error("Error connecting to server");
    }
    int on;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on))
        < 0) {
        error("Error setting reuse option\n");
    }
    // Fill in the server address structure
    if (inet_aton(SERVER_ADDR, &serverAddress.sin_addr) == 0) {
        error("Error converting address");
    }

    return sockfd;
}

int main(int argc, char** argv) {
    /*the changes in this function is to check the send message from client if it contains @ charecter otherwise it will send as broadcast*/
    if (argc < 3){
       printf("Please specify a nickname and/or PORT number\n");
    }else{
	printf("Welcome %s\n", argv[1]);
	    
	int PORT = atoi(argv[2]);
	struct Message outgoing;
	strcpy(outgoing.nickname, argv[1]);

	int sockfd = settingUpClientSocket(PORT);

	struct Message incoming;
	struct pollfd fds[2];
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = sockfd;
	fds[1].events = POLLIN;
	
	// Start sending messages
        while (1) {
            printf("%s> ", outgoing.nickname);
            fflush(stdout);
            int retpoll = poll(fds, 2, -1);
            if (retpoll < 0) {
                perror("poll failed");
                return -1;
            }
            if (retpoll == 0) {
                continue;
            }
            if (fds[0].revents == POLLIN) {
                char input[BUFFER_LENGTH + NAME_LENGTH];
                fgets(input, sizeof(input), stdin);
                // Cleanup \n from input
                input[strcspn(input, "\n")] = 0;

                // Check if the input contains a recipient to send private messages
                if (input[0] == '@') {
                    // Parse the recipient
                    char *space = strchr(input, ' ');
                    if (space != NULL) {
                        size_t recipient_len = space - input - 1;
                        strncpy(outgoing.recipient, input + 1, recipient_len);
                        outgoing.recipient[recipient_len] = '\0';
                        strncpy(outgoing.message, space + 1, sizeof(outgoing.message) - 1);
                        outgoing.message[sizeof(outgoing.message) - 1] = '\0';
                    } else {
                        printf("Invalid private message format. Use @recipient message\n");
                        continue;
                    }
                } else {
                    // to send broadcast the message
                    outgoing.recipient[0] = '\0';
                    strncpy(outgoing.message, input, sizeof(outgoing.message) - 1);
                    outgoing.message[sizeof(outgoing.message) - 1] = '\0';
                }

                if (sendMessage(sockfd, &outgoing) < 0) {
                    perror("Error sending message");
                    break;
                }
            } else if (fds[1].revents == POLLIN) {
                if (readMessage(fds[1].fd, &incoming) < 0) {
                    perror("Error reading message");
                    break;
                }
                printf("\r%s> %s\n", incoming.nickname, incoming.message);
                printf("%s> ", outgoing.nickname);
                fflush(stdout);
            } else {
                printf("Unexpected revent\n");
                break;
            }
        }
        close(sockfd);
    }
    return 0;
}

