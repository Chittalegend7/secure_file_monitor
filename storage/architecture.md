# System Architecture

## Linux-Based Secure File Transfer and System Resource Monitoring System Using C++

## 1. Architecture Overview

The project follows a **client-server architecture** implemented using C++ and Linux/POSIX system programming.

The system consists of:

1. **C++ Client**
2. **Multithreaded C++ Server**
3. **Server Storage**
4. **Linux System Resource Monitor**

The client and server communicate using **TCP sockets on port 8080**. The server handles multiple clients using separate threads.

---

## 2. High-Level Architecture

```text
                    ┌──────────────────────────────┐
                    │         C++ CLIENT           │
                    │                              │
                    │  • Authentication            │
                    │  • LIST                       │
                    │  • PUT (Upload)               │
                    │  • GET (Download)             │
                    │  • QUIT                       │
                    └──────────────┬───────────────┘
                                   │
                                   │ TCP/IP
                                   │ Port 8080
                                   ▼
                    ┌──────────────────────────────┐
                    │    MULTITHREADED C++ SERVER  │
                    │                              │
                    │  • Client Authentication     │
                    │  • Command Processing        │
                    │  • File Transfer             │
                    │  • Filename Validation       │
                    │  • Path Traversal Protection │
                    │  • SHA-256 Integrity Check   │
                    │  • Client Threads            │
                    └──────────────┬───────────────┘
                                   │
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │          storage/             │
                    │                              │
                    │      Server-side Files       │
                    └──────────────────────────────┘


                    ┌──────────────────────────────┐
                    │   LINUX SYSTEM MONITOR      │
                    │                              │
                    │  /proc/stat                  │
                    │       ↓                      │
                    │  CPU Usage                   │
                    │                              │
                    │  /proc/meminfo               │
                    │       ↓                      │
                    │  RAM Usage                   │
                    │                              │
                    │  statvfs()                   │
                    │       ↓                      │
                    │  Disk Usage                  │
                    └──────────────────────────────┘
```

---

## 3. Client Architecture

The client is implemented in:

```text
client/client.cpp
```

The client performs the following operations:

### Authentication

The client connects to the server and sends the username and password.

Demo credentials:

```text
Username: admin
Password: admin123
```

After successful authentication, the client displays the available commands.

### File Operations

The client supports:

```text
1. LIST
2. PUT
3. GET
4. QUIT
```

### Upload

For an upload, the client:

1. Selects a local file.
2. Calculates its SHA-256 hash.
3. Sends the filename, file size, and hash to the server.
4. Waits for the server's `READY` response.
5. Sends the file in chunks.
6. Receives the server's integrity result.

### Download

For a download, the client:

1. Requests a file using `GET`.
2. Receives the file size and SHA-256 hash.
3. Sends `READY`.
4. Receives the file in chunks.
5. Saves the file locally.
6. Calculates its local SHA-256 hash.
7. Compares the hashes.

---

## 4. Server Architecture

The server is implemented in:

```text
server/server.cpp
```

The server:

1. Creates a TCP socket.
2. Binds it to port `8080`.
3. Listens for incoming connections.
4. Accepts clients.
5. Creates a separate thread for each client.
6. Authenticates the client.
7. Processes commands.
8. Performs file operations.
9. Verifies file integrity.

### Multithreading

Each connected client is handled by a separate C++ thread.

```text
Client 1 ──────► Thread 1
Client 2 ──────► Thread 2
Client 3 ──────► Thread 3
                   │
                   ▼
              C++ Server
```

This allows multiple clients to communicate with the server without requiring a separate server process for every client.

---

## 5. File Storage Architecture

Uploaded files are stored inside:

```text
storage/
```

The server does not directly accept arbitrary filesystem paths.

Instead, filenames are validated before file operations are performed.

The server rejects filenames containing:

```text
..
/
\
```

This provides protection against basic **path traversal attacks**.

---

## 6. File Upload Architecture

```text
┌──────────────┐
│    Client    │
└──────┬───────┘
       │
       │ Calculate SHA-256
       ▼
┌────────────────────┐
│ Filename + Size +  │
│ SHA-256 Hash       │
└─────────┬──────────┘
          │
          │ PUT
          ▼
┌────────────────────┐
│      Server        │
│ Validate filename  │
│ Validate file size │
└─────────┬──────────┘
          │
          │ READY
          ▼
┌────────────────────┐
│ Send file chunks   │
└─────────┬──────────┘
          │
          ▼
┌────────────────────┐
│ Server calculates  │
│ SHA-256            │
└─────────┬──────────┘
          │
          ▼
     Compare hashes
       /       \
     Match    Mismatch
       │         │
       ▼         ▼
   Store file   Reject
   in storage/
```

The file is stored only after the calculated hash matches the expected hash.

---

## 7. File Download Architecture

```text
┌──────────────┐
│    Client    │
└──────┬───────┘
       │
       │ GET filename
       ▼
┌────────────────────┐
│      Server        │
│ Check file exists  │
└─────────┬──────────┘
          │
          │ Size + SHA-256
          ▼
┌────────────────────┐
│      Client        │
│ Sends READY        │
└─────────┬──────────┘
          │
          │ File chunks
          ▼
┌────────────────────┐
│ Local downloaded_  │
│ file               │
└─────────┬──────────┘
          │
          ▼
     Calculate SHA-256
          │
          ▼
    Compare with server
```

A matching SHA-256 value confirms that the downloaded file has the same content as the server file.

---

## 8. Linux System Resource Monitor

The monitoring component is implemented in:

```text
monitor/monitor.cpp
```

It uses Linux system interfaces instead of external monitoring libraries.

### CPU Monitoring

The monitor reads:

```text
/proc/stat
```

CPU statistics are collected at two different times and used to calculate CPU utilization.

### Memory Monitoring

The monitor reads:

```text
/proc/meminfo
```

It uses the available memory information to calculate RAM usage.

### Disk Monitoring

The monitor uses the POSIX function:

```cpp
statvfs()
```

to obtain filesystem capacity and available space.

### Monitoring Interval

The monitor refreshes its information approximately every:

```text
2 seconds
```

---

## 9. Security Architecture

The project implements several security-related mechanisms.

### Authentication

Clients must provide valid credentials before accessing file operations.

### SHA-256 Integrity Verification

SHA-256 is used to verify that files are not altered during transfer.

### Path Traversal Protection

The server validates filenames and rejects unsafe path patterns.

### File Size Limitation

Uploaded files are limited to:

```text
100 MB
```

This prevents unnecessarily large transfers from consuming excessive server resources.

### Controlled Storage

Files are stored inside the dedicated:

```text
storage/
```

directory.

---

## 10. Technology Architecture

| Layer                | Technology          |
| -------------------- | ------------------- |
| Programming Language | C++17               |
| Operating System     | Linux / WSL2 Ubuntu |
| Networking           | TCP/IP              |
| Network API          | POSIX Sockets       |
| Concurrency          | `std::thread`       |
| Hashing              | OpenSSL SHA-256     |
| CPU Monitoring       | `/proc/stat`        |
| Memory Monitoring    | `/proc/meminfo`     |
| Disk Monitoring      | `statvfs()`         |
| Build System         | Makefile            |
| Version Control      | Git                 |

---

## 11. Project Directory Architecture

```text
secure_file_monitor/
│
├── client/
│   └── client.cpp
│
├── server/
│   └── server.cpp
│
├── monitor/
│   └── monitor.cpp
│
├── storage/
│   └── architecture.md
│
├── docs/
│   └── screenshots/
│
├── Makefile
├── README.md
└── .gitignore
```

> Note: Git does not track empty directories. The `storage/` directory is included in the repository because it contains the architecture documentation/file.

---

## 12. Overall Data Flow

```text
Client
  │
  │ Authentication
  ▼
Server
  │
  ├── LIST ───────────────► File List
  │
  ├── PUT ────────────────► Validate → Receive → Hash → Store
  │
  ├── GET ◄─────────────── Receive Request → Hash → Send File
  │
  └── QUIT
```

At the same time, the Linux monitoring component independently reads system-level information:

```text
Linux Kernel Interfaces
        │
        ├── /proc/stat
        │       ↓
        │     CPU
        │
        ├── /proc/meminfo
        │       ↓
        │     RAM
        │
        └── statvfs()
                ↓
              Disk
```

---

## 13. Important Security Limitation

This project provides a **secure file transfer prototype**, but the TCP connection itself is **not encrypted with TLS**.

Therefore:

```text
Authentication ≠ Encryption
SHA-256 Integrity ≠ Encryption
```

The current implementation focuses on authentication, file integrity, input validation, and controlled storage.

**TLS/SSL encrypted communication is planned as a future enhancement.**

---

## 14. Future Architecture Enhancements

Future versions can include:

* TLS/SSL encrypted communication
* Secure external credential storage
* Role-based access control
* User registration
* Database-backed user management
* Detailed server logging
* File permissions
* Transfer progress indicators
* Network monitoring
* Process monitoring
* Web-based management interface
* Encrypted file storage

---

## 15. Conclusion

The architecture combines **Linux system programming, C++ networking, multithreading, file handling, security concepts, and system resource monitoring** into a single practical application.

The project demonstrates how a Linux-based C++ application can provide controlled file transfer while also monitoring important system resources such as CPU, RAM, and disk utilization.
