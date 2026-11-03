#include "traceroute.h"
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>   // On Ubuntu, struct icmphdr lives here, not netinet/icmp.h
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

// 20 byte IP header + 8 byte ICMP header + 36 byte payload
const int PACKET_SIZE = 64;

std::string destIP;

// ****************************************************************************
// * Compute the Internet Checksum over an arbitrary buffer.
// * (written with the help of ChatGPT 3.5)
// ****************************************************************************
uint16_t checksum(unsigned short *buffer, int size) {
    unsigned long sum = 0;
    while (size > 1) {
        sum += *buffer++;
        size -= 2;
    }
    if (size == 1) {
        sum += *(unsigned char *) buffer;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (unsigned short) (~sum);
}

// Step 3: fill in the IP header at the front of the buffer
void fill_in_IP_header(char *packet) {
    struct iphdr *ip = (struct iphdr *)packet;

    ip->version  = 4;
    ip->ihl      = 5;
    ip->tos      = 0;
    ip->tot_len  = htons(PACKET_SIZE);
    ip->id       = 0;
    ip->frag_off = 0;
    ip->ttl      = 2;                          // Per project
    ip->protocol = IPPROTO_ICMP;
    ip->check    = 0;
    ip->saddr    = 0;
    ip->daddr    = inet_addr(destIP.c_str());
}

// Step 4: fill in the ICMP header right behind the IP header
void fill_in_ICMP_header(char *packet) {
    struct icmphdr *icmp = (struct icmphdr *)packet;

    icmp->type             = ICMP_ECHO;
    icmp->code             = 0;
    icmp->checksum         = 0;
    icmp->un.echo.id       = htons(getpid() & 0xFFFF);
    icmp->un.echo.sequence = htons(1);
}

// Steps 2-4: pattern-fill the buffer, then lay the headers over the front
void buildDatagram(char *packet) {
    memset(packet, 'A', PACKET_SIZE);
    fill_in_IP_header(packet);
    fill_in_ICMP_header(packet + sizeof(struct iphdr));
}

int main(int argc, char *argv[]) {

    // ********************************************************************
    // * Process the command line arguments
    // ********************************************************************
    int opt = 0;
    while ((opt = getopt(argc, argv, "t:d:")) != -1) {
        switch (opt) {
        case 't':
            destIP = optarg;
            break;
        case 'd':
            LOG_LEVEL = atoi(optarg);
            break;
        case ':':
        case '?':
        default:
            std::cout << "useage: " << argv[0] << " -t [target ip] -d [Debug Level]" << std::endl;
            exit(-1);
        }
    }
    if (destIP.empty()) {
        std::cout << "useage: " << argv[0] << " -t [target ip] -d [Debug Level]" << std::endl;
        exit(-1);
    }

    // Step 1: allocate two 64 byte buffers, one for sending and one for receiving
    char *sendBuffer = new char[PACKET_SIZE]();
    char *recvBuffer = new char[PACKET_SIZE]();

    // Steps 2-4: build the datagram in the send buffer
    buildDatagram(sendBuffer);
    DEBUG << "Datagram built for " << destIP << ENDL;

    // Destination address for sendto()
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(destIP.c_str());

    // Step 5: create the send and receive sockets
    int sendFd, recvFd;
    if ((sendFd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1) {
        perror("send socket");
        exit(-1);
    }
    if ((recvFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
        perror("receive socket");
        exit(-1);
    }
    DEBUG << "Send socket = " << sendFd << ", receive socket = " << recvFd << ENDL;

    // Step 6: while (CURRENT_TTL <= 31) and (reply-not-received)
    int currentTTL = 2;
    bool replyReceived = false;
    while (currentTTL <= 31 && !replyReceived) {
        // Step 6a: set TTL in IP header to current TTL
        struct iphdr *ip_header = (struct iphdr *)sendBuffer;
        ip_header->ttl = currentTTL;

        // Step 6b: fill in ICMP checksum
        struct icmphdr *icmp_header = (struct icmphdr *)(sendBuffer + sizeof(*ip_header));
        icmp_header->checksum = 0;
        icmp_header->checksum = checksum((unsigned short *)icmp_header, PACKET_SIZE - sizeof(*ip_header));

        // Step 6c: send the packet
        if (sendto(sendFd, sendBuffer, PACKET_SIZE, 0, (struct sockaddr *) &dest_addr,
                   sizeof(dest_addr)) == -1)
        {
            perror("sendto");
            return -1;
        }

        // Step 6d: While (now < START_TIME + 15) and (not-done-reading)
        time_t startTime = time(NULL);
        bool doneReading = false;
        int selectReturned = 0;
        fd_set readFdSet;
        struct timeval timeout;

        while (time(NULL) < startTime + 15 && !doneReading) {

            // Step 6d.i: use select() to sleep for up to 5 seconds, wake up if data arrives
            // Set timeout to 5 seconds.
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;

            // Set the bit in the set corresponding to file descriptor recvFd
            FD_ZERO(&readFdSet);
            FD_SET(recvFd, &readFdSet);

            // Call select()
            DEBUG << "Waiting up to 5 seconds to see if data becomes avalable" << ENDL;
            if ((selectReturned = select(recvFd + 1, &readFdSet, NULL, NULL, &timeout)) < 0) {
                perror("select");
                break;
            }
            DEBUG << "selectReturned = " << selectReturned << ENDL;

            // Confirm that select returned because our FD is set, not because of a timeout.
            if (!FD_ISSET(recvFd, &readFdSet)) {
                continue;   // timed out, check the 15 second limit again
            }
            DEBUG << "Bit #" << recvFd << " is set" << ENDL;

            // Step 6d.ii: data has arrived, read it with recvfrom()
            struct sockaddr_in recv_addr;
            socklen_t addr_len = sizeof(recv_addr);
            if (recvfrom(recvFd, recvBuffer, PACKET_SIZE, 0, (struct sockaddr *)&recv_addr, &addr_len) < 0)
            {
                perror("recvfrom");
                return -1;
            }

            char respondent_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &recv_addr.sin_addr, respondent_ip, INET_ADDRSTRLEN);

            struct iphdr *recv_ip = (struct iphdr *)recvBuffer;
            int iph_len = recv_ip->ihl * 4;
            struct icmphdr *recv_icmp = (struct icmphdr *)(recvBuffer + iph_len);

            if (recv_icmp->type == ICMP_ECHOREPLY) {
                std::cout << "TTL " << currentTTL << ": " << respondent_ip
                          << " - Reached destination" << std::endl;
                doneReading = true;     // set not-done-reading to false
                replyReceived = true;   // set reply-not-received to false
            } else if (recv_icmp->type == ICMP_TIME_EXCEEDED && recv_icmp->code == ICMP_EXC_TTL) {
                std::cout << "TTL " << currentTTL << ": " << respondent_ip << std::endl;
                doneReading = true;     // set not-done-reading to false
            }
        }

        if (!doneReading) {
            std::cout << "No response with TTL of " << currentTTL << std::endl;
        }

        // Step 6e: increment TTL by 1
        currentTTL++;
    }

    close(sendFd);
    close(recvFd);
    delete[] sendBuffer;
    delete[] recvBuffer;
    return 0;
}
