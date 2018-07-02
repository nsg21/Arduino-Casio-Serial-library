# Host for Casio Basic SEND/RECEIVE operators.

This library enables use of Casio graphing calculators with serial interface
capabilities as a simple user interface/data storage device for Arduino
controllers. It implements Casio Basic serial communication protocol which
allows the use of Casio Basic to define logic of data presentation and storage.

## Why Casio

Casio calculators use simple TTL UART serial for communication. Older models
are 5V and can be connected directly to 5V Arduino Boards. Newer models (e.g.
fx-CG20 Prizm) are 3.3 volts. They feature same exact connector and people
would not think twice about connecting them with their earlier siblings. I hope
Casio engineers did the right thing and made newer models 5V tolerant. Having
said that, in my design I use logic level converter to connect 3.3V Prizm to 5V
Mega, just to be on a safe side.

Physical connector is a standard 2.5 mm ("microphone") jack. 

Casio graphing calculators are easy to come by and *pre-owned* older models are
often very inexpensive.

Casio Basic, while far from being state-of-the-art IDE, still provides enough
flexibility to design simple interfaces. Minor changes and adjustments can be
made on the spot and without PC.

## What is implemented

Casio Basic features 2 statements, SEND(*var*) and RECEIVE(*var*). *Var* can be
a named scalar variable, numbered list, named matrix, or numbered picture.

This library implements operator variants that work with named scalar variables
only.

From the calculator's point of view, both `SEND()` and `RECEIVE()` are requests to
a host (server) to save and retrieve given named value respectively.

Arduino acts as such host. It waits for either type of request and when any of
them arrives, it processes it accordingly. It takes a value sent by `SEND()`
and acts on it, or generates a value for a name requested by `RECEIVE()` and
sends it back to a calculator.

Typically, `SEND()` can be used to activate actuators and `RECEIVE()` can be
used to retrieve sensor values.

## Mailboxes

The library interfaces with the rest of controller software via *mailboxes*.
Mailboxes are data structures that hold a name (1 character "alpha-variable" of
Casio Basic), a value and some flags. There are 2 flavors of mailboxes: inboxes
and outboxes. Each of those can be *immediate* or not.

Outboxes hold data ready to be sent in response to `RECEIVE()` request by a
calculator. Inboxes is where the data sent by a calculator via `SEND()` go.

Inboxes and Outboxes are organized in linked lists with a `.next` field. The
pointer to the head of inboxes list is called `casio_inboxes`, the pointer to
the head of outboxes is called `casio_outboxes` and must be assigned in the
`setup()`.

It is possible to have both inbox and outbox for the same name. They are
completly independent.

### Inbox

Incoming variables (sent by SEND()) are delivered to inboxes.

When library gets a valid SEND() request, it looks for a mailbox associated
with then name in the request. If there is such a mailbox, the library writes a
value to its `.value` field and sets its `.fresh` flag.

If the inbox is *immediate* (its `.immediate` flag is `true`), the library
proceeds with the protocol, confirms the reception of the value to the
calculator, the `SEND()` operator successfully completes and the Casio Basic
program proceeds in normal fashion.  It is up to the rest of the controller
software to act upon or ignore the value in `.value` field of a mailbox.

If the inbox is *not* *immediate* (`.immediate` flag is `false`), the library
will **not** confirm the reception of the value until the rest of control
software processes it and clears the `.fresh` flag of the mailbox. The
calculator will wait for the confirmation indefinitely, so it is really
important that control software properly communicates when it is finished
processing that request.

This flavor can be used to implement actions that take time to complete, like
driving certain distance. For example if `25→D:SEND(D)` initiates a movement,
the Basic program will be stuck on `SEND()` until control
software indicates that it moved the target 25 units. It may take a few seconds
or a few hours -- `SEND()` will wait patiently for confirmation.

### Outbox

Values for variables requsted by `RECEIVE()` are sought in
outboxes.

Control software in its main loop may check sensors and populate appropriate mailboxes' `.value` field with the sensor value.

When a calculator requests a value with a `RECEIVE()` operator, *immediate*
outbox will provide a value right away, whether its `.fresh` flag set or not.
Outbox without `.immediate` flag will keep calculator on hold until `.fresh`
flag goes `true`. After that, the value is sent to the calculator, and the
`.fresh` flag is cleared again.

The poll function calls a hook (`*casio_receive_hook`) immediately after
initial `RECEIVE()` handshake.
The control software may use it to initiate a process of obtaining a value
for that name.




## API

### `HardwareSerial *casio_serial`

Physical serial interface which is attached to Casio communication.
It can be `&Serial` on arduinos without extra serial interfaces, but extreme
care should be taken to ensure that nothing else, especially debug messages
are transmitted over it.

For boards with multiple uarts, it can be `&Serial1` ... `&Serial3`, which is
preferrable.

This global must be assigned in `setup()`

### `CasioMailBox`

Data structure that holds mailbox information

```c
typedef struct casiomailbox {
  char name; /* 1-character name of Casio Basic variable */
  bool immediate; /* immediate flag */
  bool fresh; /* freshness indicator */
  double value;
  struct casiomailbox *next; /* link field for linked list */
} CasioMailBox;
```

The easiest strategy is to have fixed number of inboxes and outboxes,
permanently assigned to certain process variables and commands, periodically
updated/checked by control sofware.

### `CasioMailBox *casio_inboxes`
### `CasioMailBox *casio_outboxes`

Pointers to the heads of linked lists containing inboxes and outboxes
respectively. Must be assigned in `setup()`.


### `fill_static_links(&my_inbox[0], my_inbox_count);`
Internal function
`get_mailbox()` relies on linked list fields to find appropriate box.

If mailboxes are allocated in static arrays, their link fields need to be
initialized with `fill_static_links(...)`, e.g.:
```c
CasioMailBox my_inbox[]={
  {name:'A',immediate:true},
...
};
...
fill_static_links(&my_inbox[0], sizeof(my_inbox)/sizeof(CasioMailBox));
fill_static_links(&my_outbox[0], sizeof(my_outbox)/sizeof(CasioMailBox));
```
The function simply sets `.next` of each mailbox to the next element in the
array, last one points to `NULL`.

### `void casio_poll(void);`

This function implements serial protocols for `SEND()` and `RECEIVE()`
operators. It listens for incoming bytes, interprets them, populates inboxes
with the incoming values and uses values in
outboxes to respond to variable requests.

I took extra care to make it as non-blocking as possible. If it has to wait
for the calculator's response, it returns. When called next time, it resumes
protocol from the previous point once the bytes from the calculator have
arrived.

It should be called periodically, e.g. in the `loop()` with reasonable
frequency. In the early phases of protocol calculator is sensitive to timeout,
so it is important to send back prompt initial response.

If there are parts of the control software that may block execution for a
while, it is worth considering calling this function from serial interrupt.


### `void (*casio_receive_hook)(char);`

This hook is called each time `casio_poll()` sees a `RECEIVE()` request from a
calculator. It gets the name of the requested variable as its first
parameter.

## Physical connection

### Using Standard Casio Crossover cable

Attach *ring* terminal of a female 2.5 mm socket to RX pin (possibly through
logic level voltage converter) and *tip* terminal to the TX pin. Attach
*base* to ground.

Connect this socket too your calculator using standard Casio serial cable
("crossover" cable).

### Custom built cable

Make a cable with a male 2.5 mm TRS plug on one end and whatever is convenient
for your design on the other end. Attach *ring* terminal of a male 2.5 mm plug
to TX pin, *tip* terminal to RX pin and *base* to ground. Note that connections
of *ring* and *tip* are switched compared to wiring of a female connector.

## Examples

Please see the examples.

## Copyright

Copyright (C) 2018 nsg21. All rights reserved.

## Acknoledgements

I would like to thank Michael Fenton for describing Casio host protocol in
"Connecting the PICAXE 08M and PICAXE 18X to the Casio 9750G Plus graphics
calculator"

## License

Apache-2.0
see `LICENSE.txt`

