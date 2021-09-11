# CPP Backup Server For Files over the socket interface.
## Homework Maman #14 of "defensive systems programming", Open Uni

main.cpp: Main code that runs the server.
server.h: Header file for main.
client.py: Python implementation of a client.
backup.info: Names of files for backup and other operations. (For client use).
server.info: IP and Port of the server. (For client use).

The server supports the following operations:
- Backup files.
- Retrieve files.
- Erase files.
- Send Directory list.

Each client has its own directory on the server.
