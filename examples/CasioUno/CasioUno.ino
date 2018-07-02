#include <Arduino.h>
#include "CasioSerial.h"

CasioMailBox my_inbox[]={
  MAILBOX('W',false) // request by a calc: number of milliseconds to delay
#define BOX_WAIT my_inbox[0]
  ,MAILBOX('L',true) // brightness of built-in LED (0..255)
#define BOX_LED my_inbox[1] // sent index for subsequent read
  ,MAILBOX('I',true) // select analog input to read with 'V'
#define BOX_INDEX my_inbox[2] // sent index for subsequent read
};

CasioMailBox my_outbox[]={
  IMMEDIATE('T') // millisecond timer
#define BOX_MILLIS(v) POST_TO_BOX(my_outbox[0],v)
  ,IMMEDIATE('U') // microsecond timer
#define BOX_MICROS(v)  POST_TO_BOX(my_outbox[1],v)
  ,IMMEDIATE('V') // sensor value read from analog input 'I'
#define BOX_VALUE(v)    POST_TO_BOX(my_outbox[2],v)
};

void hook_example(char name) {
}

void setup() {
  // No debug or monitoring via serial port -- port is occupied by Casio

  // Setup communication interface.
  casio_serial=&Serial;
  casio_serial->begin(9600);

  // Setup mailboxes
  fill_static_links(&my_inbox[0], sizeof(my_inbox)/sizeof(CasioMailBox));
  fill_static_links(&my_outbox[0], sizeof(my_outbox)/sizeof(CasioMailBox));
  casio_inboxes=&my_inbox[0];
  casio_outboxes=&my_outbox[0];
  casio_receive_hook=&hook_example;

  // Other initializations
  pinMode(LED_BUILTIN,OUTPUT);
  
}

bool wait_in_progress=false;
long wait_started=0;

int analog_pin=0;

void loop() {

  casio_poll();

  /* Try on your calculator:
   * RECEIVE(T)
   * then inspect T -- should show current timer.
   * Look for hook trace in serial monitor window.
   */
  BOX_MILLIS(millis());

  /* Try on your calculator:
   * RECEIVE(U)
   * then inspect U -- should show current timer in microseconds.
   */
  BOX_MICROS(micros());

  /* Try on your calculator:
   * 1->I:SEND(I)
   * This will setup Arduino to read analog value from analog pin 1.
   */
  if( BOX_INDEX.fresh ) {
    analog_pin=(int)BOX_INDEX.value;
    BOX_INDEX.fresh=false;
  }

  /*
   * Connect potentiometer to analog pin 1, 5 volts and ground. Set it to some
   * position. On your calculator execute
   * RECEIVE(V):V
   * and note the value. Then change the potentiometer position and execute
   * RECEIVE(V):V
   * once more. Note how the value has changed.
   */
  if( analog_pin>0 ) {
     BOX_VALUE(analogRead(analog_pin)); 
  }

  /* Try on your calculator:
   * 3000->W
   * SEND(W)
   * -- Calculator should remain on hold during SEND() for ~3000 ms before
   *    continuing. Also watch serial monitor window for feedback.
   * -- Note that mailbox for W is marked as not .immediate. This enables
   *    polling routine to keep calculator on hold while data is being
   *    processed/executed.
   */
  if( BOX_WAIT.fresh ) {
    if( !wait_in_progress ) {
      wait_in_progress=true;
      wait_started=millis();
    } else {
      if( millis()>wait_started+(long)BOX_WAIT.value ) {
        wait_in_progress=false;
        BOX_WAIT.fresh=false;
      }
    }
  }

  /* Try on your calculator:
   * 0→L
   * SEND(L)
   * -- Built-in LED should go OFF
   * 100→L
   * SEND(L)
   * -- Built-in LED should go full ON
   * 5→L
   * SEND(L)
   * -- Built-in LED should go dim, but still visible.
   */
  if( BOX_LED.fresh ) {
    analogWrite(LED_BUILTIN,constrain(map((int)BOX_LED.value,0,100,0,255),0,255));
   /*
    * Note that this mailbox is marked as .immediate, so technically, there is
    * no need to clear .fresh bit upon completion of request, but we still do
    * it anyway.
    */
    BOX_LED.fresh=false;
  }

  /* I have an irrational fear of busy waiting, so ... */
  delay(1);

}
