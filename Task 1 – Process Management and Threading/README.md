# Operating System and Security Coursework

## ST5004CEM - Operating Systems and Security

### Task 1: Process Management and Threading

This project is developed as part of the **ST5004CEM Operating Systems and Security** module.

The application demonstrates the core concepts of operating systems including process management, multithreading, synchronization, process scheduling, race condition handling, and deadlock prevention using the C programming language on Ubuntu Linux.

---

## Features

- Process creation using `fork()`
- Multi-threading using POSIX Threads (`pthread`)
- Three concurrent worker threads
- Round Robin Scheduler simulation
- Shared resource synchronization using Mutexes
- Condition Variables for thread coordination
- Race condition prevention
- Deadlock prevention using fixed lock ordering
- Interactive terminal dashboard
- Parent and Child process management
- Real-time execution visualization

---

## Technologies Used

- C Programming Language
- GCC Compiler
- POSIX Threads (pthread)
- Linux / Ubuntu
- Git & GitHub

---

## Project Structure

```
Task 1 – Process Management and Threading
│
├── task1.c
├── screenshots/
├── README.md
└── Documentation.pdf
```

---

## Compilation

Compile using GCC:

```bash
gcc task1.c -o task1 -lpthread
```

---

## Execution

Run the program:

```bash
./task1
```

---

## Assignment Requirements Covered

| Requirement | Status |
|------------|--------|
| Process Creation (`fork`) | ✅ |
| Minimum 3 Threads | ✅ |
| Thread Synchronization | ✅ |
| Round Robin Scheduling | ✅ |
| Race Condition Handling | ✅ |
| Deadlock Prevention | ✅ |

---

## Sample Output

```
Forking supervisor process...

Parent Process Created

Child Process Running

Thread-0 Running

Thread-1 Running

Thread-2 Running

Round Robin Scheduler Active

Shared Counter Updated

Simulation Completed Successfully
```

---

## Learning Outcomes

This project demonstrates:

- Process Management
- Thread Management
- CPU Scheduling
- Synchronization
- Critical Sections
- Race Condition Prevention
- Deadlock Prevention
- Operating System Concepts

---

## Author

**Anup Neupane**

GitHub: https://github.com/anup77777777

---

## License

This project is submitted for academic purposes as coursework for the ST5004CEM module.
