#ifdef __GNUC__
#pragma GCC system_header
#endif

#include "protocol.h"

#include <stdint.h>
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

#define DEBUG 0

using namespace std;

// Double conversion helpers
void convertDoubleToNet(double hostVal, double* netVal) {
    uint64_t temp;
    memcpy(&temp, &hostVal, sizeof(double));
    temp = (((uint64_t)htonl(temp & 0xFFFFFFFFULL)) << 32) | htonl(temp >> 32);
    memcpy(netVal, &temp, sizeof(double));
}

void convertDoubleFromNet(double netVal, double* hostVal) {
    uint64_t temp;
    memcpy(&temp, &netVal, sizeof(double));
    temp = (((uint64_t)ntohl(temp & 0xFFFFFFFFULL)) << 32) | ntohl(temp >> 32);
    memcpy(hostVal, &temp, sizeof(double));
}

// Cleanup helper
void cleanup(int sockfd, addrinfo* res) {
    if (sockfd >= 0) close(sockfd);
    if (res) freeaddrinfo(res);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    char *hostStr = strtok(argv[1], ":");
    char *portToken = strtok(NULL, ":");
    if (!hostStr || !portToken) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    cout << "Host " << hostStr << ", and port " << portToken << "." << endl;

    addrinfo hints{}, *res = nullptr, *cur = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(hostStr, portToken, &hints, &res) != 0) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        return 1;
    }

    int sockfd = -1;
    for (cur = res; cur != nullptr; cur = cur->ai_next) {
        sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sockfd != -1) break;
    }

    if (sockfd == -1) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    timeval tv{2, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#if DEBUG
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    getsockname(sockfd, (sockaddr*)&local, &len);
    cerr << "Connected to " << hostStr << ":" << portToken
         << " local port " << ntohs(local.sin_port) << endl;
#endif

    // Prepare and send calcMessage
    calcMessage initMsg = { htons(22), htons(0), htons(17), htons(1), htons(0) };
    char buffer[1024];
    ssize_t n;
    bool gotReply = false;
    int attempts = 0;

    while (attempts < 3 && !gotReply) {
        sendto(sockfd, &initMsg, sizeof(initMsg), 0, cur->ai_addr, cur->ai_addrlen);
        socklen_t addrLen = cur->ai_addrlen;
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, cur->ai_addr, &addrLen);
        if (n >= 0) gotReply = true;
        else attempts++;
    }

    if (!gotReply) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    if (n == sizeof(calcMessage)) {
        calcMessage failMsg;
        memcpy(&failMsg, buffer, sizeof(calcMessage));
        if (ntohs(failMsg.type) == 2 && ntohs(failMsg.message) == 2)
            cout << "NOT OK - server does not support protocol" << endl;
        else
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    if (n != sizeof(calcProtocol)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    calcProtocol protoPkt;
    memcpy(&protoPkt, buffer, sizeof(calcProtocol));

    // Unpack fields
    uint16_t pktType = ntohs(protoPkt.type);
    uint16_t verMajor = ntohs(protoPkt.major_version);
    uint16_t verMinor = ntohs(protoPkt.minor_version);
    uint32_t pktId = ntohl(protoPkt.id);
    uint32_t opCode = ntohl(protoPkt.arith);
    int32_t val1 = ntohl(protoPkt.inValue1);
    int32_t val2 = ntohl(protoPkt.inValue2);

    double dVal1, dVal2, tmp;
    memcpy(&tmp, &protoPkt.flValue1, sizeof(double));
    convertDoubleFromNet(tmp, &dVal1);
    memcpy(&tmp, &protoPkt.flValue2, sizeof(double));
    convertDoubleFromNet(tmp, &dVal2);

    if (verMajor != 1 || verMinor != 0 || opCode < 1 || opCode > 8) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    string opStr;
    switch(opCode) {
        case 1: opStr = "add"; break;
        case 2: opStr = "sub"; break;
        case 3: opStr = "mul"; break;
        case 4: opStr = "div"; break;
        case 5: opStr = "fadd"; break;
        case 6: opStr = "fsub"; break;
        case 7: opStr = "fmul"; break;
        case 8: opStr = "fdiv"; break;
    }

    cout << "ASSIGNMENT: " << opStr << " ";
    if (opCode <= 4) cout << val1 << " " << val2 << endl;
    else cout << dVal1 << " " << dVal2 << endl;

    double resultD = 0;
    int32_t resultI = 0;

    if (opCode <= 4) {
        if (opCode == 4 && val2 == 0) {
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
            cleanup(sockfd, res);
            return 1;
        }
        switch(opCode) {
            case 1: resultI = val1 + val2; break;
            case 2: resultI = val1 - val2; break;
            case 3: resultI = val1 * val2; break;
            case 4: resultI = val1 / val2; break;
        }
        protoPkt.inResult = htonl(resultI);
    } else {
        if (opCode == 8 && dVal2 == 0) {
            cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
            cleanup(sockfd, res);
            return 1;
        }
        switch(opCode) {
            case 5: resultD = dVal1 + dVal2; break;
            case 6: resultD = dVal1 - dVal2; break;
            case 7: resultD = dVal1 * dVal2; break;
            case 8: resultD = dVal1 / dVal2; break;
        }
        double netD;
        convertDoubleToNet(resultD, &netD);
        memcpy(&protoPkt.flResult, &netD, sizeof(double));
    }

#if DEBUG
    cerr << "Calculated the result to: " << (opCode <= 4 ? to_string(resultI) : to_string(resultD)) << endl;
#endif

    // Repack
    protoPkt.type = htons(pktType);
    protoPkt.major_version = htons(verMajor);
    protoPkt.minor_version = htons(verMinor);
    protoPkt.id = htonl(pktId);
    protoPkt.arith = htonl(opCode);

    attempts = 0;
    gotReply = false;
    while (attempts < 3 && !gotReply) {
        sendto(sockfd, &protoPkt, sizeof(protoPkt), 0, cur->ai_addr, cur->ai_addrlen);
        socklen_t addrLen = cur->ai_addrlen;
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, cur->ai_addr, &addrLen);
        if (n >= 0) gotReply = true;
        else attempts++;
    }

    if (!gotReply || n != sizeof(calcMessage)) {
        cout << "ERROR WRONG SIZE OR INCORRECT PROTOCOL" << endl;
        cleanup(sockfd, res);
        return 1;
    }

    calcMessage finalMsg;
    memcpy(&finalMsg, buffer, sizeof(calcMessage));
    uint16_t finalVal = ntohs(finalMsg.message);

    if (finalVal == 0)
        cout << "OK (myresult=" << (opCode <= 4 ? resultI : resultD) << ")" << endl;
    else
        cout << "NOT OK (myresult=" << (opCode <= 4 ? resultI : resultD) << ")" << endl;

    cleanup(sockfd, res);
    return 0;
}


