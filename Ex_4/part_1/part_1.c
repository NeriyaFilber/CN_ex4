/*
 * @file ping_ipv6.c
 * @version 1.1
 * @author Roy Simanovich
 * @date 2024-02-13
 * @brief A simple implementation of the ping program using raw sockets, supporting both IPv4 and IPv6.
 */

#include <stdio.h> // Standard input/output definitions
#include <arpa/inet.h> // Definitions for internet operations (inet_pton, inet_ntoa)
#include <netinet/in.h> // Internet address family (AF_INET, AF_INET6)
#include <netinet/ip.h> // Definitions for internet protocol operations (IP header)
#include <netinet/ip_icmp.h> // Definitions for internet control message protocol operations (ICMP header)
#include <netinet/icmp6.h> // Definitions for ICMPv6 header
#include <poll.h> // Poll API for monitoring file descriptors (poll)
#include <errno.h> // Error number definitions (EACCES, EPERM)
#include <string.h> // String manipulation functions (strlen, memset, memcpy)
#include <sys/socket.h> // Definitions for socket operations (socket, sendto, recvfrom)
#include <sys/time.h> // Time types (struct timeval and gettimeofday)
#include <unistd.h> // UNIX standard function definitions (getpid, close, sleep)

#define BUFFER_SIZE 1024
#define TIMEOUT 1000 // Timeout in milliseconds
#define MAX_RETRY 3
#define MAX_REQUESTS 4
#define SLEEP_TIME 1

unsigned short int calculate_checksum(void *data, unsigned int bytes);

/*
 * @brief Main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <destination_ip>\n", argv[0]);
        return 1;
    }

    struct sockaddr_storage destination_address;
    memset(&destination_address, 0, sizeof(destination_address));

    if (inet_pton(AF_INET, argv[1], &((struct sockaddr_in *)&destination_address)->sin_addr) > 0) {
        ((struct sockaddr_in *)&destination_address)->sin_family = AF_INET;
    } else if (inet_pton(AF_INET6, argv[1], &((struct sockaddr_in6 *)&destination_address)->sin6_addr) > 0) {
        ((struct sockaddr_in6 *)&destination_address)->sin6_family = AF_INET6;
    } else {
        fprintf(stderr, "Error: \"%s\" is not a valid IP address\n", argv[1]);
        return 1;
    }

    int sock = (destination_address.ss_family == AF_INET) ?
               socket(AF_INET, SOCK_RAW, IPPROTO_ICMP) :
               socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);

    if (sock < 0) {
        perror("socket(2)");
        if (errno == EACCES || errno == EPERM)
            fprintf(stderr, "You need to run the program with sudo.\n");
        return 1;
    }

    char buffer[BUFFER_SIZE] = {0};
    char *msg = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$^&*()_+{}|:<>?~`-=[]',.";
    int payload_size = strlen(msg) + 1;
    int retries = 0;
    int seq = 0;
    struct pollfd fds[1];
    fds[0].fd = sock;
    fds[0].events = POLLIN;

    fprintf(stdout, "PING %s with %d bytes of data:\n", argv[1], payload_size);

    while (1) {
        memset(buffer, 0, sizeof(buffer));

        if (destination_address.ss_family == AF_INET) {
            struct icmphdr icmp_header;
            icmp_header.type = ICMP_ECHO;
            icmp_header.code = 0;
            icmp_header.un.echo.id = htons(getpid());
            icmp_header.un.echo.sequence = htons(seq++);
            icmp_header.checksum = 0;

            memcpy(buffer, &icmp_header, sizeof(icmp_header));
            memcpy(buffer + sizeof(icmp_header), msg, payload_size);
            ((struct icmphdr *)buffer)->checksum = calculate_checksum(buffer, sizeof(icmp_header) + payload_size);
        } else {
            struct icmp6_hdr icmp6_header;
            icmp6_header.icmp6_type = ICMP6_ECHO_REQUEST;
            icmp6_header.icmp6_code = 0;
            icmp6_header.icmp6_id = htons(getpid());
            icmp6_header.icmp6_seq = htons(seq++);
            icmp6_header.icmp6_cksum = 0;

            memcpy(buffer, &icmp6_header, sizeof(icmp6_header));
            memcpy(buffer + sizeof(icmp6_header), msg, payload_size);
        }

        struct timeval start, end;
        gettimeofday(&start, NULL);

        if (destination_address.ss_family == AF_INET) {
            if (sendto(sock, buffer, (sizeof(struct icmphdr) + payload_size), 0, (struct sockaddr *)&destination_address, sizeof(struct sockaddr_in)) <= 0) {
                perror("sendto(2)");
                close(sock);
                return 1;
            }
        } else {
            if (sendto(sock, buffer, (sizeof(struct icmp6_hdr) + payload_size), 0, (struct sockaddr *)&destination_address, sizeof(struct sockaddr_in6)) <= 0) {
                perror("sendto(2)");
                close(sock);
                return 1;
            }
        }

        int ret = poll(fds, 1, TIMEOUT);
        if (ret == 0) {
            if (++retries == MAX_RETRY) {
                fprintf(stderr, "Request timeout for icmp_seq %d, aborting.\n", seq);
                break;
            }
            fprintf(stderr, "Request timeout for icmp_seq %d, retrying...\n", seq);
            --seq;
            continue;
        } else if (ret < 0) {
            perror("poll(2)");
            close(sock);
            return 1;
        }

        if (fds[0].revents & POLLIN) {
            struct sockaddr_storage source_address;
            socklen_t addr_len = sizeof(source_address);
            memset(buffer, 0, sizeof(buffer));

            if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&source_address, &addr_len) <= 0) {
                perror("recvfrom(2)");
                close(sock);
                return 1;
            }

            retries = 0;
            gettimeofday(&end, NULL);

            if (destination_address.ss_family == AF_INET) {
                struct iphdr *ip_header = (struct iphdr *)buffer;
                struct icmphdr *icmp_header = (struct icmphdr *)(buffer + ip_header->ihl * 4);

                if (icmp_header->type == ICMP_ECHOREPLY) {
                    float pingPongTime = ((float)(end.tv_usec - start.tv_usec) / 1000) + ((end.tv_sec - start.tv_sec) * 1000);
                    fprintf(stdout, "%ld bytes from %s: icmp_seq=%d ttl=%d time=%.2fms\n",
                            (ntohs(ip_header->tot_len) - (ip_header->ihl * 4) - sizeof(struct icmphdr)),
                            inet_ntoa(((struct sockaddr_in *)&source_address)->sin_addr),
                            ntohs(icmp_header->un.echo.sequence),
                            ip_header->ttl, pingPongTime);

                    if (seq == MAX_REQUESTS)
                        break;
                }
            } else {
                struct icmp6_hdr *icmp6_header = (struct icmp6_hdr *)buffer;

                if (icmp6_header->icmp6_type == ICMP6_ECHO_REPLY) {
                    float pingPongTime = ((float)(end.tv_usec - start.tv_usec) / 1000) + ((end.tv_sec - start.tv_sec) * 1000);
                    char addr_str[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&source_address)->sin6_addr, addr_str, sizeof(addr_str));
                    fprintf(stdout, "%d bytes from %s: icmp_seq=%d time=%.2fms\n",
                            payload_size, addr_str, ntohs(icmp6_header->icmp6_seq), pingPongTime);

                    if (seq == MAX_REQUESTS)
                        break;
                }
}}}}
unsigned short int calculate_checksum(void *data, unsigned int bytes) {
    unsigned short int *data_pointer = (unsigned short int *)data;
    unsigned int total_sum = 0;

    // Main summing loop.
    while (bytes > 1)
    {
        total_sum += *data_pointer++; // Some magic pointer arithmetic.
        bytes -= 2;
    }

    // Add left-over byte, if any.
    if (bytes > 0)
        total_sum += *((unsigned char *)data_pointer);

    // Fold 32-bit sum to 16 bits.
    while (total_sum >> 16)
        total_sum = (total_sum & 0xFFFF) + (total_sum >> 16);

    // Return the one's complement of the result.
    return (~((unsigned short int)total_sum));
}