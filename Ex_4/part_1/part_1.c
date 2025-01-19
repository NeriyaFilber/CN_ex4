#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>  // Include this header for getopt, optarg, and usleep
#include <getopt.h>
#include "part_1.h"
#include <math.h>

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s -a <address> -t <type> [-c <count>] [-f]\n", argv[0]);
        return 1;
    }
    int count = MAX_REQUESTS;  // Default to 10 pings
    int flood = 0;             // Default is no flood mode
    int type = 0;              // IPv4 or IPv6 (determined later)
    char *address = NULL;      // IP address to ping
    int opt;
    int to_send;

    // Parse command line options using getopt
    while ((opt = getopt(argc, argv, "a:t:c:f")) != -1) {
        switch (opt) {
            case 'a':
                address = optarg;  // IP address to ping
                break;
            case 't':
                type = atoi(optarg);  // Communication type (4 for IPv4, 6 for IPv6)
                if (type != 4 && type != 6) {
                    fprintf(stderr, "Error: Invalid type '%s'. Use 4 for IPv4 or 6 for IPv6.\n", optarg);
                    return 1;
                }
                break;
            case 'c':
                count = atoi(optarg);  // Set the count of pings
                break;
            case 'f':
                flood = 1;  // Enable flood mode
                break;
            default:
                fprintf(stderr, "Usage: %s -a <address> -t <type> [-c <count>] [-f]\n", argv[0]);
                return 1;
        }
    }
    to_send = count;
    if (address == NULL) {
        fprintf(stderr, "Error: IP address is required (use -a flag).\n");
        return 1;
    }
    if (type == 0) {
        fprintf(stderr, "Error: Communication type is required (use -t flag with 4 for IPv4 or 6 for IPv6).\n");
        return 1;
    }
    rtts = (float*)malloc(count * sizeof(float));
    if (rtts == NULL) {
        perror("malloc");
        return 1;
    }

    // Set the socket type based on the communication type (IPv4 or IPv6)
    struct sockaddr_storage destination_address;
    memset(&destination_address, 0, sizeof(destination_address));

    if (type == 4) {
        if (inet_pton(AF_INET, address, &((struct sockaddr_in *)&destination_address)->sin_addr) <= 0) {
            fprintf(stderr, "Error: \"%s\" is not a valid IPv4 address\n", address);
            return 1;
        }
        ((struct sockaddr_in *)&destination_address)->sin_family = AF_INET;
    } else if (type == 6) {
        if (inet_pton(AF_INET6, address, &((struct sockaddr_in6 *)&destination_address)->sin6_addr) <= 0) {
            fprintf(stderr, "Error: \"%s\" is not a valid IPv6 address\n", address);
            return 1;
        }
        ((struct sockaddr_in6 *)&destination_address)->sin6_family = AF_INET6;
    }

    int sock = (type == 4) ?
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

    fprintf(stdout, "PING %s with %d bytes of data:\n", address, payload_size);

    while (count >= 0) {
        memset(buffer, 0, sizeof(buffer));
        if (type == 4) {
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

        if (type == 4) {
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
        sending_pings++;

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

            if (type == 4) {
                struct iphdr *ip_header = (struct iphdr *)buffer;
                struct icmphdr *icmp_header = (struct icmphdr *)(buffer + ip_header->ihl * 4);

                if (icmp_header->type == ICMP_ECHOREPLY) {
                    float pingPongTime = ((float)(end.tv_usec - start.tv_usec) / 1000) + ((end.tv_sec - start.tv_sec) * 1000);
                    fprintf(stdout, "%ld bytes from %s: icmp_seq=%d ttl=%d time=%.2fms\n",
                            (ntohs(ip_header->tot_len) - (ip_header->ihl * 4) - sizeof(struct icmphdr)),
                            inet_ntoa(((struct sockaddr_in *)&source_address)->sin_addr),
                            ntohs(icmp_header->un.echo.sequence),
                            ip_header->ttl, pingPongTime);
                    rtts[rtt_count++] = pingPongTime;
                    recive_pings++;
                    if (seq == to_send)
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
                    rtts[rtt_count++] = pingPongTime;
                    recive_pings++;
                    if (seq == to_send)
                        break;
                }
            }
        }

        // Flood mode (send continuously without delay)
        if (flood) {
            sleep(0);  // No delay between pings in flood mode
        } else {
            sleep(SLEEP_TIME);  // Delay between pings in normal mode
        }
        count--;
    }

    close(sock);
    display_results(rtts, address);
    free(rtts);
    return 0;
}

// Function to calculate the checksum of a packet
unsigned short int calculate_checksum(void *data, unsigned int bytes) {
    unsigned short int *data_pointer = (unsigned short int *)data;
    unsigned int total_sum = 0;

    // Sum all 16-bit words
    while (bytes > 1) {
        total_sum += *data_pointer++;
        bytes -= 2;
    }

    // Add any remaining byte
    if (bytes > 0) {
        total_sum += *((unsigned char *)data_pointer);
    }

    // Fold 32-bit sum to 16 bits
    while (total_sum >> 16) {
        total_sum = (total_sum & 0xFFFF) + (total_sum >> 16);
    }

    return (~((unsigned short int)total_sum));
}

// Function to display the results of the ping
void display_results(float *result, char *addr) {
    if (rtt_count == 0) {
        printf("There is no info to display\n");
        return;
    }
    float min = result[0], max = result[0], sum = result[0];
    for (int i = 1; i < rtt_count; i++) {
        if (result[i] < min) {
            min = result[i];
        }
        if (result[i] > max) {
            max = result[i];
        }
        sum += result[i];
    }
    float avg = sum / rtt_count;
    printf("--- %s ping statistics ---\n%d packets transmitted, %d received, time %.2fms\nrtt min/avg/max/medv = "
           "%.2f/%.2f/%.2f/%.2fms\n",
           addr, sending_pings, recive_pings, sum, min, avg, max, calculate_std(result, rtt_count, avg));
}

// Function to calculate the standard deviation of the round-trip times
double calculate_std(float *arr, int size, float mean) {
    double sum_of_squares = 0.0;

    // Calculate the sum of squared differences from the mean
    for (int i = 0; i < size; i++) {
        sum_of_squares += (arr[i] - mean) * (arr[i] - mean);
    }

    // Calculate standard deviation (sqrt of the variance)
    return sqrt(sum_of_squares / size);  // For population std dev. Use (size-1) for sample std dev.
}