//
// Created by 尾崎耀一 on 2019-01-12.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "mydhcp.h"

/* ---------------------- proc functions ---------------------- */
void send_discover_proc(int, struct sockaddr *);
void send_alloc_request_proc(int, struct sockaddr *, struct message *);
void recv_ok_ack_proc(int, struct sockaddr *, struct message *);
void send_ext_request_proc(int, struct sockaddr *, struct message *);
void resend_discover_proc(int, struct sockaddr *, struct message *);
void resend_alloc_request_proc(int, struct sockaddr *, struct message *);
void resend_ext_request_proc(int, struct sockaddr *, struct message *);
void recv_NG_offer_proc(int, struct sockaddr *, struct message *);
void recv_unknown_message_proc(int, struct sockaddr *, struct message *);
void recv_unexpected_message_proc(int, struct sockaddr *, struct message *);
void timed_out_proc(int, struct sockaddr *, struct message *);
void recv_NG_ack_proc(int, struct sockaddr *, struct message *);
void send_release_proc(int, struct sockaddr *, struct message *);

/* ---------------------- helper functions ---------------------- */
void send_discover(int, struct sockaddr *);
void send_alloc_request(int, struct sockaddr *);
void send_ext_request(int, struct sockaddr *);
void send_request(int, struct sockaddr *, uint8_t);

void SIGALRM_handler();
void SIGHUP_handler();

int wait_event(int, struct sockaddr_in *, struct message *, int);
void dump_message(struct message *);

struct proc_table {
    int state;
    int event;
    void (*func)(int, struct sockaddr *, struct message *);
} table[] = {
        {C_STATE_WAIT_OFFER,        C_EVENT_RECV_OFFER_OK,      send_alloc_request_proc     }, // 2
        {C_STATE_WAIT_ACK,          C_EVENT_RECV_ACK_OK,        recv_ok_ack_proc            }, // 3
        {C_STATE_INUSE,             C_EVENT_HALF_TTL,           send_ext_request_proc       }, // 4
        {C_STATE_WAIT_EXT_ACK,      C_EVENT_RECV_ACK_OK,        recv_ok_ack_proc            }, // 5
        {C_STATE_WAIT_OFFER,        C_EVENT_TIMEOUT,            resend_discover_proc        }, // 6
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_RECV_OFFER_OK,      resend_alloc_request_proc   }, // 7
        {C_STATE_WAIT_ACK,          C_EVENT_TIMEOUT,            resend_alloc_request_proc   }, // 8
        {C_STATE_ACK_TIMEOUT,       C_EVENT_RECV_ACK_OK,        recv_ok_ack_proc            }, // 9
        {C_STATE_EXT_ACK_TIMEOUT,   C_EVENT_RECV_ACK_OK,        recv_ok_ack_proc            }, // 10
        {C_STATE_WAIT_EXT_ACK,      C_EVENT_TIMEOUT,            resend_ext_request_proc     }, // 11
        {C_STATE_WAIT_OFFER,        C_EVENT_RECV_OFFER_NOIP,    recv_NG_offer_proc          }, // 12
        {C_STATE_WAIT_OFFER,        C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 12
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_TIMEOUT,            timed_out_proc              }, // 13
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_RECV_OFFER_NOIP,    recv_NG_offer_proc          }, // 13
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 13
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_RECV_ACK_NG,        recv_NG_ack_proc            }, // 14
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 14
        {C_STATE_ACK_TIMEOUT,       C_EVENT_TIMEOUT,            timed_out_proc              }, // 15
        {C_STATE_ACK_TIMEOUT,       C_EVENT_RECV_ACK_NG,        recv_NG_ack_proc            }, // 15
        {C_STATE_ACK_TIMEOUT,       C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 15
        {C_STATE_EXT_ACK_TIMEOUT,   C_EVENT_TIMEOUT,            timed_out_proc              }, // 16
        {C_STATE_EXT_ACK_TIMEOUT,   C_EVENT_RECV_ACK_NG,        recv_NG_ack_proc            }, // 16
        {C_STATE_EXT_ACK_TIMEOUT,   C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 16
        {C_STATE_WAIT_EXT_ACK,      C_EVENT_RECV_ACK_NG,        recv_NG_ack_proc            }, // 17
        {C_STATE_WAIT_EXT_ACK,      C_EVENT_RECV_UNKNOWN,       recv_unknown_message_proc   }, // 17
        {C_STATE_INUSE,             C_EVENT_RECV_SIGHUP,        send_release_proc           }, // 18
        {C_STATE_WAIT_OFFER,        C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 12
        {C_STATE_OFFER_TIMEOUT,     C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 13
        {C_STATE_WAIT_ACK,          C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 14
        {C_STATE_ACK_TIMEOUT,       C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 15
        {C_STATE_EXT_ACK_TIMEOUT,   C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 16
        {C_STATE_WAIT_EXT_ACK,      C_EVENT_DEFAULT,            recv_unexpected_message_proc}, // 17
        {0,                         0,                          NULL}
};

int state;                  // client's state
int HUP_flag;
int ALRM_flag;
struct in_addr myip;        // allocated IP address, network byte order
struct in_addr mynetmask;   // allocated netmask, network byte order
uint16_t allocated_ttl;     // allocated TTL, network byte order

struct message received_message;

int
main(int argc, char *argv[])
{
    struct sockaddr_in server_socket_address; // server socket address
    struct sigaction ALRM_act, HUP_act;
    struct proc_table *pointer_to_table;
    int s, event;
    memset(&received_message, 0, sizeof(received_message));

    if (argc != 2) {
        fprintf(stderr, "Usage: mydhcpc <server.IP.address>\n");
        exit(EXIT_FAILURE);
    }

    // prepare server's socket
    server_socket_address.sin_family = AF_INET;
    server_socket_address.sin_port = htons(MYDHCP_PORT);
    inet_aton(argv[0], &server_socket_address.sin_addr);
    fprintf(stderr, "\n## [DEBUG] server ###############\n");
    fprintf(stderr, "#\tIP:\t%s\t\t#\n", inet_ntoa(server_socket_address.sin_addr));
    fprintf(stderr, "#\tPORT:\t%d\t\t#\n", ntohs(server_socket_address.sin_port));
    fprintf(stderr, "#################################\n\n");

    // set signal handlers
    ALRM_flag = 0;
    ALRM_act.sa_handler = SIGALRM_handler;
    sigaction(SIGALRM, &ALRM_act, NULL);
    HUP_flag = 0;
    HUP_act.sa_handler = SIGHUP_handler;
    sigaction(SIGHUP, &HUP_act, NULL);

    // set initial state
    state = C_STATE_INIT;

    // create client's socket
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket @ creating client's socket");
        exit(EXIT_FAILURE);
    }

    // main loop
    for (;;) {

        if (state == C_STATE_INIT) {
            send_discover_proc(s, (struct sockaddr *)&server_socket_address);
            continue;
        }

        if ((event = wait_event(s, &server_socket_address, &received_message, sizeof(received_message))) < 0) {
            fprintf(stderr, "ERROR: illegal event\n");
            exit(EXIT_FAILURE);
        }

        for (pointer_to_table = table; pointer_to_table->state; pointer_to_table++) {
            if (pointer_to_table->state == state && pointer_to_table->event == event) {
                (*pointer_to_table->func)(s, (struct sockaddr *)&server_socket_address, &received_message);
                break;
            }
            if (pointer_to_table->state == state && pointer_to_table->event == C_EVENT_DEFAULT) {
                (*pointer_to_table->func)(s, (struct sockaddr *)&server_socket_address, &received_message);
                break;
            }
        }

        if (pointer_to_table->state == 0) {
            fprintf(stderr, "ERROR: no such state: state = %s, event = %s\n",
                    CLIENT_STATES_NAME[state],
                    CLIENT_EVENT_NAME[event]);
            exit(EXIT_FAILURE);
        }
    }
}

int
wait_event(int socket, struct sockaddr_in *server_socket, struct message *buf, int size)
{
    struct sockaddr_in _server_socket_address;
    struct timeval timeout, t1, t2;
    fd_set fdset;
    struct message *_received_message;
    int ret, _received_message_size;
    socklen_t _server_socket_len;

    if (state == C_STATE_INUSE) {
        pause(); // wait for signal
        if (HUP_flag > 0) {
            HUP_flag = 0;
            return C_EVENT_RECV_SIGHUP;
        }
        if (ALRM_flag > 0) {
            ALRM_flag = 0;
            return C_EVENT_HALF_TTL;
        }
        fprintf(stderr, "ERROR: caught unexpected signal, so exit.");
        exit(EXIT_FAILURE);
    }
    timeout.tv_sec = RECV_TIMEOUT;
    timeout.tv_usec = 0;
    gettimeofday(&t1, NULL);

  AGAIN:
    FD_ZERO(&fdset);
    FD_SET(socket, &fdset);
    if ((ret = select(socket+1, &fdset, NULL, NULL, &timeout)) < 0) {
        if (errno != EINTR) {
            perror("select @ wait_event");
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "[DEBUG] Caught signal!\n");
        if (HUP_flag > 0) {
            HUP_flag = 0;
            return C_EVENT_RECV_SIGHUP;
        } else if (ALRM_flag > 0) {
            ALRM_flag = 0;
            return C_EVENT_HALF_TTL;
        }
        goto AGAIN;
    }

    if (ret == 0) { // timed out
        fprintf(stderr, "\n######################################################\n");
        fprintf(stderr, "#                     timed out.                     #\n");
        fprintf(stderr, "######################################################\n\n");
        return C_EVENT_TIMEOUT;
    }

    if (FD_ISSET(socket, &fdset) == 0) {
        gettimeofday(&t2, NULL);
        timeout.tv_sec -= (t2.tv_sec - t1.tv_sec);
        fprintf(stderr, "-- select again: %ld sec remaining --\n", timeout.tv_sec);
        goto AGAIN;
    }

    _server_socket_len = sizeof(struct sockaddr_in);
    if ((_received_message_size = (int) recvfrom(
                socket,
                buf,
                (size_t) size,
                0,
                (struct sockaddr *)&_server_socket_address,
                &_server_socket_len)) < 0) {
        perror("recvfrom @ wait_event");
        exit(EXIT_FAILURE);
    }

    _received_message = buf;

    if (_received_message_size != sizeof(struct message)) {
        fprintf(stderr, "ERROR: received illegal size message\n");
        return C_EVENT_DEFAULT;
    }

//    if (_server_socket_address.sin_addr.s_addr != server_socket->sin_addr.s_addr) {
//        fprintf(stderr, "ERROR: received from illegal server: want=%s, got=%s\n",
//                inet_ntoa(server_socket->sin_addr),
//                inet_ntoa(_server_socket_address.sin_addr));
//        return C_EVENT_DEFAULT;
//    }

    switch (_received_message->type) {
        case TYPE_OFFER:
            if (_received_message->code == CODE_SUCCUESS) {
                allocated_ttl = buf->ttl;
                dump_message(_received_message);
                return C_EVENT_RECV_OFFER_OK;
            }
            if (_received_message->code == CODE_NOIPADDR) {
                dump_message(_received_message);
                return C_EVENT_RECV_OFFER_NOIP;
            }
            break;
        case TYPE_ACK:
            if (_received_message->code == CODE_SUCCUESS) {
                dump_message(_received_message);
                return C_EVENT_RECV_ACK_OK;
            }
            if (_received_message->code == CODE_PARMERR) {
                dump_message(_received_message);
                return C_EVENT_RECV_ACK_NG;
            }
            break;
        default:
            fprintf(stderr, "ERROR: unknown message type: %d\n",
                    _received_message->type);
            return C_EVENT_RECV_UNKNOWN;
    }
    fprintf(stderr, "ERROR: unknown message code: %d\n",
            _received_message->code);
    return C_EVENT_RECV_UNKNOWN;
}

void
send_discover_proc(int socket, struct sockaddr *server_socket)
{
    int before = state;

    send_discover(socket, server_socket);   // send DISCOVER message

    state = C_STATE_WAIT_OFFER;             // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
}

void
send_alloc_request_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;

    myip.s_addr = message->ipaddr.s_addr;
    mynetmask.s_addr = message->netmask.s_addr;
    allocated_ttl = ntohs(message->ttl);

    send_alloc_request(socket, server_socket);  // send REQUEST(ALLOC)

    state = C_STATE_WAIT_ACK;                   // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);

}

void
recv_ok_ack_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;
    struct timeval tv;

    // dump_message(&received_message);

    state = C_STATE_INUSE;   // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);

    gettimeofday(&tv, NULL);
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
    fprintf(stderr, "%s\n", ctime(&tv.tv_sec));
    alarm(allocated_ttl/(unsigned int)2); // TTL timer starts
}

void
send_ext_request_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;

    send_ext_request(socket, server_socket);    // send REQUEST(EXTENSION)

    state = C_STATE_WAIT_EXT_ACK;               // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
}

void
resend_discover_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;

    send_discover(socket, server_socket);   // resend DISCOVER message

    state = C_STATE_OFFER_TIMEOUT;          // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
}

void
resend_alloc_request_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;

    send_alloc_request(socket, server_socket);  // resend REQUEST(ALLOC)

    state = C_STATE_ACK_TIMEOUT;                // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
}

void
resend_ext_request_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    int before = state;

    send_ext_request(socket, server_socket);    // resend REQUEST(EXTENSION)

    state = C_STATE_EXT_ACK_TIMEOUT;            // state transition
    fprintf(stderr, "%s --> %s\n",
            CLIENT_EVENT_NAME[before],
            CLIENT_EVENT_NAME[state]);
}
void
recv_NG_offer_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    dump_message(&received_message);

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);

    exit(EXIT_SUCCESS);
}

void
recv_unknown_message_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    dump_message(&received_message);
    fprintf(stderr, "received unknown message\n");

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);

    exit(EXIT_SUCCESS);
}

void
recv_unexpected_message_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    dump_message(&received_message);
    fprintf(stderr, "received unexpected message\n");

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);

    exit(EXIT_SUCCESS);
}

void
timed_out_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    fprintf(stderr, "waiting message, but timed out\n");

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);

    exit(EXIT_SUCCESS);
}

void
recv_NG_ack_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    dump_message(&received_message);

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);

    exit(EXIT_SUCCESS);
}

void
send_release_proc(int socket, struct sockaddr *server_socket, struct message *message)
{
    struct message release_message;
    fprintf(stderr, "releasing allocated IP address...\n");

    memset(&release_message, 0, sizeof(release_message));
    release_message.type = TYPE_RELEASE;
    release_message.ipaddr.s_addr = myip.s_addr;
    if (sendto(socket, &release_message, sizeof(release_message), 0, server_socket, sizeof(server_socket)) < 0) {
        perror("sendto @ send_release_proc");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "send RELEASE\n");
    fprintf(stderr, "\t-- [ (->) RELEASE ] --\n");

    dump_message(&release_message);

    fprintf(stderr, "%s --> EXIT\n",
            CLIENT_STATES_NAME[state]);
    exit(EXIT_SUCCESS);
}

/* ---------------------- helper functions ---------------------- */
void
send_discover(int socket, struct sockaddr *server_socket)
{
    struct message discover_message;
    memset(&discover_message, 0, sizeof(discover_message));

    discover_message.type = TYPE_DISCOVER;
    if (sendto(
            socket,
            &discover_message,
            sizeof(discover_message),
            0,
            server_socket,
            sizeof(struct sockaddr))
                    != sizeof(discover_message)) {
        perror("sendto @ send_discover");
        exit(EXIT_FAILURE);
    }

    dump_message(&discover_message);
}

void
send_alloc_request(int socket, struct sockaddr *server_socket)
{
    send_request(socket, server_socket, CODE_ALLOCREQ);
}

void
send_ext_request(int socket, struct sockaddr *server_socket)
{
    send_request(socket, server_socket, CODE_EXTREQ);
}

void
send_request(int socket, struct sockaddr *server_socket, uint8_t code)
{
    struct message request_message;
    memset(&request_message, 0, sizeof(request_message));
    request_message.type = TYPE_REQUEST;
    request_message.code = code;
    request_message.ttl = htons(allocated_ttl);
    request_message.ipaddr.s_addr = myip.s_addr;
    request_message.netmask.s_addr = mynetmask.s_addr;
    if (sendto(
            socket,
            &request_message,
            sizeof(request_message),
            0,
            server_socket,
            sizeof(struct sockaddr))
                    != sizeof(request_message)) {
        perror("sendto @ send_request");
        exit(EXIT_FAILURE);
    }
    dump_message(&request_message);
}

void dump_message(struct message *message)
{
    switch (message->type) {
        case TYPE_DISCOVER: // client -> server
            fprintf(stderr, "send %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-- [ (->) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tType:    %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_OFFER:    // client <- server
            fprintf(stderr, "receive %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-- [ (<-) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tType:      %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tCode:      %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "|\tIP:        %s\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "|\tNetmask:   %d\t\t\t\t\t\t\t|\n", ntohs(message->netmask.s_addr));
            fprintf(stderr, "|\tTTL:       %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_REQUEST:  // client -> server
            fprintf(stderr, "send %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-- [ (->) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tType:    %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tCode:    %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "\t|\tIP:      %s\t\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "\t|\tNetmask: %d\t\t\t\t\t\t|\n", message->netmask.s_addr);
            fprintf(stderr, "\t|\tTTL:     %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "\t+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_ACK:      // client <- server
            fprintf(stderr, "receive %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-- [ (<-) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tType:      %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tCode:      %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "|\tIP:        %s\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "|\tNetmask:   %d\t\t\t\t\t\t|\n", message->netmask.s_addr);
            fprintf(stderr, "|\tTTL:       %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_RELEASE:  // client -> server
            fprintf(stderr, "send %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-- [ (->) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tType:    %s\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tIP:      %s\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "\t+-----------------------------------------------------------------------+\n");
            break;
        default:
            return;
    }
}

void
SIGALRM_handler()
{
    struct timeval tv;
    tv.tv_sec = allocated_ttl;
    gettimeofday(&tv, NULL);
    // fprintf(stderr, "[%s]: caught SIGALRM\n", ctime(&tv.tv_sec));
    alarm(0); // stop timer
    ALRM_flag = 1;
}

void
SIGHUP_handler()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(stderr, "[%s]: caught SIGHUP\n", ctime(&tv.tv_sec));
    HUP_flag = 1;
}