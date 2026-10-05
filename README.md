# Linux-Based Secure File Transfer and System Resource Monitoring System

## Overview

This project is a Linux-based client-server application developed using C++17 and POSIX system programming.

The system provides secure file transfer between a client and a multithreaded server while also monitoring Linux system resources such as CPU usage, memory usage, and disk usage.

The project demonstrates practical concepts from:

- Linux system programming
- C++ programming
- Computer networks
- TCP/IP socket programming
- Multithreading
- File handling
- Process and system resource monitoring
- Data integrity verification
- Basic security mechanisms

---

## Objectives

The main objectives of this project are:

1. Implement TCP client-server communication in C++.
2. Develop a multithreaded Linux server.
3. Implement client authentication.
4. Provide file upload and download functionality.
5. Verify transferred files using SHA-256.
6. Prevent basic path traversal attacks.
7. Monitor Linux CPU, memory, and disk resources.
8. Demonstrate practical POSIX/Linux system programming.

---

## System Architecture

```text
                    Linux System
                         |
          +--------------+--------------+
          |                             |
          v                             v
  System Resource Monitor        Secure File Server
          |                             |
          |                       TCP Socket :8080
          |                             |
          |                    +--------+--------+
          |                    |                 |
          |                    v                 v
       /proc                 Client 1         Client 2
          |                    |                 |
          |                    +--------+--------+
          |                             |
          v                             v
 CPU / RAM / Disk                  Authentication
                                      |
                                      v
                              LIST / PUT / GET
                                      |
                                      v
                               SHA-256 Integrity
                                      |
                                      v

                                  storage/
