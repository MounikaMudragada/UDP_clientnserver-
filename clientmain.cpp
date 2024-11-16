#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <inttypes.h>
#include "protocol.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " ip:port\n";
        return 1;
    }

    char *serverHost = strtok(argv[1], ":");
    char *serverPort = strtok(NULL, ":");

    if (serverHost == NULL || serverPort == NULL) {
        cerr << "Invalid input format. Use ip:port\n";
        return 1;
    }

    int port = atoi(serverPort);
    printf("Host %s, and port %d.\n", serverHost, port);

    addrinfo hints, *serverInfo, *addrPtr;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP

    if (getaddrinfo(serverHost, serverPort, &hints, &serverInfo) != 0) {
        cerr << "Error getting server address info.\n";
        return 1;
    }

    int socketFd = -1;
    for (addrPtr = serverInfo; addrPtr != NULL; addrPtr = addrPtr->ai_next) {
        socketFd = socket(addrPtr->ai_family, addrPtr->ai_socktype, addrPtr->ai_protocol);
        if (socketFd != -1) break;
    }

    if (socketFd == -1) {
        cerr << "Failed to create socket\n";
        freeaddrinfo(serverInfo);
        return 1;
    }

    timeval timeout = {2, 0}; // 2-second timeout
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        perror("setsockopt");
        close(socketFd);
        freeaddrinfo(serverInfo);
        return 1;
    }

    // Prepare and send initial message
    calcMessage initMessage = {
        .type = htons(22),
        .message = htonl(0),
        .protocol = htons(17),
        .major_version = htons(1),
        .minor_version = htons(0)
    };

    if (sendto(socketFd, &initMessage, sizeof(initMessage), 0, addrPtr->ai_addr, addrPtr->ai_addrlen) == -1) {
        perror("sendto");
        close(socketFd);
        freeaddrinfo(serverInfo);
        return 1;
    }

    calcProtocol receivedMsg;
    int retries = 0;
    bool responseReceived = false;

    // Receive response
    while (retries < 3 && !responseReceived) {
        memset(&receivedMsg, 0, sizeof(receivedMsg));
        socklen_t addrLen = addrPtr->ai_addrlen;
        int bytesReceived = recvfrom(socketFd, &receivedMsg, sizeof(receivedMsg), 0, addrPtr->ai_addr, &addrLen);

        if (bytesReceived >= 0) {
            responseReceived = true;
        } else {
            if (sendto(socketFd, &initMessage, sizeof(initMessage), 0, addrPtr->ai_addr, addrPtr->ai_addrlen) == -1) {
                perror("sendto");
                close(socketFd);
                freeaddrinfo(serverInfo);
                return 1;
            }
            retries++;
        }
    }

    if (!responseReceived) {
        cerr << "No response from server\n";
        close(socketFd);
        freeaddrinfo(serverInfo);
        return 1;
    }

    // Handle arithmetic operation
    int num1 = ntohl(receivedMsg.inValue1);
    int num2 = ntohl(receivedMsg.inValue2);
    float fNum1 = receivedMsg.flValue1;
    float fNum2 = receivedMsg.flValue2;
    string resultStr;

    switch (ntohl(receivedMsg.arith)) {
        case 1: receivedMsg.inResult = htonl(num1 + num2); break;
        case 2: receivedMsg.inResult = htonl(num1 - num2); break;
        case 3: receivedMsg.inResult = htonl(num1 * num2); break;
        case 4: receivedMsg.inResult = htonl(num1 / num2); break;
        case 5: receivedMsg.flResult = fNum1 + fNum2; break;
        case 6: receivedMsg.flResult = fNum1 - fNum2; break;
        case 7: receivedMsg.flResult = fNum1 * fNum2; break;
        case 8: receivedMsg.flResult = fNum1 / fNum2; break;
        default:
            cerr << "Unknown operation\n";
            close(socketFd);
            freeaddrinfo(serverInfo);
            return 1;
    }

    // Send result back
    if (sendto(socketFd, &receivedMsg, sizeof(receivedMsg), 0, addrPtr->ai_addr, addrPtr->ai_addrlen) == -1) {
        perror("sendto");
        close(socketFd);
        freeaddrinfo(serverInfo);
        return 1;
    }

    calcMessage finalResponse;
    retries = 0;
    responseReceived = false;

    // Receive final response
    while (retries < 3 && !responseReceived) {
        memset(&finalResponse, 0, sizeof(finalResponse));
        socklen_t addrLen = addrPtr->ai_addrlen;
        int bytesReceived = recvfrom(socketFd, &finalResponse, sizeof(finalResponse), 0, addrPtr->ai_addr, &addrLen);

        if (bytesReceived >= 0) {
            responseReceived = true;
        } else {
            if (sendto(socketFd, &receivedMsg, sizeof(receivedMsg), 0, addrPtr->ai_addr, addrPtr->ai_addrlen) == -1) {
                perror("sendto");
                close(socketFd);
                freeaddrinfo(serverInfo);
                return 1;
            }
            retries++;
        }
    }

    if (!responseReceived) {
        cerr << "No response from server for result\n";
        close(socketFd);
        freeaddrinfo(serverInfo);
        return 1;
    }

    if (ntohl(finalResponse.message) == 1) {
        cout << "Server: OK!\n";
    } else {
        cerr << "Server: NOT OK!\n";
    }

    close(socketFd);
    freeaddrinfo(serverInfo);
    return 0;
}