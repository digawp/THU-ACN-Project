# File transfer program for THU course Advanced Computer Network

## Requirements:

- boost (1.55.0 is used when writing the code)
- c++11-compliant compiler (g++ is used)

## Command to compile:

    g++ <src-file> -lboost_system -lboost_filesystem -lpthread -std=c++11

## About the project

The requirement of the project is to write a file transfer program to send files over an ethernet cable as fast as possible.

The mechanism must be as such:

1. Client sends a request to the server.
2. Server sends the file to the client as the response.
3. Client terminates upon receipt of the last file.

Unix command `time` will be used to measure the time taken by the client to receive the file. Specifically, summation of user and system time is used as the time taken.

## Notes

Used to be 2 separate repo but merged manually, which explains the rather odd commit history.

