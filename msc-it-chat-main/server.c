#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#include "chat.h"
#include "server.h"

/* remove the default port to assigne port number 
#define PORT 12345*/

#define MAX_LINE_LENGTH 1024

void error(const char* msg) {
    perror(msg);
    exit(1);
}

struct Server createServer() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        error("Error creating socket");
    }
    struct Server retval;
    retval.nbClients = 0;
    memset(&retval.fds[0], 0, sizeof(struct pollfd));
    retval.fds[0].fd = fd;
    retval.fds[0].events = POLLIN;
    
    // Bind the socket to the address
    /* import the port number and assigne the input number to PORT parameter */
    int PORT;
    printf("Enter The PORT Number: ");
    scanf("%d",&PORT);
    struct sockaddr_in serverAddress = setupServer(PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(retval.fds[0].fd, (struct sockaddr*)&serverAddress,
            sizeof(serverAddress))
        == -1) {
        error("Error binding");
    }
    int on;
    if (setsockopt(
            retval.fds[0].fd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on))
        < 0) {
        error("Error setting reuse option\n");
    }
    if (ioctl(retval.fds[0].fd, FIONBIO, (char*)&on) < 0) {
        error("Error setting non block option\n");
    }
    if (listen(retval.fds[0].fd, 32) < 0) {
        error("Error setting reuse option\n");
    }
    return retval;
}

void createlog(const struct Message* message) {
    /*
    this function aim to store the chat history to chat_log text file */
    if (strlen(message->message) > 0) {
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        
        char log_date[20];
        strftime(log_date, sizeof(log_date), "%Y-%m-%d %H:%M:%S", tm);
        
        FILE* history_file = fopen("chat_log.txt", "a");
        if (history_file == NULL) {
            perror("Error opening chat log file");
            return;
        }
        
        fprintf(history_file, "%s > %s> %s\n", log_date, message->nickname, message->message);
        fclose(history_file);
    }
}


int acceptNewClients(struct Server* _server) {
    /* new change to print the name of client */
    if (_server == NULL) {
        return -2;
    }
    int newFd;
    char nickname[MAX_NICKNAME_LENGTH]; // Buffer to store client nickname
    newFd = accept(_server->fds[0].fd, NULL, NULL);
    if (newFd < 0) {
        if (errno != EWOULDBLOCK) {
           perror("accept() failed");
           return -1;
        }
    }
    _server->nbClients++;
    memset(&_server->fds[_server->nbClients], 0, sizeof(struct pollfd));
    _server->fds[_server->nbClients].fd = newFd;
    _server->fds[_server->nbClients].events = POLLIN;

    // Receive nickname from the client
    int bytesRead = recv(newFd, nickname, MAX_NICKNAME_LENGTH - 1, 0);
    if (bytesRead < 0) {
        perror("Error receiving nickname");
        return -1;
    }
    nickname[bytesRead] = '\0'; // Null-terminate the received nickname
    strcpy(_server->clientNicknames[_server->nbClients], nickname); // Store the nickname
    
    printf("Client '%s' connected\n", nickname); // Print the nickname of the connected client
    return 0;
}

int findClientByName(struct Server* _server, const char* nickname) {
    /*aims to locate a client within the server's list of connected clients by
     their nickname and return the corresponding client's index */
    for (int i = 1; i < _server->nbClients + 1; ++i) {
        if (strcmp(_server->clientNicknames[i], nickname) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

int receiveAndBroadcastMessage(struct Server* _server, int _sendingClientFd) {
    /*add a filter to check if the message should send as private or broadcast */
    if (_server == NULL) {
        return -3;
    }
    struct Message message;
    int read = readMessage(_sendingClientFd, &message);
    if (read < 0) {
        return -1;
    }   
   /*Message history recording starts here*/
   createlog(&message);
   // Check if the message is a private message
    if (message.message[0] == '@') {
        // Extract recipient's nickname from the message
        char recipient[MAX_NICKNAME_LENGTH];
        sscanf(message.message, "@%s", recipient);

        // Find recipient's client ID
        int recipientClientId = findClientByName(_server, recipient);
        if (recipientClientId < 0) {
            // Handle recipient not found
            return -2;
        }

        // Send the private message to the recipient
        sendMessage(_server->fds[recipientClientId].fd, &message);
    } else {
        // Broadcast message Send message to all other clients
    for (int j = 1; j < _server->nbClients + 1; ++j) {
        if (_server->fds[j].fd == _sendingClientFd) {
            continue;
        }
        int sent = sendMessage(_server->fds[j].fd, &message);
        if (sent < 0) {
            _server->fds[j].fd = -1;
            return -2;
        }
    }
    // Print the message
    printf("%s> %s\n", message.nickname, message.message);
    return 0;
}
}

void compactDescriptor(struct Server* _server) {
    /* print the nicknames */
    if (_server == NULL) {
        return;
    }
    for (int i = 1; i < _server->nbClients + 1; i++) {
        if (_server->fds[i].fd == -1) {
            // Print the nickname of the disconnected client
            printf("Client '%s' disconnected\n", _server->clientNicknames[i]);
            // Remove the disconnected client and update the number of clients
            for (int j = i; j < _server->nbClients; j++) {
                _server->fds[j].fd = _server->fds[j + 1].fd;
                strcpy(_server->clientNicknames[j], _server->clientNicknames[j + 1]);
            }
            _server->nbClients--;
            printf("Current online clients: %d\n", _server->nbClients); // Print current number of clients
            i--; // Adjust the index after removing a client
        }
    }
}



void runServer(struct Server* _server) {
    /* print the status and fds when there is an action from client in case joining or dropping from the server session*/
    if (_server == NULL) {
        return;
    }
    int currentNbClients = _server->nbClients;
    while (1) {
        int retpoll = poll(_server->fds, _server->nbClients + 1, -1);
        if (retpoll < 0) {
            perror("Call to poll failed");
            break;
        }
        if (retpoll == 0) {
            continue;
        }
        // Only print "Checking 1 fds" when there is a change in the number of clients
        if (_server->nbClients != currentNbClients) {
            printf("Checking 1 fds\n");
            currentNbClients = _server->nbClients;
        }
        if (_server->fds[0].revents == POLLIN) {
            if (acceptNewClients(_server) == -1) {
                perror("Error accepting new clients");
                break;
            }
            if (_server->nbClients != currentNbClients) {
                printf("Current online clients: %d\n", _server->nbClients);
                currentNbClients = _server->nbClients;
            }
        }
        for (int i = 1; i < _server->nbClients + 1; i++) {
            if (_server->fds[i].revents == 0) {
                continue;
            }
            if (_server->fds[i].revents != POLLIN) {
                _server->fds[i].fd = -1;
                break;
            }
            int broadcast = receiveAndBroadcastMessage(_server, _server->fds[i].fd);
            if (broadcast == -1) {
                perror("Error reading message");
                _server->fds[i].fd = -1;
                break;
            }
        }
        compactDescriptor(_server);
    }
    for (int i = 0; i < _server->nbClients; ++i) {
        close(_server->fds[i].fd);
    }
}

int main() {
    struct Server server = createServer();
    runServer(&server);
    return 0;
}
