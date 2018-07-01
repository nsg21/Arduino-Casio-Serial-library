#include "casio.h"
#include <stdlib.h>

MailBox my_inbox[]={
  MAILBOX('W',false) // request by a calc: number of milliseconds to delay
#define BOX_WAIT my_inbox[0]
  ,MAILBOX('L',true) // brightness of built-in LED (0..255)
#define BOX_LED my_inbox[1] // sent index for subsequent read
  ,MAILBOX('I',false) // number of milliseconds to rotate (negative=ccw)
#define BOX_INDEX my_inbox[2] // sent index for subsequent read
};

MailBox my_outbox[]={
  IMMEDIATE('T') // millisecond timer
#define BOX_MILLIS(v) POST_TO_BOX(my_outbox[0],v)
  ,IMMEDIATE('U') // microsecond timer
#define BOX_MICROS(v)  POST_TO_BOX(my_outbox[1],v)
  ,IMMEDIATE('V') // sensor value read from analog input 'I'
#define BOX_VALUE(v)    POST_TO_BOX(my_outbox[2],v)
};

void hook_example(char name) {
  Serial.print("[hook] :REQ for ");
  Serial.println(name);
}

void setup() {
  Serial.begin(9600);
  CalSerial.begin(9600);
  
  Serial.println("Listening on Serial3" );

  fill_static_links(&my_inbox[0], sizeof(my_inbox)/sizeof(MailBox));
  fill_static_links(&my_outbox[0], sizeof(my_outbox)/sizeof(MailBox));
  cccp_inboxes=&my_inbox[0];
  cccp_outboxes=&my_outbox[0];
  cccp_hook_receive=&hook_example;
  pinMode(LED_BUILTIN,OUTPUT);
  Serial.println("Finished setup" );
}

long lastbeat=0;

bool wait_in_progress=false;
long wait_started=0;

void loop() {
  long timer=millis();
  // Hartbeat indicator
  if(timer>lastbeat+10000) {
    lastbeat=timer;
    Serial.print("Timer=");
    Serial.println(timer);
    Serial.print("Mailbox Timer=");
    Serial.println(my_outbox[0].value);
  }

  casio_poll();

  /* Try on your calculator:
   * RECEIVE(T)
   * then incpect T -- should show current timer.
   * Look for hook trace in serial monitor window.
   */
  BOX_MILLIS(millis());

  /* Try on your calculator:
   * RECEIVE(U)
   * then incpect U -- should show current timer in microseconds.
   */
  BOX_MICROS(micros());

  if( BOX_INDEX.fresh ) {
    Serial.print("Requested index ");
    Serial.println(BOX_INDEX.value);
    BOX_INDEX.fresh=false;
  }

  /* Try on your calculator:
   * 3000→W
   * SEND(W)
   * -- Calculator should remain on hold during SEND() for ~3000 ms before
   *    continuing. Also watch serial monitor window for feedback.
   * -- Note that mailbox for W is marked as not .immediate. This enables
   *    polling routine to keep calculator on hold while data is being
   *    processed/executed.
   */
  if( BOX_WAIT.fresh ) {
    if( !wait_in_progress ) {
      Serial.print("Received WAIT request for ");
      Serial.print(BOX_WAIT.value);
      wait_in_progress=true;
      wait_started=millis();
      Serial.print(" at ");
      Serial.println(wait_started);
      Serial.print("Estimated completion at ");
      Serial.println(wait_started+(long)BOX_WAIT.value);
    } else {
      if( millis()>wait_started+(long)BOX_WAIT.value ) {
        wait_in_progress=false;
        BOX_WAIT.fresh=false;
        Serial.print("completed WAIT request at ");
        Serial.println(millis());
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
