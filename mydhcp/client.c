//
// Created by 尾崎耀一 on 2019-01-09.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

// status
#define S_INIT                  1
#define S_WAIT_OFFER            2
#define S_OFFER_TIMEOUT         3
#define S_WAIT_ACK              4
#define S_ACK_TIMEOUT           5
#define S_IN_USE                6
#define S_EXTEND_ACK_TIMEOUT    7
#define S_WAIT_EXTEND_ACK       8
#define S_EXIT                  9

// event
#define E_SEND_DISCOVER         1
#define E_RECV_OK_OFFER         2
#define E_RECV_OK_ACK           3
#define E_HALFTTL_PASSED        4
#define E_RECV_NG_OFFER         5
#define E_RECV_TIMEOUT          6
#define E_RECV_NG_ACK           7
#define E_RECV_SIGHUP           8
#define E_RECV_UNKNOWN_MSG      9
#define E_RECV_UNEXPECTED_MSG   10

// message
struct message {
    uint8_t         Type;
    uint8_t         Code;
    uint16_t        TTL;
    in_addr_t       IP;
    struct in_addr  Netmask;
};

// port
#define CLIENT_PORT 51230
#define SERVER_PORT 51230

// message type
#define T_DISCOVER  0
#define T_OFFER     1
#define T_REQUEST   2
#define T_ACK       3
#define T_RELEASE   4

// message code
#define C_OK                0
#define C_OFFER_ERROR       1
#define C_REQUEST_ALLOC     2
#define C_REQUEST_EXTEND    3
#define C_ACK_ERROR         4

int wait_event();

void signal_handler(int);

void send_discover(int, int);               // S_INIT -> S_WAIT_OFFER
void send_alloc_request(int, int);          // S_WAIT_OFFER | S_OFFER_TIMEOUT -> S_WAIT_ACK
void send_extend_request(int, int);         // S_IN_USE -> S_WAIT_EXTEND_ACK
void re_send_discover(int, int);            // S_WAIT_OFFER -> S_OFFER_TIMEOUT
void re_send_alloc_request(int, int);       // S_WAIT_ACK -> S_ACK_TIMEOUT
void re_send_extend_request(int, int);      // S_WAIT_EXTEND_ACK -> S_EXTEND_ACK_TIMEOUT
void send_release(int, int);                // S_IN_USE -> S_EXIT
void exit_client(int, int);                 // S_WAIT_OFFER | S_OFFER_TIMEOUT | S_WAIT_ACK | S_ACK_TIMEOUT | S_EXTEND_ACK_TIMEOUT | S_WAIT_EXTEND_ACK -> S_EXIT
void get_ip_addr(int, int);                 // S_WAIT_ACK | S_ACK_TIMEOUT | S_WAIT_EXTEND_ACK | S_EXTEND_ACK_TIMEOUT -> S_IN_USE

struct proctable {
    int status;
    int event;
    void (*func)(int, int); // void func(int, int)へのポインタ
} ptab[] = {
        {S_WAIT_OFFER, E_RECV_OK_OFFER, send_alloc_request},
        {S_WAIT_OFFER, E_RECV_TIMEOUT, re_send_discover},
        {S_WAIT_OFFER, E_RECV_NG_OFFER, exit_client},
        {S_WAIT_OFFER, E_RECV_UNKNOWN_MSG, exit_client},
        {S_WAIT_OFFER, E_RECV_UNEXPECTED_MSG, exit_client},
        {S_OFFER_TIMEOUT, E_RECV_OK_OFFER, send_alloc_request},
        {S_OFFER_TIMEOUT, E_RECV_TIMEOUT, exit_client},
        {S_OFFER_TIMEOUT, E_RECV_NG_OFFER, exit_client},
        {S_OFFER_TIMEOUT, E_RECV_UNKNOWN_MSG, exit_client},
        {S_OFFER_TIMEOUT, E_RECV_UNEXPECTED_MSG, exit_client},
        {S_WAIT_ACK, E_RECV_OK_ACK, get_ip_addr},
        {S_WAIT_ACK, E_RECV_TIMEOUT, re_send_alloc_request},
        {S_WAIT_ACK, E_RECV_NG_ACK, exit_client},
        {S_WAIT_ACK, E_RECV_UNKNOWN_MSG, exit_client},
        {S_WAIT_ACK, E_RECV_UNEXPECTED_MSG, exit_client},
        {S_ACK_TIMEOUT, E_RECV_OK_ACK, get_ip_addr},
        {S_ACK_TIMEOUT, E_RECV_TIMEOUT, exit_client},
        {S_ACK_TIMEOUT, E_RECV_NG_ACK, exit_client},
        {S_ACK_TIMEOUT, E_RECV_UNKNOWN_MSG, exit_client},
        {S_ACK_TIMEOUT, E_RECV_UNEXPECTED_MSG, exit_client},
        {S_IN_USE, E_HALFTTL_PASSED, send_extend_request},
        {S_IN_USE, E_RECV_OK_ACK, get_ip_addr},
        {S_IN_USE, E_RECV_SIGHUP, send_release},
        {S_EXTEND_ACK_TIMEOUT, E_RECV_TIMEOUT, exit_client},
        {S_EXTEND_ACK_TIMEOUT, E_RECV_NG_ACK, exit_client},
        {S_EXTEND_ACK_TIMEOUT, E_RECV_UNKNOWN_MSG, exit_client},
        {S_EXTEND_ACK_TIMEOUT, E_RECV_UNEXPECTED_MSG, exit_client},
        {S_EXTEND_ACK_TIMEOUT, E_RECV_OK_ACK, get_ip_addr},
        {S_WAIT_EXTEND_ACK, E_RECV_OK_ACK, get_ip_addr},
        {S_WAIT_EXTEND_ACK, E_RECV_TIMEOUT, re_send_extend_request},
        {S_WAIT_EXTEND_ACK, E_RECV_NG_ACK, exit_client},
        {S_WAIT_EXTEND_ACK, E_RECV_UNKNOWN_MSG, exit_client},
        {S_WAIT_EXTEND_ACK, E_RECV_UNEXPECTED_MSG, exit_client},
        {0, 0, NULL}
};

int status  = S_INIT;

int client_socket;
struct sockaddr_in server_addr;

uint16_t            offered_ttl;
struct in_addr      offered_ip;
uint32_t            offered_netmask;

fd_set readfds;
struct timeval tv;

int got_signal;

int set_timer();

int
main(int argc, char *argv[]) {

    if (SIG_ERR == signal(SIGHUP, signal_handler)) {
        fprintf(stderr, "ERROR: failed to set signal handler for SIGHUP\n");
        exit(EXIT_FAILURE);
    }
    if (SIG_ERR == signal(SIGALRM, signal_handler)) {
        fprintf(stderr, "ERROR: failed to set signal handler for SIGALRM\n");
        exit(EXIT_FAILURE);
    }

    // ./mydhcpc server.ip.addr
    if (argc != 2) {
        fprintf(stderr, "%s: Usage: %s <SERVER IP ADDR>\n", argv[0], argv[0]);
        return -1;
    }

    // create socket
    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("ERROR: client_socket = socket(AF_INET, SOCK_DGRAM, 0);");
        exit(1);
    }

    // set server ip address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    // FOR DEBUG
    fprintf(stderr, "[DEBUG] SERVER\n");
    fprintf(stderr, "\tip address: %s\n", inet_ntoa(server_addr.sin_addr));
    fprintf(stderr, "\tport: %d\n\n", ntohs(server_addr.sin_port));

    struct proctable *pt;
    int event = -1;

    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    for(;;) {
        if (status == S_INIT) {
            send_discover(S_INIT, E_SEND_DISCOVER);
        }
        event = wait_event();
        for (pt = ptab; pt->status; pt++) {
            if (pt -> status == status && pt-> event == event) {
                (*pt->func)(status, event);
                break;
            }
        }
        if (pt->status == 0) {
            fprintf(stderr, "ERROR @ pt->status == 0");
            exit(EXIT_FAILURE);
        }
    }
}

int
wait_event()
{
    int ret;
    if (status == S_IN_USE) {
        pause(); // シグナルハンドラから返ってくるまでここで停止
        goto GOT_SIGNAL;
    } else {
        // int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
        ret = select(client_socket+1, &readfds, NULL, NULL, &tv);

        // signal
        if (ret < 0) {
            if (errno == EINTR) {
                goto GOT_SIGNAL;
            } else {
                fprintf(stderr, "ERROR: select @ wait_event\n");
                exit(EXIT_FAILURE);
            }
        }
        // timeout
        if (ret == 0) {
            return E_RECV_TIMEOUT;
        } else {
            struct message buf;
            memset(&buf, '\0', sizeof(buf));
            if (recvfrom(
                    client_socket,
                    &buf,
                    sizeof(buf),
                    0,
                    NULL,
                    NULL)
                    < 0) {
                perror("recvfrom @ wait_event");
                exit(EXIT_FAILURE);
            }

            struct in_addr tmp;
            tmp.s_addr = buf.IP;

            fprintf(stderr, "--[(<-)RECV MESSAGE]----------------------------------\n");
            fprintf(stderr, "|\tType: %d\n", buf.Type);
            fprintf(stderr, "|\t\tT_DISCOVER          0\n"
                            "|\t\tT_OFFER             1\n"
                            "|\t\tT_REQUEST           2\n"
                            "|\t\tT_ACK               3\n"
                            "|\t\tT_RELEASE           4\n");
            fprintf(stderr, "|\tCode: %d\n", buf.Code);
            fprintf(stderr, "|\t\tC_OK                0\n"
                            "|\t\tC_OFFER_ERROR       1\n"
                            "|\t\tC_REQUEST_ALLOC     2\n"
                            "|\t\tC_REQUEST_EXTEND    3\n"
                            "|\t\tC_ACK_ERROR         4\n");
            fprintf(stderr, "|\tTTL: %d\n", (uint16_t)(rand()%(ntohs(buf.TTL)-1+1)+1));
            fprintf(stderr, "|\tIP: %s\n", inet_ntoa(tmp));
            fprintf(stderr, "|\tNetmask: %d\n", ntohs(buf.TTL));
            fprintf(stderr, "--------------------------------------------------\n");

            switch (buf.Type) {
                case T_OFFER:
                    if (buf.Code == C_OK) {
                        offered_ip.s_addr = buf.IP;
                        offered_netmask = buf.Netmask.s_addr;
                        offered_ttl = buf.TTL;
                        return E_RECV_OK_OFFER;
                    } else if (buf.Code == C_OFFER_ERROR) {
                        return E_RECV_NG_OFFER;
                    } else {
                        fprintf(stderr, "ERROR: recv unexpected code.\n");
                        return E_RECV_UNEXPECTED_MSG;
                    }
                case T_ACK:
                    if (buf.Code == C_OK) {
                        offered_ip.s_addr = buf.IP;
                        offered_netmask = buf.Netmask.s_addr;
                        offered_ttl = buf.TTL;
                        return E_RECV_OK_ACK;
                    } else if (buf.Code == C_ACK_ERROR) {
                        return E_RECV_NG_ACK;
                    } else {
                        fprintf(stderr, "ERROR: recv unexpected code.\n");
                        return E_RECV_UNEXPECTED_MSG;
                    }
                default:
                    fprintf(stderr, "ERROR: recv unknown code.\n");
                    return E_RECV_UNKNOWN_MSG;
            }
        }

    }
    GOT_SIGNAL:
    if (got_signal == SIGHUP) {
        return E_RECV_SIGHUP;
    } else if (got_signal == SIGALRM) {
        return E_HALFTTL_PASSED;
    } else { // never reach
        fprintf(stderr, "ERROR: got unexpected signal.\n");
        exit(EXIT_FAILURE);
    }
}

void
send_discover(int st, int ev)
{
    struct message discover_message;
    memset(&discover_message, '\0', sizeof(discover_message));
    discover_message.Type = T_DISCOVER;
    if (sendto(
            client_socket,
            &discover_message,
            sizeof(discover_message),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
            < 0) {
        perror("sendto @ send_discover");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "STATUS: S_INIT(%d) --[E_SEND_DISCOVER(%d)]--> S_WAIT_OFFER(%d)\n", st, ev, S_WAIT_OFFER);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_DISCOVER\n");
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    status = S_WAIT_OFFER;
}

void
send_alloc_request(int st, int ev)
{
    struct message alloc_request;
    memset(&alloc_request, '\0', sizeof(alloc_request));
    alloc_request.Type = T_REQUEST;
    alloc_request.Code = C_REQUEST_ALLOC;
    srand((unsigned)time(NULL));

    // alloc_request.TTL = htons((uint16_t)(rand()%(ntohs(offered_ttl)-1+1)+1));
    alloc_request.TTL = offered_ttl;

    // debug
    fprintf(stderr, "OFFERED TTL: %d\n", ntohs(offered_ttl));

    alloc_request.IP = offered_ip.s_addr;
    alloc_request.Netmask.s_addr = offered_netmask;
    if (sendto(
            client_socket,
            &alloc_request,
            sizeof(alloc_request),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ send_alloc_request");
        exit(EXIT_FAILURE);
    }

    status = S_WAIT_ACK;
    switch (st) {
        case S_WAIT_OFFER:
            fprintf(stderr, "STATUS: S_WAIT_OFFER(%d) --[E_RECV_OK_OFFER(%d) + send_alloc_request()]--> S_WAIT_ACK(%d)\n", st, ev, S_WAIT_ACK);
            break;
        case S_OFFER_TIMEOUT:
            fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_OK_OFFER(%d) + send_alloc_request()]--> S_WAIT_ACK(%d)\n", st, ev, S_WAIT_ACK);
            break;
        default:
            fprintf(stderr, "Error: Unexpected status change.");
            exit(EXIT_FAILURE);
    }

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_REQUEST\n");
    fprintf(stderr, "\t|\tCode: C_REQUEST_ALLOC\n");
    fprintf(stderr, "\t|\tTTL: %d\n", (uint16_t)(rand()%(ntohs(offered_ttl)-1+1)+1));
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(offered_netmask));
    fprintf(stderr, "\t--------------------------------------------------\n\n");
}

void
send_extend_request(int st, int ev)
{
    struct message extend_request;
    memset(&extend_request, '\0', sizeof(extend_request));
    extend_request.Type = T_REQUEST;
    extend_request.Code = C_REQUEST_EXTEND;
    srand((unsigned)time(NULL));
    extend_request.TTL = (uint16_t)(rand()%(offered_ttl-1+1)+1);
    extend_request.IP = offered_ip.s_addr;
    extend_request.Netmask.s_addr = offered_netmask;
    if (sendto(
            client_socket,
            &extend_request,
            sizeof(extend_request),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ send_extend_request");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_HALFTTL_PASSED(%d) + send_extend_request()]--> S_WAIT_EXTEND_ACK(%d)\n", st, ev, S_WAIT_EXTEND_ACK);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_REQUEST\n");
    fprintf(stderr, "\t|\tCode: C_REQUEST_EXTEND\n");
    fprintf(stderr, "\t|\tTTL: %d\n", (uint16_t)(rand()%(ntohs(offered_ttl)-1+1)+1));
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(offered_netmask));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    status = S_WAIT_EXTEND_ACK;
}

void
re_send_discover(int st, int ev)
{
    struct message re_discover_message;
    memset(&re_discover_message, '\0', sizeof(re_discover_message));
    re_discover_message.Type = T_DISCOVER;
    if (sendto(
            client_socket,
            &re_discover_message,
            sizeof(re_discover_message),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ re_send_discover");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "STATUS: S_WAIT_OFFER(%d) --[E_RECV_TIMEOUT(%d) + re_send_discover]--> S_OFFER_TIMEOUT(%d)\n", st, ev, S_OFFER_TIMEOUT);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_DISCOVER\n");
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    status = S_OFFER_TIMEOUT;
}

void
re_send_alloc_request(int st, int ev)
{
    struct message re_alloc_request;
    memset(&re_alloc_request, '\0', sizeof(re_alloc_request));
    re_alloc_request.Type = T_REQUEST;
    re_alloc_request.Code = C_REQUEST_ALLOC;
    srand((unsigned)time(NULL));
    re_alloc_request.TTL = (uint16_t)(rand()%(offered_ttl-1+1)+1);
    re_alloc_request.IP = offered_ip.s_addr;
    re_alloc_request.Netmask.s_addr = offered_netmask;
    if (sendto(
            client_socket,
            &re_alloc_request,
            sizeof(re_alloc_request),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ re_send_alloc_request");
        exit(EXIT_FAILURE);
    }
    status = S_ACK_TIMEOUT;

    fprintf(stderr, "STATUS: S_WAIT_ACK(%d) --[E_RECV_TIMEOUT(%d) + re_send_alloc_request()]--> S_ACK_TIMEOUT(%d)\n", st, ev, S_ACK_TIMEOUT);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_REQUEST\n");
    fprintf(stderr, "\t|\tCode: C_REQUEST_ALLOC\n");
    fprintf(stderr, "\t|\tTTL: %d\n", (uint16_t)(rand()%(ntohs(offered_ttl)-1+1)+1));
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(offered_netmask));
    fprintf(stderr, "\t--------------------------------------------------\n\n");
}

void
re_send_extend_request(int st, int ev)
{
    struct message re_extend_request;
    memset(&re_extend_request, '\0', sizeof(re_extend_request));
    re_extend_request.Type = T_REQUEST;
    re_extend_request.Code = C_REQUEST_EXTEND;
    srand((unsigned)time(NULL));
    re_extend_request.TTL = (uint16_t)(rand()%(offered_ttl-1+1)+1);
    re_extend_request.IP = offered_ip.s_addr;
    re_extend_request.Netmask.s_addr = offered_netmask;
    if (sendto(
            client_socket,
            &re_extend_request,
            sizeof(re_extend_request),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ re_send_extend_request");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "STATUS: S_WAIT_EXTEND_ACK(%d) --[E_RECT_TIMEOUT(%d) + re_send_extend_request()]--> S_EXTEND_ACK_TIMEOUT(%d)\n", st, ev, S_EXTEND_ACK_TIMEOUT);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_REQUEST\n");
    fprintf(stderr, "\t|\tCode: C_REQUEST_EXTEND\n");
    fprintf(stderr, "\t|\tTTL: %d\n", (uint16_t)(rand()%(ntohs(offered_ttl)-1+1)+1));
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(offered_netmask));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    status = S_EXTEND_ACK_TIMEOUT;
}

void
send_release(int st, int ev)
{
    struct message release_message;
    memset(&release_message, '\0', sizeof(release_message));
    release_message.Type = T_RELEASE;
    release_message.IP = offered_ip.s_addr;
    if (sendto(
            client_socket,
            &release_message,
            sizeof(release_message),
            0,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        < 0) {
        perror("sendto @ send_release");
        exit(EXIT_FAILURE);
    }
    status = S_EXIT;

    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_RECT_SIGHUP(%d) + send_release()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_RELEASE\n");
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "\t--------------------------------------------------\n\n");
}

void
exit_client(int st, int ev)
{
    close(client_socket);
    switch (st) {
        case S_WAIT_OFFER:
            switch (ev) {
                case E_RECV_NG_OFFER:
                    fprintf(stderr, "STATUS: S_WAIT_OFFER(%d) --[E_RECV_NG_OFFER(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        case S_OFFER_TIMEOUT:
            switch (ev) {
                case E_RECV_TIMEOUT:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_TIMEOUT(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_NG_OFFER:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_NG_OFFER(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_OFFER_TIMEOUT(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        case S_WAIT_ACK:
            switch (ev) {
                case E_RECV_NG_ACK:
                    fprintf(stderr, "STATUS: S_WAIT_ACK(%d) --[E_RECV_NG_ACK(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_WAIT_ACK(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_WAIT_ACK(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        case S_ACK_TIMEOUT:
            switch (ev) {
                case E_RECV_TIMEOUT:
                    fprintf(stderr, "STATUS: S_ACK_TIMEOUT(%d) --[E_RECV_TIMEOUT(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_NG_ACK:
                    fprintf(stderr, "STATUS: S_ACK_TIMEOUT(%d) --[E_RECV_NG_ACK(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_ACK_TIMEOUT(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_ACK_TIMEOUT(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        case S_EXTEND_ACK_TIMEOUT:
            switch (ev) {
                case E_RECV_TIMEOUT:
                    fprintf(stderr, "STATUS: S_EXTEND_ACK_TIMEOUT(%d) --[E_RECV_TIMEOUT(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_NG_ACK:
                    fprintf(stderr, "STATUS: S_EXTEND_ACK_TIMEOUT(%d) --[E_RECV_NG_ACK(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_EXTEND_ACK_TIMEOUT(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_EXTEND_ACK_TIMEOUT(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        case S_WAIT_EXTEND_ACK:
            switch (ev) {
                case E_RECV_NG_ACK:
                    fprintf(stderr, "STATUS: S_WAIT_EXTEND_ACK(%d) --[E_RECV_NG_ACK(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNKNOWN_MSG:
                    fprintf(stderr, "STATUS: S_WAIT_EXTEND_ACK(%d) --[E_RECV_UNKNOWN_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                case E_RECV_UNEXPECTED_MSG:
                    fprintf(stderr, "STATUS: S_WAIT_EXTEND_ACK(%d) --[E_RECV_UNEXPECTED_MSG(%d) + exit_client()]--> S_EXIT(%d)\n", st, ev, S_EXIT);
                    break;
                default:
                    fprintf(stderr, "Error: Unexpected status change.");
                    exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Error: Unexpected status change.");
            exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

void
get_ip_addr(int st, int ev)
{
    if (set_timer() < 0) {
        fprintf(stderr, "ERROR: failed to set timer\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "********** ALLOCATED IP ADDRESS **********\n");
    fprintf(stderr, "*\tIP:\t\t%s\n", inet_ntoa(offered_ip));
    fprintf(stderr, "*\tNETMASK:\t%d\n", ntohs(offered_netmask));
    fprintf(stderr, "*\tTTL:\t\t%d\n", offered_ttl);
    fprintf(stderr, "******************************************\n\n");
    status = S_IN_USE;
}

void
signal_handler(int sig)
{
    fprintf(stderr, "[DEBUG] Got signal(%d)!\n", sig);
    if (sig == SIGHUP) {
        got_signal = SIGHUP;
    } else if (sig == SIGALRM) {
        got_signal =  SIGALRM;
    }
}

int
set_timer()
{
    int rc = 0;
    struct itimerval val;
    val.it_interval.tv_sec  = offered_ttl/2;
    val.it_interval.tv_usec = 0;
    val.it_value = val.it_interval;
    rc = setitimer(ITIMER_REAL, &val, NULL);
    if(rc < 0){
        return -1;
    }
    return 0;
}
