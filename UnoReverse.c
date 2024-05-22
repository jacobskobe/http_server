#ifdef _WIN32
    #define _WIN32_WINNT _WIN32_WINNT_WIN7
    #include <winsock2.h> //for all socket programming
    #include <ws2tcpip.h> //for getaddrinfo, inet_pton, inet_ntop
    #include <stdio.h> //for fprintf, perror
    #include <unistd.h> //for close
    #include <stdlib.h> //for exit
    #include <string.h> //for memset
    #include <stdint.h>
    void OSInit( void )
    {
        WSADATA wsaData;
        int WSAError = WSAStartup( MAKEWORD( 2, 0 ), &wsaData ); 
        if( WSAError != 0 )
        {
            fprintf( stderr, "WSAStartup errno = %d\n", WSAError );
            exit( -1 );
        }
    }
    void OSCleanup( void )
    {
        WSACleanup();
    }
    #define perror(string) fprintf( stderr, string ": WSA errno = %d\n", WSAGetLastError() )
#else
    #include <sys/socket.h> //for sockaddr, socket, socket
    #include <sys/types.h> //for size_t
    #include <netdb.h> //for getaddrinfo
    #include <netinet/in.h> //for sockaddr_in
    #include <arpa/inet.h> //for htons, htonl, inet_pton, inet_ntop
    #include <errno.h> //for errno
    #include <stdio.h> //for fprintf, perror
    #include <unistd.h> //for close
    #include <stdlib.h> //for exit
    #include <string.h> //for memset
    #include <stdint.h>
    void OSInit( void ) {}
    void OSCleanup( void ) {}
#endif

int custom_inet_pton(int af, const char *src, void *dst) {
    struct sockaddr_storage ss;
    int size = sizeof(ss);
    char src_copy[INET6_ADDRSTRLEN+1];

    ZeroMemory(&ss, sizeof(ss));
    strncpy(src_copy, src, INET6_ADDRSTRLEN+1);
    src_copy[INET6_ADDRSTRLEN] = 0;

    if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0) {
        switch(af) {
            case AF_INET:
                *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
                return 1;
            case AF_INET6:
                *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
                return 1;
        }
    }
    return 0;
}

const char *custom_inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    struct sockaddr_storage ss;
    unsigned long s = size;

    ZeroMemory(&ss, sizeof(ss));
    ss.ss_family = af;

    switch(af) {
        case AF_INET:
            ((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
            break;
        case AF_INET6:
            ((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
            break;
        default:
            return NULL;
    }
    return (WSAAddressToString((struct sockaddr *)&ss, sizeof(ss), NULL, dst, &s) == 0) ? dst : NULL;
}

#define infinite

void initialize_socket_library();
int create_and_bind_socket();
int accept_client_connection(int internet_socket);
void handle_client_connection(int client_socket);
void log_ip_address(const char *ip);
void cleanup_sockets(int internet_socket, int client_socket);
void perform_geolocation_lookup(const char *client_ip);

char ip_lookup[30];

int main(int argc, char *argv[]) {
    while (1) {
        // Initialization
        initialize_socket_library();
        int internet_socket = create_and_bind_socket();

        // Connection
        printf("Listening on port 22:\n");
        int client_socket = accept_client_connection(internet_socket);

        // Execution
        handle_client_connection(client_socket);

        // Clean up
        cleanup_sockets(internet_socket, client_socket);

        OSCleanup();
    }
    return 0;
}

void initialize_socket_library() {
    OSInit();
}

int create_and_bind_socket() {
    struct addrinfo internet_address_setup;
    struct addrinfo *internet_address_result;
    memset(&internet_address_setup, 0, sizeof internet_address_setup);
    internet_address_setup.ai_family = AF_UNSPEC;
    internet_address_setup.ai_socktype = SOCK_STREAM;
    internet_address_setup.ai_flags = AI_PASSIVE;
    int getaddrinfo_return = getaddrinfo(NULL, "22", &internet_address_setup, &internet_address_result);
    if (getaddrinfo_return != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_return));
        exit(1);
    }

    int internet_socket = -1;
    struct addrinfo *internet_address_result_iterator = internet_address_result;
    while (internet_address_result_iterator != NULL) {
        internet_socket = socket(internet_address_result_iterator->ai_family, internet_address_result_iterator->ai_socktype, internet_address_result_iterator->ai_protocol);
        if (internet_socket == -1) {
            perror("socket error");
        } else {
            int bind_return = bind(internet_socket, internet_address_result_iterator->ai_addr, internet_address_result_iterator->ai_addrlen);
            if (bind_return == -1) {
                perror("bind error");
                close(internet_socket);
            } else {
                int listen_return = listen(internet_socket, 1);
                if (listen_return == -1) {
                    close(internet_socket);
                    perror("listen error");
                } else {
                    break;
                }
            }
        }
        internet_address_result_iterator = internet_address_result_iterator->ai_next;
    }

    freeaddrinfo(internet_address_result);

    if (internet_socket == -1) {
        fprintf(stderr, "socket: No address found\n");
        exit(2);
    }

    return internet_socket;
}

int accept_client_connection(int internet_socket) {
    struct sockaddr_storage client_internet_address;
    socklen_t client_internet_address_length = sizeof client_internet_address;
    int client_socket = accept(internet_socket, (struct sockaddr *)&client_internet_address, &client_internet_address_length);
    if (client_socket == -1) {
        perror("accept error");
        close(internet_socket);
        exit(3);
    }

    char client_ip[INET6_ADDRSTRLEN];
    if (client_internet_address.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&client_internet_address;
        custom_inet_ntop(AF_INET, &(s->sin_addr), client_ip, sizeof client_ip);
    } else if (client_internet_address.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_internet_address;
        custom_inet_ntop(AF_INET6, &(s->sin6_addr), client_ip, sizeof client_ip);
    } else {
        fprintf(stderr, "No valid ip\n");
        close(client_socket);
        close(internet_socket);
        exit(4);
    }

    printf("Connected IP: %s\n", client_ip);
    log_ip_address(client_ip);
    perform_geolocation_lookup(client_ip);

    return client_socket;
}

void handle_client_connection(int client_socket) {
    int number_of_bytes_received = 0;
    char buffer[2000];

    number_of_bytes_received = recv(client_socket, buffer, (sizeof buffer) - 1, 0);
    if (number_of_bytes_received == -1) {
        perror("recveive error");
    } else {
        buffer[number_of_bytes_received] = '\0';
        printf("Received : %s\n", buffer);
    }

    int sendcount = 0;
    #ifdef infinite
    while (1) {
    #else
    for (int i = 0; i < 1000; i++) {
    #endif

        char chartosend[] = "SPAMSPAMSPAM\n";
        int number_of_bytes_send = send(client_socket, chartosend, strlen(chartosend), 0);
        if (number_of_bytes_send == -1) {
            perror("send error");
            break;
        } else {
            sendcount++;
            printf("sent: %s\n", chartosend);
            printf("messages sent: %d", sendcount);
        }
    }

    FILE *logp = fopen("log.txt", "a");
    if (logp == NULL) {
        perror("Error opening log.txt");
        return;
    }
    fprintf(logp, "messages sent: %d\n", sendcount);
    fclose(logp);
}

void log_ip_address(const char *ip) {
    char logbuffer[1000];
    snprintf(logbuffer, sizeof(logbuffer), "<--------------------------------------------------->\nIP attacker: %s\n", ip);
    
    FILE *logp = fopen("log.txt", "a");
    if (logp == NULL) {
        perror("Error opening log.txt");
        return;
    }
    fprintf(logp, "%s", logbuffer);
    fclose(logp);
}

void perform_geolocation_lookup(const char *client_ip) {
    char CLI_buffer[1000];
    snprintf(CLI_buffer, sizeof(CLI_buffer), "curl https://api.ipgeolocation.io/ipgeo?apiKey=47f537ece1fb4b5894fff1395b6777d6&ip=%s", client_ip);
    FILE *fp;
    char IP_LOG_ITEM[2000];

    fp = popen(CLI_buffer, "r");
    if (fp == NULL) {
        perror("Error executing curl command");
        return;
    }

    if (fgets(IP_LOG_ITEM, sizeof(IP_LOG_ITEM) - 1, fp) == NULL) {
        perror("Error reading curl output");
        pclose(fp);
        return;
    }
    pclose(fp);

    // Parse JSON response (improved to handle JSON parsing correctly)
    char *country = strstr(IP_LOG_ITEM, "\"country\":\"");
    char *regionName = strstr(IP_LOG_ITEM, "\"regionName\":\"");
    char *city = strstr(IP_LOG_ITEM, "\"city\":\"");
    char *isp = strstr(IP_LOG_ITEM, "\"isp\":\"");
    char *org = strstr(IP_LOG_ITEM, "\"org\":\"");

    if (country) country += 11; // Skip "\"country\":\""
    if (regionName) regionName += 14; // Skip "\"regionName\":\""
    if (city) city += 8; // Skip "\"city\":\""
    if (isp) isp += 7; // Skip "\"isp\":\""
    if (org) org += 7; // Skip "\"org\":\""

    if (country) strtok(country, "\"");
    if (regionName) strtok(regionName, "\"");
    if (city) strtok(city, "\"");
    if (isp) strtok(isp, "\"");
    if (org) strtok(org, "\"");

    // Write geolocation information to log.txt
    FILE *logp = fopen("log.txt", "a");
    if (logp == NULL) {
        perror("Error opening log.txt");
        return;
    }
    if (country && regionName && city && isp && org) {
        fprintf(logp, "<--------------------------------------------------->\n");
        fprintf(logp, "IP Address: %s\nCountry: %s\nRegion: %s\nTown: %s\nISP: %s\nOrganization: %s\n", client_ip, country, regionName, city, isp, org);
    } else {
        fprintf(logp, "<--------------------------------------------------->\n");
        fprintf(logp, "IP Address: %s\nNo geo: LocalHost\n", client_ip);
    }
    fclose(logp);
}

void cleanup_sockets(int internet_socket, int client_socket) {
    int shutdown_return = shutdown(client_socket, SD_RECEIVE);
    if (shutdown_return == -1) {
        perror("cleanup error");
    }
    close(client_socket);
    close(internet_socket);
}
