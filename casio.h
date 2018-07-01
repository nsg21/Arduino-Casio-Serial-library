/* (C) 2018 by nsg
 * Host implementation for Casio Basic SEND/RECEIVE serial interface operators.
 *
 * Incoming variables (sent by SEND()) are delivered to inboxes,
 * Values for requested variables (requsted by RECEIVE()) are sought in
 * outboxes.
 * Mailboxes have freshness flags (.fresh). They have slightly different
 * meaning for inboxes and for outboxes.
 *
 * Whenever inbox receives a value (sent by a calculator with a SEND()
 * operator), the .fresh flag is set. Firmware should clear this flag after it
 * reads this value and acts on it. Normal inboxes will keep calculator on hold
 * until this flag is cleared. Inbox marked immediate (.immediate flag) do not
 * do this and proceed with the protocol immediately after the value is
 * received.
 * 
 * If a calculator requests a value with a RECEIVE() operator, .immediate
 * outbox will provide a value right away, whether it is fresh or not. Outbox
 * without .immediate flag will keep calculator on hold until .fresh flag goes
 * on. After the value is sent to the calc, the .fresh flag is cleared again.
 * The poll function calls a hook (*casio_receive_hook) immediately after upon
 * request. The firmware may use it to initiate a process of obtaining a value
 * for that name.
 *
 */


// variable names are
// 'A'..'Z'
// 0xcd = r
// 0xce = θ

#define CASIO_LOWR 0xcd
#define CASIO_THETA 0xce


#define CalSerial Serial3

#define CASIO_STATIC_MAILBOX

typedef struct casiomailbox {
  char name;
  // "freshness" indicator
  // inbox: indicates new data from calc; expected to be cleared after firmware
  //   acts upon it
  // outbox: set by firmware whenever it updates it
  bool fresh;
  bool immediate; // ignore freshness, use the data as is and immediately
  double value;
  struct casiomailbox *next; // linked list
#ifndef CASIO_STATIC_MAILBOX
#endif
} CasioMailBox;

// Use these macros to allocate memory statically for mailboxes.
#ifdef CASIO_STATIC_MAILBOX
#define IMMEDIATE(n) {name:n, fresh:false, immediate:true, value:0.0}
#define MAILBOX(n,imm) {name:n, fresh:false, immediate:imm, value:0.0}
#endif

// use the POST_TO_BOX macro to post directly or to define 
// post macros for specific mailboxes, e.g.
// #define BOX_LEFT(v) POST_TO_BOX(my_outbox[0],v)

#define POST_TO_BOX(BOX,V) do{BOX.value=V;BOX.fresh=true;}while(0)

extern CasioMailBox *casio_inboxes;
extern CasioMailBox *casio_outboxes;

CasioMailBox *get_mailbox(CasioMailBox **head, char name, bool create_if_not_exists=false);

inline CasioMailBox *get_outbox(char name) { return get_mailbox(&casio_outboxes, name); }
inline CasioMailBox *get_inbox(char name) { return get_mailbox(&casio_inboxes, name); }

// get_mailbox relies on linked list fields to find appropriate box.
// If mailboxes are allocated in static arrays, their link fields need to be
// initialized with fill_static_links(), e.g.:
//  fill_static_links(&my_inbox[0], sizeof(my_inbox));
//  fill_static_links(&my_outbox[0], sizeof(my_outbox));
void fill_static_links(CasioMailBox *head, int count);

// This procedure implements serial protocols for SEND() and RECEIVE()
// operators. It populates inboxes with the incoming values and uses values in
// outboxes to respond to variable requests.
// It should be called periodically, e.g. in the loop()
void casio_poll();

// This hook is called each time casio_poll gets a RECEIVE() request from a
// calculator. It gets the name of the requested variable as its first
// parameter.
extern void (*casio_receive_hook)(char);

/*
 * --Receive()     --Send()
 * Casio MCU       Casio MCU
 * $15   $13       $15   $13
 * :REQ  $06       :VAL  $06
 * $06   :VAL      :0101 $06 <- only present when "in use" byte==1
 * $06   :0101     :END
 * $06   :END
 */

