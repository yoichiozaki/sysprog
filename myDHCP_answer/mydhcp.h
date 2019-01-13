//
// Created by 尾崎耀一 on 2019-01-12.
//

#ifndef MYDHCP_ANSWER_MYDHCP_H
#define MYDHCP_ANSWER_MYDHCP_H

#include <stdint.h>
#include <netinet/in.h>

#define MYDHCP_PORT     55150   // mydhcp podr number
#define RECV_TIMEOUT    10      // recv time out

// MYDHCP message definition
struct message {
    uint8_t         type;       // message type
    uint8_t         code;       // detailed info about message type
    uint16_t        ttl;        // time to live (network byte order)
    struct in_addr  ipaddr;     // IP address (network byte order)
    struct in_addr  netmask;    // netmask (network byte order)
};

// MYDHCP header type
#define TYPE_DISCOVER   1       // client -> server
#define TYPE_OFFER      2       // server -> client
#define TYPE_REQUEST    3       // client -> server
#define TYPE_ACK        4       // server -> client
#define TYPE_RELEASE    5       // client -> server

// MYDHCP header code
#define CODE_SUCCUESS   0       // success
#define CODE_NOIPADDR   1       // OFFER: no IP address available
#define CODE_ALLOCREQ   2       // REQUEST: address allocate request
#define CODE_EXTREQ     3       // REQUEST: address TTL extension request
#define CODE_PARMERR    4       // ACK: parameter error

// client's states
#define C_STATE_INIT                1       // initial state
#define C_STATE_WAIT_OFFER          2       // sent DISCOVER, waiting for OFFER
#define C_STATE_OFFER_TIMEOUT       3       // wait receiving OFFER but timed out
#define C_STATE_WAIT_ACK            4       // sent REQUEST, waiting for ACK
#define C_STATE_ACK_TIMEOUT         5       // wait receiving ACK, but timed out
#define C_STATE_INUSE               6       // allocated IP address
#define C_STATE_WAIT_EXT_ACK        7       // sent extension REQUEST, waiting for ACK
#define C_STATE_EXT_ACK_TIMEOUT     8       // wait receiving ACK for extension request, but timed out

// client's events
#define C_EVENT_RECV_OFFER_OK       1       // received OFFER(OK)
#define C_EVENT_RECV_OFFER_NOIP     2       // received OFFER(NG)
#define C_EVENT_RECV_ACK_OK         3       // received ACK(OK)
#define C_EVENT_RECV_ACK_NG         4       // recvived ACN(NG)
#define C_EVENT_TIMEOUT             5       // waiting message, but timed out
#define C_EVENT_HALF_TTL            6       // TTL/2 have passed
#define C_EVENT_RECV_SIGHUP         7       // caught SIGHUP
#define C_EVENT_RECV_UNKNOWN        8       // recvived unknown message
#define C_EVENT_DEFAULT             -1      // default error event

// server's states
#define S_STATE_INIT                1       // initial state
#define S_STATE_WAIT_REQUEST        2       // sent OFFER, waiting for REQUEST
#define S_STATE_INUSE               3       // allocating IP address for the client
#define S_STATE_REQUEST_TIMEOUT     4       // wait receiving REQUEST, but timed out

// server's events
#define S_EVENT_RECV_DISCOVER       1       // received DISCOVER
#define S_EVENT_RECV_ALLOC_REQUEST  2       // received REQUEST(allocate)
#define S_EVENT_REQUEST_TIMEOUT     3       // wait receiving REQUEST, but timed out
#define S_EVENT_RECV_EXT_REQUEST    4       // received REQUEST(extend)
#define S_EVENT_RECV_RELEASE        5       // received RELEASE
#define S_EVENT_TTL_TIMEOUT         6       // TTL timed out
#define S_EVENT_RECV_UNKNOWN        7       // received unknown message
#define S_EVENT_RECV_UNEXPECTED     8       // received unexpected message
#define S_EVENT_DEFAULT             9       // default error event

// clients list (used by server)
struct client {
    struct client *fp;          // pointer to the forward client
    struct client *bp;          // pointer to the backword client
    int state;                  // the client's state
    int ttl_counter;            // TTL counter (host byte order)
    int timeout_counter;        // timeout counter for REQUEST

    // below: network byte order
    struct in_addr id;          // client's id (IP address)
    struct in_addr ipaddr;      // allocated IP address
    struct in_addr netmask;     // netmask
    in_port_t port;             // port number of the client
    uint16_t ttl;               // TTL of the allocated IP address
};

// address pool (used by server)
struct address_pool {
    struct address_pool *fp;
    struct address_pool *bp;

    // below: network byte order
    struct in_addr ipaddr;
    struct in_addr netmask;
    int ttl;
};

// table for message types
char *MESSAGE_TYPE_NAME[] = {
        "ERROR(0)                                   ",
        "DISCOVER(1)                                ",
        "OFFER(2)                                   ",
        "REQUEST(3)                                 ",
        "ACK(4)                                     ",
        "RELEASE(5)                                 "
};

// table for message codes
char *MESSAGE_CODE_NAME[] = {
        "SUCCESS(0)                                 ",
        "NO IP ADDRESS AVAIALBE(1)                  ",
        "ALLOCATION REQUEST(2)                      ",
        "EXTENSION REQUEST(3)                       ",
        "PARAMETER ERROR(4)                         "
};

// table for states' names of client
char *CLIENT_STATES_NAME[] = {
        "ERROR(0)                                   ",
        "INIT(1)                                    ",
        "WAIT OFFER(2)                              ",
        "WAITING OFFER, BUT TIMED OUT(3)            ",
        "WAITING ACK(4)                             ",
        "WAITING ACK, BUT TIMED OUT(5)              ",
        "IN USE(6)                                  ",
        "WAITING ACK FOR EXTENSION(7)               ",
        "WAITING ACK FOR EXTENSION, BUT TIMED OUT(8)"
};

// table for states' names of server
char *SERVER_STATES_NAME[] = {
        "ERROR(0)                                   ",
        "INIT(1)                                    ",
        "WAITING REQUEST(2)                         ",
        "IN USE(3)                                  ",
        "WAITING REQUEST, BUT TIMED OUT(4)          "
};

// table for events' names of client
char *CLIENT_EVENT_NAME[] = {
        "ERROR(0)                                   ",
        "RECEIVED OFFER[OK](1)                      ",
        "RECEIVED OFFER[NO IP ADDRESS AVAILABLE](2) ",
        "RECEIVED ACK[OK](3)                        ",
        "RECEIVED ACK[NG](4)                        ",
        "WAIT RECEIVING MESSAGE, BUT TIMED OUT(5)   ",
        "HAVE PASSED HALF TTL(6)                    ",
        "CAUGHT SIGHUP(7)                           ",
        "RECEIVED UNKNOWN MESSAGE(8)                "
};

// table for events' names of server
char *SERVER_EVENT_NAME[] = {
        "ERROR(0)                                   ",
        "RECEIVED DISCOVER(1)                       ",
        "RECEIVED ALLOCATION REQUEST(2)             ",
        "WAIT RECEIVING REQUEST, BUT TIMED OUT(3)   ",
        "RECEIVED EXTENSION REQUEST(4)              ",
        "RECEIVED RELEASE(5)                        ",
        "HAVE PASSED HALF TTL(6)                    ",
        "RECEIVED UNKNOWN MESSAGE(7)                ",
        "RECEIVED UNEXPECTED MESSAGE(8)             ",
        "DEFAULT ERROR EVENT(9)                     "
};

#endif //MYDHCP_ANSWER_MYDHCP_H
