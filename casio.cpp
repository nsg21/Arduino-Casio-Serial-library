// (C) 2018 by nsg
#include "Arduino.h"
// check communication for 0x15
// when get it, immediately send back 0x13
// and wait for further info
//
// If receive :REQ request, check if there is anything for that name and send it
// Do not send back 0x06 until there is something (calc will wait indefinitely
// at this point)
// Once there is data for this name, send 0x06, and proceed with sending :VAL,
// :0101 and :END
//
// If receive :VAL request, acknoledge it immediately and save it to the
// mailbox for that name for other parts of the firmware to consume.
// Alternative strategy, if it is considered an atomic command, hold on on
// sending final 0x06 until the "command" is executed (e.g. rover has actually
// driven specified distance or temperature reached specified value)

/*
  inbox -- contains values for received names. "action" names require "fresh" flag to be cleared before 
  outbox -- contains values for 
  "new" flag:
    inbox
      do not acknoledge until "fresh" flag is clear
    outbox
      do not acknoledge

  Immediate box does not wait, nonimmediate waits forever.
  It may have sense to have something in between: timed-out box which emits a
  value after a timeout even if it is not fresh.
*/

#include "casio.h"

MailBox *cccp_inboxes=NULL;
MailBox *cccp_outboxes=NULL;

#define CASIO_DEBUG

void cccp_null_hook(char name)
{
}

void (*cccp_hook_receive)(char)=&cccp_null_hook;


// find a mailbox for a name or create one

#ifdef CCCP_STATIC_MAILBOX
// run in setup() to populate links in mailboxes defined literally
void fill_static_links(MailBox *head, int count)
{
  for(int i=1; i<count; ++i ) {
    head[i-1].next=head+i;
  }
  head[count-1].next=NULL;
  
}
#endif


MailBox fakebox;

MailBox *get_mailbox(MailBox **head, char name, bool create_if_not_exists)
{
  MailBox *p=*head;
  while( NULL!=p ) {
    if( p->name == name ) return p;
    p=p->next;
  }
#ifdef CASIO_DEBUG
  Serial.print("Not found mailbox for ");
  Serial.println(name);
#endif
#ifdef CCCP_STATIC_MAILBOX
  fakebox.name=name;
  fakebox.next=NULL;
  fakebox.fresh=true;
  fakebox.immediate=true;
  fakebox.value=12345.67;
  return &fakebox;
#else
  if( !create_if_not_exists ) return NULL;
  p=(MailBox*)malloc(sizeof(MailBox));
  if( NULL==p ) return NULL; // cannot allocate
  // create new and update head
  p->name=name;
  p->next=*head;
  *head=p;
  p->fresh=false;
  p->immediate=true; // signals are immediate by default to keep calc from waiting;
#endif
}

/*
 * --Receive()     --Send()
 * Casio MCU       Casio MCU
 * $15   $13       $15   $13
 * :REQ  $06       :VAL  $06
 * $06   :VAL      :0101 $06
 * $06   :0101     :END
 * $06   :END
 */

/*
  
  IDLE -> (get $15) -> ALERT -> (send $13) -> GETHEADER ->
   (:END) -> IDLE
   (:VAL) -> SEND_ACK1 -> (send $06) -> SEND_WAIT_DATA ->
        (get :0101 with data) -> SEND_EXECUTE_DATA -> (send $06) -> GETHEADER 
   (:REQ) -> RECEIVE_WAIT_DATA-> -> RECEIVE_ACK1->
        (send $06)->RECEIVE_CLIENT_WAIT1->(receive $06)->RECEIVE_VAL->(send :VAL)->RECEIVE_CLIENT_WAIT2->(receive $06)->

States are named after casio basic operators SEND and RECEIVE.
So, SEND's states are about arduino receiving data and possibly acting on it
and RECEIVE's states are about arduino obtaining sensor or process data and
sending it to calc.
  
*/


enum CCCP_STATE {
  CCCP_ALERT,
  CCCP_NACK,
  CCCP_GETHEADER0,
  CCCP_GETHEADER,
  CCCP_SEND_ACK1,
  CCCP_SEND_WAITDATA0,
  CCCP_SEND_WAITDATA,
  CCCP_SEND_EXECUTEDATA,
  CCCP_RECEIVE_WAITDATA,
  CCCP_RECEIVE_ACK1,
  CCCP_RECEIVE_CLIENTWAIT1,
  CCCP_RECEIVE_VAL0,
  CCCP_RECEIVE_VAL,
  CCCP_RECEIVE_CLIENTWAIT2,
  CCCP_RECEIVE_0101_0,
  CCCP_RECEIVE_0101,
  CCCP_RECEIVE_CLIENTWAIT3,
  CCCP_RECEIVE_END0,
  CCCP_RECEIVE_END,
  CCCP_IDLE
};


int cccp_state=CCCP_IDLE;
MailBox *cccp_actionbox;

// offsets in a buffer
#define CASIO_B_NAME 11
#define CASIO_B_COMPLEX 27 // 'C' or 'R'
#define CASIO_B_RANK 5 // VM/PC/LT/MT
#define CASIO_B_VARTAG 19 // 'Variable' tag
#define CASIO_B_USED1 8
#define CASIO_B_USED2 10
#define CASIO_B_CHECKSUM 49
#define CASIO_B_SIZE 50


#define CASIO_B_RE 5 // location of the real part
#define CASIO_B_IM 15 // location of the complex part
#define CASIO_R_SIZE 16 // real value buffer size
#define CASIO_C_SIZE 26 // complex value buffer size

#define CASIO_DEFAULT_VALUE 987.654 // value to use for non-existent mailboxes

byte cccp_buffer[CASIO_B_SIZE];
int cccp_buffer_index;
int cccp_buffer_size;
char cccp_varname; // recently requested name; needed if there is no mailbox for it

byte casio_checksum(byte *buffer, int size)
{
  byte chk=0;
  int i;
  for(i=0; i<size; ++i ) {
    chk+=buffer[i];
  }
  return 1+~(chk-0x3a);
}

const byte PACKET_END[] PROGMEM={
 ':','E','N','D', 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff, 0xff,
 0xff, 0xff, 0xff, 0xff,
 'V'
};

const byte* HEADER_END=&PACKET_END[0];
const byte HEADER_REQ[] PROGMEM={ ':','R','E','Q', 0 };
const byte HEADER_VAL[] PROGMEM={ ':','V','A','L', 0 };
const byte HEADER_0101[] PROGMEM={ ':', (char)0,(char)1,(char)0,(char)1 };

// from bcd
int fbcd(byte b){
  return (b & 0x0f)+10*(b>>4);
}

byte bcd(int v){
  if( v<0 ) return 0;
  if( v>99 ) return 99;
  return (v%10) | ((v/10)<<4);
}

// "sign" byte bitfields
#define CASIO_EXPPOS 1
#define CASIO_NEG    0x50

#define CASIO_IM     0x80

#ifdef CASIO_DEBUG
void serial_dump(byte *buffer, size_t size){
  char buf[10];
  for(int i=0; i<size; ++i){
    sprintf(buf," %02x",buffer[i]);
    Serial.print(buf);
  }
  Serial.print("\n");
}
#endif

byte *casio_double_fmt(byte *buffer, double value)
{
  char fmtbuf[32];
  int sign=0;
#ifdef CASIO_DEBUG
  Serial.print("Formatting ");
  Serial.println(value);
#endif
  /*
   * If non-restricted sprintf and 8-byte double were available, the formating
   * procedure would look like this:
  sprintf(fmtbuf,"%-#.14e",value);
  Serial.print("fmtbuf=");
  Serial.println(fmtbuf);
   * 0..15 -- mantissa (including decimal point)
   * 16,17 -- "e+"
   * 18..20 -- exp
   */

   /*
   * Arduino's printf does not format floats
   * Also, Arduino's doubles are actually floats (4 bytes ieee)
dtostre formatting=+9.8765000e+32
   * 0=sign
   * 1..9 = digits of mantissa
   * 10="e"
   * 11=exponent sign
   * 12..13=exponent
   */
  dtostre(value,fmtbuf,20,DTOSTR_ALWAYS_SIGN|DTOSTR_PLUS_SIGN);
  if( fmtbuf[0]=='-' ) {
    sign=sign|CASIO_NEG;
  }
  int exp;
  if( fmtbuf[1]=='i' ) {
    goto OVERFLOW;
  } else {
    buffer[0]=fmtbuf[1] & 0x0f;
    
    for(int i=1; i<4;++i) {
      buffer[i]=((fmtbuf[1+2*i]&0x0f)<<4)|((fmtbuf[2+2*i]&0x0f));
    }
    buffer[4]=(fmtbuf[9]&0x0f)<<4;
    exp=(fmtbuf[12]-'0')*10+(fmtbuf[13]-'0');
    if(fmtbuf[11]=='-') {
      exp=100-exp;
      if(exp<1) {
        // CAUTION: undeflow
        exp=0;
        memset(&buffer[0],0x00,8);
      }
    } else {
      sign=sign|CASIO_EXPPOS;
    }
    if(exp>99) {
      // Cannot happen on Arduino, but might happen on platform with 8 byte
      // double.
OVERFLOW:
      // CAUTION: overflow
      exp=99;
      memset(&buffer[1],0x99,7);
      buffer[0]=0x09;
    }
  }
  buffer[8]=sign;
  buffer[9]=bcd(exp);
#ifdef CASIO_DEBUG
  Serial.print("Buffer=");
  serial_dump(buffer,10);
  Serial.print("\n");
#endif
  return buffer; 
}

/* 
 * --- Real:
 * :0101
 * dddd dddd s e
 * Σ
 * --- Complex:
 * :0101
 * dddd dddd s e
 * dddd dddd s e
 * Σ
 */

// 0  1  2  3   4  5  6  7   8  9
// 0d dd dd dd  dd dd dd dd  ss ee 
double casio_double_parse(byte *buffer)
{
  int i;
  double r=0.0;
  int exp,sign;
#ifdef CASIO_DEBUG
  Serial.print("Parse");
  serial_dump(buffer,10);
#endif
  for( i=0; i<8; ++i ) r=r*100.0+fbcd(buffer[i]);
  r=r/1e14;
  sign=buffer[8];
  exp=fbcd(buffer[9]);
#ifdef CASIO_DEBUG
  Serial.print("mantissa=");
  Serial.println(r);
  Serial.print("exp=");
  Serial.println(exp);
  Serial.print("sign=");
  Serial.println(sign);
#endif
  if( 0==(sign & CASIO_EXPPOS) ) exp=exp-100;
  if( 0!=(sign & CASIO_NEG ) ) r=-r;
  return r*pow(10.0,exp);
}

void fmt_double(byte *buffer, double val)
{
  int sign=0;
  if( val<0.0 ) {
    sign=sign | CASIO_NEG;
    val=-val;
  }
  
}
 

const char TAG_VM[] PROGMEM = {'V','M'}; // named variable
const char TAG_PC[] PROGMEM = {'P','C'}; // picture
const char TAG_LT[] PROGMEM = {'L','T'}; // list
const char TAG_MT[] PROGMEM = {'M','T'}; // matrix
const char TAG_Variable[] PROGMEM = {'V','a','r','i','a','b','l','e'};

int cccp_analyze_header(byte *buffer)
{
  // :END -> idle
  // :REQ -> CCCP_RECEIVE_WAITDATA
  // :VAL -> CCCP_SEND_ACK1
  // setup cccp_actionbox
#ifdef CASIO_DEBUG
  Serial.print("Received header ");
  serial_dump(buffer, CASIO_B_SIZE);
#endif
  if( buffer[CASIO_B_CHECKSUM]!=casio_checksum(buffer, CASIO_B_SIZE-1) ) return CCCP_NACK;
  if( 0==memcmp_P(&buffer[0],HEADER_END,5) ) return CCCP_IDLE;
  if( 0==memcmp_P(&buffer[0],HEADER_VAL,5) ) {
    // :VAL request, :0101 packet with actual data possibly to follow
    if( 0!=memcmp_P(&buffer[CASIO_B_RANK],TAG_VM,2) ) return CCCP_NACK;
    cccp_actionbox=get_inbox(buffer[CASIO_B_NAME]);
    cccp_varname=buffer[CASIO_B_NAME];
    if( buffer[CASIO_B_USED1]!=buffer[CASIO_B_USED2] ) return CCCP_NACK;
    switch(buffer[CASIO_B_USED1]) {
      case 0:
        // variable has not been assigned yet, no :0101 to follow
        cccp_buffer_size=0;
        break;
      case 1:
        // size of expected :0101 packet
        cccp_buffer_size=buffer[CASIO_B_COMPLEX]=='C'?26:16;
        break;
      default:
        return CCCP_NACK;
    }
    return CCCP_SEND_ACK1;
  }
  if( 0==memcmp_P(&buffer[0],HEADER_REQ,5) ) {
    // TODO: investigate if there is NACK to respond to valid requests that are
    // not supproted
    if( 0!=memcmp_P(&buffer[CASIO_B_RANK],TAG_VM,2) ) return CCCP_NACK;
    cccp_actionbox=get_outbox(buffer[CASIO_B_NAME]);
    cccp_varname=buffer[CASIO_B_NAME];
    if( NULL!=cccp_hook_receive ) (*cccp_hook_receive)(cccp_varname);
    return CCCP_RECEIVE_WAITDATA;
  }
  return CCCP_NACK;
}

int cccp_analyze_senddata(byte* buffer)
{
  // validate that data in buffer is :0101 of acceptable characteristics
  // decode value
  // populate actionbox
  // move on to CCCP_SEND_EXECUTEDATA
#ifdef CASIO_DEBUG
  Serial.print("senddata: ");
  serial_dump(buffer,cccp_buffer_size);
#endif

  if( buffer[cccp_buffer_size-1]!=casio_checksum(buffer, cccp_buffer_size-1) ) return CCCP_NACK;
  if( 0!=memcmp_P(&buffer[0],HEADER_0101,5) ) return CCCP_NACK;
  if( NULL!=cccp_actionbox ) {
    cccp_actionbox->value=casio_double_parse(&buffer[CASIO_B_RE]);
    // TODO? deal with imaginary part if present
    cccp_actionbox->fresh=true;
  }
  return CCCP_SEND_EXECUTEDATA;
}


long int last_change;
int last_state;

void casio_poll()
{
  int rd;
  if( cccp_state!=last_state ){
    last_change=millis();
  } else {
    if( cccp_state!=CCCP_IDLE && millis()>last_change+6000 ) {
      Serial.print("Stuck in state ");
      Serial.println(cccp_state);
      last_change=millis();
    }
  }
  last_state=cccp_state;
  while(true) switch(cccp_state){
    case CCCP_IDLE:
      if( 0==CalSerial.available() ) return;
      if( CalSerial.read()==CASIO_ATT ) cccp_state=CCCP_ALERT;
      break;
    case CCCP_ALERT:
      if( 0==CalSerial.availableForWrite() ) return;
      CalSerial.write(CASIO_READY);

    case CCCP_GETHEADER0:
      cccp_buffer_index=0;
      cccp_buffer_size=CASIO_B_SIZE;
      cccp_state=CCCP_GETHEADER;
    case CCCP_GETHEADER:
      // TODO: timeout
      if( 0==CalSerial.available() ) return;
      cccp_buffer[cccp_buffer_index++]=CalSerial.read();
      if( cccp_buffer_index>=cccp_buffer_size )
        cccp_state=cccp_analyze_header(cccp_buffer);
      break;

    case CCCP_SEND_ACK1:
      if( 0==CalSerial.availableForWrite() ) return;
      CalSerial.write(CASIO_ACK);
      if( cccp_buffer_size==0 ) {
        // not expecting :0101, jump right to :END
        cccp_state=CCCP_GETHEADER0;
        break;
      }

    case CCCP_SEND_WAITDATA0:
      cccp_buffer_index=0;
      // cccp_buffer_size should be set by cccp_analyze_header;
      cccp_state=CCCP_SEND_WAITDATA;
    case CCCP_SEND_WAITDATA:
      // expecting :0101
      if( 0==CalSerial.available() ) return;
      cccp_buffer[cccp_buffer_index++]=CalSerial.read();
      if( cccp_buffer_index>=cccp_buffer_size )
        cccp_state=cccp_analyze_senddata(cccp_buffer);
      break;

    case CCCP_SEND_EXECUTEDATA:
      // CAUTION: make sure that non-immediate mailboxes are properly acted
      // upon by firmware and the freshness bit is cleared when that happens.
      // TODO? timed-out actionboxes
      if( NULL!=cccp_actionbox
      && !cccp_actionbox->immediate
      && cccp_actionbox->fresh )
        return; // keep calc on hold while data is executing
      // CAUTION: it is possible that once the freshness conditions are
      // satisfied, the out queue is full and while we are waiting for it to
      // clear, freshness conditions may become not satisfied. It should not
      // happen: immediate flag should not change and once fresness flag is
      // clear it should not be set until the next SEND packet arrives. But it
      // is logically possible.
      if( 0==CalSerial.availableForWrite() ) return;
      CalSerial.write(CASIO_ACK);
      cccp_state=CCCP_GETHEADER0;
      // Expect to get :END packet which leads to IDLE, but if there is some
      // other valid :VAL or :REQ packet, it might as well be acted upon.
      break;

    case CCCP_RECEIVE_WAITDATA:
      // wait for the data to become ready, client may be on hold
      if( NULL!=cccp_actionbox
      && !cccp_actionbox->immediate
      && !cccp_actionbox->fresh )
        return;
      cccp_state=CCCP_RECEIVE_ACK1;
    case CCCP_RECEIVE_ACK1:
      if( 0==CalSerial.availableForWrite() ) return;
      CalSerial.write(CASIO_ACK);
      cccp_state=CCCP_RECEIVE_CLIENTWAIT1;
    case CCCP_RECEIVE_CLIENTWAIT1:
      if( 0==CalSerial.available() ) return;
      if( CalSerial.read()!=CASIO_ACK ) {
        cccp_state=CCCP_IDLE;
        break;
      }
      
    case CCCP_RECEIVE_VAL0:
      // populate :VAL buffer
      cccp_buffer_size=CASIO_B_SIZE;
      memcpy_P(cccp_buffer,PACKET_END,CASIO_B_SIZE);
      memcpy_P(cccp_buffer,HEADER_VAL,5);
      memcpy_P(cccp_buffer+CASIO_B_RANK,TAG_VM,2);
      cccp_buffer[7]=0;
      cccp_buffer[CASIO_B_USED1]=1;
      cccp_buffer[9]=0;
      cccp_buffer[CASIO_B_USED2]=1;
      cccp_buffer[CASIO_B_NAME]=cccp_varname;
      memcpy_P(cccp_buffer+CASIO_B_VARTAG,TAG_Variable,8);
      cccp_buffer[CASIO_B_COMPLEX]='R';
      cccp_buffer[CASIO_B_COMPLEX+1]=0x0a;
      cccp_buffer[CASIO_B_CHECKSUM]=casio_checksum(cccp_buffer,cccp_buffer_size-1);

      cccp_state=CCCP_RECEIVE_VAL;
      cccp_buffer_index=0;
#ifdef CASIO_DEBUG
      Serial.print(":VAL for ");
      Serial.println(cccp_varname);
      serial_dump(cccp_buffer,cccp_buffer_size); 
#endif
    case CCCP_RECEIVE_VAL:
      // transmit :VAL buffer
      if( 0==CalSerial.availableForWrite() ) return;
      if( cccp_buffer_index<cccp_buffer_size ) {
        CalSerial.write(cccp_buffer[cccp_buffer_index++]);
        break;
      }
      cccp_state=CCCP_RECEIVE_CLIENTWAIT2;
    case CCCP_RECEIVE_CLIENTWAIT2:
      if( 0==CalSerial.available() ) return;
      rd=CalSerial.read();
      if( rd==CASIO_RETRY ) {
        // resend already populated buffer
        cccp_state=CCCP_RECEIVE_VAL;
        cccp_buffer_index=0;
        break;
      } else if( rd!=CASIO_ACK ) {
        cccp_state=CCCP_IDLE;
        break;
      }
    case CCCP_RECEIVE_0101_0:
      // TODO: populate :0101 buffer
      cccp_buffer_size=CASIO_R_SIZE; // TODO: ...or _C_SIZE
      memset(cccp_buffer,0,cccp_buffer_size);
      memcpy_P(cccp_buffer,HEADER_0101,5);
      // TODO? send back "unused" response instead of a default value
      casio_double_fmt(&cccp_buffer[CASIO_B_RE]
       ,cccp_actionbox==NULL?CASIO_DEFAULT_VALUE:cccp_actionbox->value);
      cccp_buffer[cccp_buffer_size-1]=casio_checksum(cccp_buffer,cccp_buffer_size-1);
      cccp_buffer_index=0;
      cccp_state=CCCP_RECEIVE_0101;
    case CCCP_RECEIVE_0101:
      // transmit 0101 buffer
      if( 0==CalSerial.availableForWrite() ) return;
      if( cccp_buffer_index<cccp_buffer_size ) {
        CalSerial.write(cccp_buffer[cccp_buffer_index++]);
        break;
      }
      cccp_state=CCCP_RECEIVE_CLIENTWAIT3;

    case CCCP_RECEIVE_CLIENTWAIT3:
      if( 0==CalSerial.available() ) return;
      rd=CalSerial.read();
      if( rd==CASIO_RETRY ) {
         // TODO? repopulate buffer with fresher data
         cccp_buffer_index=0;
         cccp_state=CCCP_RECEIVE_0101;
         break;
      } else if( rd!=CASIO_ACK ) {
        cccp_state=CCCP_IDLE;
        break;
      }
      // only clear freshness if received confirmation
      if( cccp_actionbox!=NULL ) cccp_actionbox->fresh=false;
    
    case CCCP_RECEIVE_END0:
      // populate :END buffer
      cccp_state=CCCP_RECEIVE_END;
      cccp_buffer_size=CASIO_B_SIZE;
      memcpy_P(cccp_buffer, PACKET_END, CASIO_B_SIZE);
      cccp_buffer_index=0;
    case CCCP_RECEIVE_END:
      // transmit :END buffer
      if( 0==CalSerial.availableForWrite() ) return;
      if( cccp_buffer_index<cccp_buffer_size ) {
        CalSerial.write(cccp_buffer[cccp_buffer_index++]);
        break;
      }
      cccp_state=CCCP_IDLE;
      break;
      // send value requested value
    case CCCP_NACK:
      if( 0==CalSerial.availableForWrite() ) return;
#ifdef CASIO_DEBUG
      Serial.println("Sending NACK");
#endif
      CalSerial.write(CASIO_ERROR);
      cccp_state=CCCP_IDLE;
      break;
      
    default:
      cccp_state=CCCP_IDLE;
  }
}




