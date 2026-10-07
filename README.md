# IE3010 Network Programming - NetMessenger

## Project Overview

NetMessenger is a multi-client chat and file-sharing application developed for the IE3010 Network Programming assignment.

The application is implemented using the C programming language and communicates through TCP/IP sockets. The server can handle multiple clients at the same time by creating a separate POSIX thread for each connected client.

The main features included in the system are:

* User registration and online user listing
* Broadcast messaging
* Private messaging
* Chat room creation and participation
* Room-based messaging
* File sharing
* Basic error handling
* Graceful client disconnection
* Server-side activity logging


## Student and System Details

* Registration Number - IT23595330
* Last Four Digits    - 5330
* Server Port         - 11330
* Node ID (NID)       - NID:5953
* Server Source       - server_5330.c
* Client Source       - client_5330.c
* Makefile            - Makefile_5330
* Log File            - netmsg_IT23595330.log
* Storage Directory   - ./storage/IT23595330/

The server port is calculated using:

`6000 + 5330 = 11330`

The NID is generated using digits 3-6 of the numeric part of the registration number.


## Requirements

The application is intended to run in a Linux environment with the following:

* GCC compiler
* POSIX threads
* TCP/IP socket support
* Make utility
* Standard Linux command-line tools


## Files

server_5330.c   -    Server program
client_5330.c   -    Client program
Makefile_5330   -    Build and execution commands
netmsg_IT23595330.log - Server log file
storage/        -    Directory used for received files


## How to Compile

Open a terminal inside the project directory and run:

`make -f Makefile_5330`

This command compiles both the server and client programs.

After a successful compilation, the following executable files will be generated:

`server_5330`
`client_5330`

To compile only the server:

`make -f Makefile_5330 server`

To compile only the client:

`make -f Makefile_5330 client`


## How to Run

### 1. Start the Server

Run the following command:

`./server_5330 11330`

The server will start listening for TCP connections on port `11330`.

The server can also be started using:

`make -f Makefile_5330 run-server`


### 2. Start a Client

Open another terminal and run:

`./client_5330 127.0.0.1 11330`

Alternatively, the client can be started using:

`make -f Makefile_5330 run-client`


## Main Commands

Once a client is connected to the server, the following commands can be used.

### Register

```text
REGISTER <username>
