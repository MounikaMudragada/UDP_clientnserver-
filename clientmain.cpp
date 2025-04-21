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

// 64-bit conversion macros for little-endian systems.
#if __BYTE_ORDER == __LITTLE_ENDIAN
  #define htonll(x) (((uint64_t)htonl((x) & 0xFFFFFFFFULL)) << 32 | htonl((x) >> 32))
  #define ntohll(x) (((uint64_t)ntohl((x) & 0xFFFFFFFFULL)) << 32 | ntohl((x) >> 32))
#else
  #define htonll(x) (x)
  #define ntohll(x) (x)
#endif

// Helper functions to convert doubles between host and network order.
void convertDoubleToNet(double hostVal, double* netVal) {
    uint64_t temp;
    memcpy(&temp, &hostVal, sizeof(double));
    temp = htonll(temp);
    memcpy(netVal, &temp, sizeof(double));
}

void convertDoubleFromNet(double netVal, double* hostVal) {
    uint64_t temp;
    memcpy(&temp, &netVal, sizeof(double));
    temp = ntohll(temp);
    memcpy(hostVal, &temp, sizeof(double));
}

int main(int argc, char *argv[]) {
    // Print only the expected output.
    if (argc != 2) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    // Split input "ip:port" into host and port.
    char *hostStr = strtok(argv[1], ":");
    char *portToken = strtok(NULL, ":");
    if (!hostStr || !portToken) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    // Resolve address.
    addrinfo hints, *res, *cur;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM;   // UDP
    if (getaddrinfo(hostStr, portToken, &hints, &res) != 0) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    int sockfd = -1;
    for (cur = res; cur != NULL; cur = cur->ai_next) {
        sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sockfd != -1)
            break;
    }
    if (sockfd == -1) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        freeaddrinfo(res);
        return 1;
    }

    // Set a 2-second timeout on receives.
    timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    // --- 1. Send the initial calcMessage.
    calcMessage initMsg;
    initMsg.type = htons(22);
    initMsg.message = htons(0);
    initMsg.protocol = htons(17);
    initMsg.major_version = htons(1);
    initMsg.minor_version = htons(0);

    int attempts = 0;
    bool gotReply = false;
    char buffer[1024];
    ssize_t n;
    while (attempts < 3 && !gotReply) {
        if (sendto(sockfd, &initMsg, sizeof(initMsg), 0, cur->ai_addr, cur->ai_addrlen) == -1) {
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }
        socklen_t addrLen = cur->ai_addrlen;
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, cur->ai_addr, &addrLen);
        if (n >= 0)
            gotReply = true;
        else
            attempts++;
    }
    if (!gotReply) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    // --- 2. Process the received message.
    // If the reply is a calcMessage or not exactly calcProtocol, then error.
    if (n == sizeof(calcMessage) || n != sizeof(calcProtocol)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    calcProtocol protoPkt;
    memcpy(&protoPkt, buffer, sizeof(calcProtocol));

    // Convert fields from network order.
    uint16_t pktType = ntohs(protoPkt.type);
    uint16_t verMajor = ntohs(protoPkt.major_version);
    uint16_t verMinor = ntohs(protoPkt.minor_version);
    uint32_t pktId = ntohl(protoPkt.id);
    uint32_t opCode = ntohl(protoPkt.arith);
    int32_t val1 = ntohl(protoPkt.inValue1);
    int32_t val2 = ntohl(protoPkt.inValue2);

    double dVal1, dVal2, dummy;
    double tmp;
    memcpy(&tmp, &protoPkt.flValue1, sizeof(double));
    convertDoubleFromNet(tmp, &dVal1);
    memcpy(&tmp, &protoPkt.flValue2, sizeof(double));
    convertDoubleFromNet(tmp, &dVal2);
    memcpy(&tmp, &protoPkt.flResult, sizeof(double));
    convertDoubleFromNet(tmp, &dummy);

    if (verMajor != 1 || verMinor != 0) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    // --- 3. Perform the arithmetic operation.
    switch(opCode) {
        case 1: // addition
            protoPkt.inResult = htonl(val1 + val2);
            break;
        case 2: // subtraction
            protoPkt.inResult = htonl(val1 - val2);
            break;
        case 3: // multiplication
            protoPkt.inResult = htonl(val1 * val2);
            break;
        case 4: // division
            if (val2 == 0) {
                cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
                close(sockfd);
                freeaddrinfo(res);
                return 1;
            }
            protoPkt.inResult = htonl(val1 / val2);
            break;
        case 5: // floating-point addition
            {
                double resD = dVal1 + dVal2;
                double netD;
                convertDoubleToNet(resD, &netD);
                protoPkt.flResult = netD;
            }
            break;
        case 6: // floating-point subtraction
            {
                double resD = dVal1 - dVal2;
                double netD;
                convertDoubleToNet(resD, &netD);
                protoPkt.flResult = netD;
            }
            break;
        case 7: // floating-point multiplication
            {
                double resD = dVal1 * dVal2;
                double netD;
                convertDoubleToNet(resD, &netD);
                protoPkt.flResult = netD;
            }
            break;
        case 8: // floating-point division
            if (dVal2 == 0) {
                cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
                close(sockfd);
                freeaddrinfo(res);
                return 1;
            } else {
                double resD = dVal1 / dVal2;
                double netD;
                convertDoubleToNet(resD, &netD);
                protoPkt.flResult = netD;
            }
            break;
        default:
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
            close(sockfd);
            freeaddrinfo(res);
            return 1;
    }

    // Reassemble header fields.
    protoPkt.type = htons(pktType);
    protoPkt.major_version = htons(verMajor);
    protoPkt.minor_version = htons(verMinor);
    protoPkt.id = htonl(pktId);
    protoPkt.arith = htonl(opCode);

    // --- 4. Send the computed result and wait for the final reply.
    attempts = 0;
    gotReply = false;
    while (attempts < 3 && !gotReply) {
        if (sendto(sockfd, &protoPkt, sizeof(protoPkt), 0, cur->ai_addr, cur->ai_addrlen) == -1) {
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }
        socklen_t addrLen = cur->ai_addrlen;
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, cur->ai_addr, &addrLen);
        if (n >= 0)
            gotReply = true;
        else
            attempts++;
    }
    if (!gotReply) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    // --- 5. Process the final reply; it must be a calcMessage.
    if (n != sizeof(calcMessage)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }
    calcMessage finalMsg;
    memcpy(&finalMsg, buffer, sizeof(calcMessage));
    uint16_t finalVal = ntohs(finalMsg.message);
    // If the server considers the result correct, finalMsg.message will be 0.
    if (finalVal == 0) {
        cout << "OK (myresult=";
        if (opCode >= 1 && opCode <= 4) {
            int32_t resInt = ntohl(protoPkt.inResult);
            cout << resInt;
        } else {
            double resDbl;
            memcpy(&tmp, &protoPkt.flResult, sizeof(double));
            convertDoubleFromNet(tmp, &resDbl);
            cout << resDbl;
        }
        cout << ")" << endl;
    } else {
        cout << "NOT OK (myresult=";
        if (opCode >= 1 && opCode <= 4) {
            int32_t resInt = ntohl(protoPkt.inResult);
            cout << resInt;
        } else {
            double resDbl;
            memcpy(&tmp, &protoPkt.flResult, sizeof(double));
            convertDoubleFromNet(tmp, &resDbl);
            cout << resDbl;
        }
        cout << ")" << endl;
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}
