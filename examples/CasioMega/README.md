# Example of communication between Arduino Mega and Casio CFX 9850G programmable calculator

Any older model programmable Casio calculator with serial port should work.

**WARNING**:
Make sure that the calculator's serial port is 5v, otherwise you may damage
your calculator.

## Hardware  setup

Wire a male 2.5mm barrel plug to your Arduino Mega.

 - *ring* terminal to  to pin 14 (Serial3 TX)
 - *tip* terminal to pin 15 (Serial3 RX)
 - *base* terminal of to pin GND (ground)

Plug 2.5mm barrel plug in the calculator's communication port.

Optionally, wire one or more potentiometers or sensors to analog pins.

## Calculator commands

I assume you know your way around Casio operating system and Casio Basic and
give only minimal details.

In this example we issue commands in calculator's immediate execution mode
(that is, normal "calculator" mode). Make sure you switch to "Linear" display
-- in their infinite wisdom Casio made SEND and RECEIVE commands not available
in "Math" mode.

Check the Arduino IDE serial console if available, as it provides extra
indication of what is going on.

### Timer

```Casio Basic
RECEIVE(T):T
```
Shows time in milliseconds since Arduino is on.

### High resolution timer

```Casio Basic
RECEIVE(U):U
```
Shows time in microseconds since Arduino is on.

### LED control

```Casio Basic
0→L
SEND(L)
```

Built-in LED should goes OFF

```Casio Basic
100→L
SEND(L)
```

Built-in LED should goes full ON

```Casio Basic
5→L
SEND(L)
```

Built-in LED should goes dim, but still visible.

### Delay

```Casio Basic
6000→W:SEND(W)
```

Note that it takes approximately 6 second to execute this command. This is an
example of *non-immediate* mailbox which keeps calculator on hold until
commands completes.

### Analog read

```Casio Basic
1→I:SEND(I)
```

Select Analog 1 pin to read value next.

```Casio Basic
RECEIVE(V):V
```
Request and display the value from analog pin (in this example, Analog 1).

## Copyright

Copyright (C) 2018 nsg21. All rights reserved.

## License

Apache-2.0
see `LICENSE.txt`

