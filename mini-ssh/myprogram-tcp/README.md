# MyFirstProgram over TCP

Run your own command-line program (here called **MyFirstProgram**) from another
computer-process over a TCP connection. You type a request in the client, it
goes over the network to the server, the server hands it to the program, and the
program's answer comes back.

```
client  --TCP-->  server  --pipe-->  MyFirstProgram (stdin)
client  <--TCP--  server  <--pipe--  MyFirstProgram (stdout)
```

Everything here runs on **127.0.0.1 only** and is meant for learning.

## The example program

`myfirstprogram.cpp` is a sample that matches what you described:

- `reken 7x7` → multiplies the two numbers and answers `7 x 7 = 49` (also accepts `7*7`).
- `Liedje` → prints one random song out of five.
- `exit` → stops.

Swap in your own `MyFirstProgram` if you like — just keep the contract below.

## ConPTY or plain pipes? → pipes

For this kind of program, **plain pipes are the right choice, not ConPTY.**

- ConPTY (the pseudo-console from `conpty-demo.cpp`) is for programs that expect
  a real *terminal*: a shell like `cmd.exe`, things with a moving cursor,
  colours, arrow keys. It wraps the output in terminal escape codes.
- `MyFirstProgram` is a simple "read a line, print an answer" program. It needs
  none of that. Sending its tidy request/answer protocol through ConPTY would
  only sprinkle terminal escape codes into the answers and make them harder to
  read. Plain pipes keep it clean.

So this demo uses the same pipe helper as the big mini-ssh `sessie` command
(`ChildProcess` from [`../pipes.h`](../pipes.h)).

## The contract your program must follow

The server talks to the program over its **standard input and output**. Your
program must:

1. **Read** one request line at a time from stdin (`std::getline`).
2. **Write** the answer to stdout. The answer may be several lines.
3. After each answer, print one line that is exactly `<<DONE>>`, so the server
   knows the answer is complete (same idea as `<<KLAAR>>` in the big mini-ssh).
4. **Flush** after each answer so it is sent immediately and does not get stuck
   in a buffer. `std::endl` flushes for you; plain `"\n"` may not.

That is all. Your program does not need to know anything about sockets or TCP —
the server handles the network.

## Safety

- The server starts **only** `MyFirstProgram`, by a fixed name. It never starts
  a shell (no `cmd.exe`, no `/bin/sh`), so a client can never run arbitrary
  commands on the machine. This is the same safe pattern as the `assistent`:
  a fixed program with fixed commands, not a raw shell.
- It listens on **127.0.0.1 only**. Never expose a program like this on a public
  port without real authentication and encryption; and never wire a real shell
  to a socket at all (that would be a "bind shell" — anyone who connects could
  run anything on your computer).

## Build and run

Build all three in this folder:

```
g++ -std=c++17 myfirstprogram.cpp -o myfirstprogram      # Linux
g++ -std=c++17 server.cpp         -o server
g++ -std=c++17 client.cpp         -o client
```

On Windows (MinGW), add `-lws2_32` to the server and client, and name the
outputs `.exe`:

```
g++ -std=c++17 myfirstprogram.cpp -o myfirstprogram.exe
g++ -std=c++17 server.cpp         -o server.exe -lws2_32
g++ -std=c++17 client.cpp         -o client.exe -lws2_32
```

Then, in one terminal start the server, and in another the client:

```
./server        # terminal 1  (it starts ./myfirstprogram itself)
./client        # terminal 2
```

Type `reken 7x7` or `Liedje` in the client and watch the answer come back over
the connection. Type `exit` to stop. (Port 2223; the big mini-ssh uses 2222, so
both can run at once.)
