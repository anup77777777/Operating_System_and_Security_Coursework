# 🔐 Task 3 – Secure File Management System

## ST5004CEM – Operating Systems and Security Coursework

A secure file management system developed in **C** that demonstrates fundamental **Operating System** and **Security** concepts including user authentication, access control, file permissions, encryption, and audit logging.

---

# Project Structure

```text
Task3_File_System_and_Security/
│
├── task3.c                # Complete application source code
├── task3                  # Compiled executable
├── users.dat              # User database
├── filemeta.dat           # File metadata database
├── audit.log              # Security audit log
├── vault/                 # Secure file storage
│   ├── Notes
│   ├── Projects
│   ├── Todo
│   ├── Secret
│   ├── Meeting
│   ├── Reminders
│   ├── GroupMessage
│   ├── TopSecret
│   ├── ProjectInfo
│   ├── CollageChats
│   ├── CollageSchedule
│   └── GroupChats
│
└── README.md
```

---

# Compilation

Compile using GCC

```bash
gcc task3.c -o task3
```

or

```bash
gcc -Wall -Wextra task3.c -o task3
```

---

# Running the Program

```bash
./task3
```

On the first execution, the application automatically creates

- `vault/`
- `users.dat`
- `filemeta.dat`
- `audit.log`

It also creates the default administrator account.

---

# Default Administrator

| Username | Password | Group | Role |
|----------|----------|-------|------|
| admin | admin123 | admins | Administrator |

> **Note:** Change the default administrator password after the first login.

---

# Sample User Accounts

| Username | Password | Group | Role |
|----------|----------|----------------|------------|
| Anup | 1234 | Admin | Administrator |
| Anuj | 1234 | Friends | Standard User |
| Nabin | 1234 | Friends | Standard User |
| Nirmal | 1234 | Friends | Standard User |
| Ronan | 1234 | CollageGroup | Standard User |
| Manjil | 1234 | Collage | Standard User |

---

# Sample Files

| File | Owner | Group | Permission | Encrypted |
|------|-------|----------------|-----------|-----------|
| Notes | Anup | Admin | rw-r----- | No |
| Projects | Anup | Admin | rw-r----- | No |
| Todo | Anup | Admin | --x--x--- | No |
| Secret | Anup | Admin | rw-r----- | Yes |
| Meeting | Nirmal | Friends | rw-r----- | No |
| Reminders | Anuj | Friends | rw-r----- | Yes |
| GroupMessage | Nabin | Friends | rw-r----- | No |
| TopSecret | Nabin | Friends | r--r--r-- | No |
| ProjectInfo | Nabin | Admin | rw-r----- | Yes |
| CollageChats | Ronan | CollageGroup | rw-rw---- | No |
| CollageSchedule | Ronan | CollageGroup | rw-r----- | No |
| GroupChats | Manjil | Collage | rw-r----- | No |

---

# Main Menu

```text
==============================================================
           SECURE FILE VAULT SYSTEM
==============================================================

FILE OPERATIONS

1. List Files
2. Create File
3. Open / Read File
4. Write File
5. Delete File
6. Rename File
7. Change Permissions (chmod)
8. Append File

SECURITY

9. Encrypt File
10. Decrypt File
11. View Audit Log
12. Change Password

(Admin Only)

13. User Management

0. Logout

==============================================================
```

---

# Implemented Features

## File Operations

- Create files
- Read files
- Write files
- Append content
- Rename files
- Delete files
- File listing
- File metadata management

---

## User Authentication

- Secure login system
- User registration
- Password verification
- Password change
- Salted password hashing
- Iterative password hashing
- Administrator account
- User management

---

## Permission System

Unix-style permission model

```
Owner   Group   Others
rwx     rwx     rwx
```

Supported permissions

- Read
- Write
- Execute

Permission editing using chmod-style numeric values

Examples

| Mode | Permission |
|------|------------|
| 640 | rw-r----- |
| 660 | rw-rw---- |
| 750 | rwxr-x--- |
| 444 | r--r--r-- |

---

## Encryption

- XOR stream cipher
- Passphrase-based encryption
- File decryption
- Integrity verification
- Checksum validation

---

## Security Features

- Password authentication
- Salt generation
- Password hashing
- File encryption
- Access control
- Permission validation
- Filename sanitization
- Path traversal prevention

---

## Audit Logging

Every important operation is recorded in **audit.log** including

- Login
- Logout
- Registration
- File creation
- File reading
- File writing
- File deletion
- File rename
- Permission changes
- Encryption
- Decryption
- Password changes
- User management

Each log entry includes

- Timestamp
- Username
- Action
- Target File
- Result

---

# Data Storage

| File | Purpose |
|------|---------|
| users.dat | Stores user accounts |
| filemeta.dat | Stores ownership and permissions |
| audit.log | Security audit records |
| vault/ | Secure file storage |

---

# Operating System Concepts Demonstrated

- File Management
- User Authentication
- Access Control
- Unix File Permissions
- File Metadata
- Encryption
- Audit Logging
- Secure Password Storage
- Processed File I/O
- Terminal Interaction

---

# Technologies Used

- C Programming
- GCC Compiler
- Linux System Calls
- POSIX Terminal API
- ANSI Escape Sequences
- File I/O
- Unix Permissions

---

# Learning Outcomes

This project demonstrates practical implementation of

- Secure File Systems
- User Authentication
- Authorization
- Permission Management
- Encryption Techniques
- Integrity Checking
- Audit Logging
- Operating System Security Concepts

---

# Author

**Anup Neupane**

Student ID: **240586**

Module: **ST5004CEM – Operating Systems and Security**

Softwarica College of IT & E-Commerce

GitHub: https://github.com/anup77777777

---

# License

This project was developed solely for academic purposes as part of the **ST5004CEM – Operating Systems and Security** coursework.

© 2026 Anup Neupane. All Rights Reserved.
