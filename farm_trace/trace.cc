/**
 * File: trace.cc
 * ----------------
 * Presents the implementation of the trace program, which traces the execution of another
 * program and prints out information about every single system call it makes.  For each system call,
 * trace prints:
 *
 *    + the name of the system call,
 *    + the values of all of its arguments, and
 *    + the system call return value
 */

#include <cassert>
#include <iostream>
#include <map>
#include <unistd.h> // for fork, execvp
#include <string.h> // for memchr, strerror
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/wait.h>
#include "trace-options.h"
#include "trace-error-constants.h"
#include "trace-system-calls.h"
#include "trace-exception.h"
#include "fork-utils.h" // this has to be the last #include statement in this file
using namespace std;

static std::map<int, std::string> syscallNums;
static std::map<std::string, systemCallSignature> syscallSigs;
static std::map<int, std::string> errConsts;
static const int REG[] = {RDI, RSI, RDX, R10, R8, R9}; //array of registers


//This helper function reads a string
static string readString(pid_t pid, unsigned long addr) {
    string str;
    size_t numBytesRead = 0;
    bool isEnd = false;
    
    while (!isEnd) {
        //get the 8 bytes residing in the address
        long ret = ptrace(PTRACE_PEEKDATA, pid, addr + numBytesRead);
        void *ptr = &ret;

        //check if there is a null terminator
        for (unsigned int i = 0; i < sizeof(long); i++) {
            char ch = *((char *)ptr + i);
            if (ch == '\0') {
                isEnd = true;
                break;
            }
            str.push_back(ch);
        }
        numBytesRead += sizeof(long);
    }
    return str;
}

//This helper function prints the signature of a system call for the full mode
void printSig(pid_t pid, systemCallSignature sig) {
    if (sig.size() == 0) {
        cout << "<signature-information-missing>";
        return;
    }
    for(unsigned int i = 0; i < sig.size(); i++) {
        //extract value in REG[i]
        long ret = ptrace(PTRACE_PEEKUSER, pid, REG[i] * sizeof(long));

        //print the signature according to its type
        enum scParamType s = sig[i];
        switch(s) {
            case SYSCALL_INTEGER:
                {
                    cout << (int)ret;
                    break;
                }
            case SYSCALL_STRING:
                {
                    string str = readString(pid, (unsigned long)ret);
                    cout << '"' << str << '"';
                    break;
                }
            case SYSCALL_POINTER:
                {
                    if (ret == 0) {
                        cout << "NULL";
                    } else {
                        cout << (long *)ret;
                    }
                    break;
                }
            case SYSCALL_UNKNOWN_TYPE:
                {
                    cout << "<unknown>";
                }
        }       
        if (i != sig.size() - 1) {
            cout << ", ";
        }
    }    
}

//This helper function enters a system call
std::string printSyscall(pid_t pid, bool simple, int& exitNum) {
    //get the system call name
    int num = ptrace(PTRACE_PEEKUSER, pid, ORIG_RAX * sizeof(long));
    string name = syscallNums[num];

    //set the exit number if the name is exit_group
    if (name == "exit_group") {
        exitNum = ptrace(PTRACE_PEEKUSER, pid, RDI * sizeof(long));        
    }
    
    if (simple) { //handles simple mode
        cout << "syscall(" << num << ") = " << flush;
    } else { //handles full mode
        cout << name << "(";
        systemCallSignature sig = syscallSigs[name];
        printSig(pid, sig);
        cout << ") = ";
    }
    return name;
}

//This helper function ends a system call
void endSyscall(pid_t pid, string name, bool simple) {

    //extract the return value
    long ret = ptrace(PTRACE_PEEKUSER, pid, RAX * sizeof(long));
    
    if (simple) { //handles simple mode
        cout << ret << endl;
    } else { // handles full mode
        if (ret < 0) { //handles errors
            cout << -1;
            cout << " " << errConsts[abs(ret)];
            cout << " (" << strerror(abs(ret)) << ")" << endl;
        }
        else {
            if (name == "brk" || name == "mmap") { //handles brk and mmap
                cout << (long *)ret << endl;
            } else {
                cout << ret << endl;
            }
        }
    }
}      

//This helper function traces system calls from detection to exit
int receiveSyscall(pid_t pid, bool simple) {
    int exitNum = 0;
    int status;
    string name;
    
    while (true) {
        
        //detect and enter a system call
        while (true) {
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            waitpid(pid, &status, 0);
            if (WIFSTOPPED(status) && (WSTOPSIG(status) == (SIGTRAP | 0x80))) {
                name = printSyscall(pid, simple, exitNum);
                break;
            }
        }
        
        //exit a system call
        while (true) {
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            waitpid(pid, &status, 0);
            
            //check if the tracee truly exits
            if (WIFEXITED(status)) {
                return exitNum;
            }
            if (WIFSTOPPED(status) && (WSTOPSIG(status) == (SIGTRAP | 0x80))) {
                endSyscall(pid, name, simple);           
                break;
            }
        }
    }
    return exitNum;
}
        
int main(int argc, char *argv[]) {
    //before tracing
    bool simple = false, rebuild = false;
    int numFlags = processCommandLineFlags(simple, rebuild, argv);
    if (argc - numFlags == 1) {
        cout << "Nothing to trace... exiting." << endl;
        return 0;
    }

    //convert systemcall opcodes to names
    compileSystemCallData(syscallNums, syscallSigs, rebuild);

    //convert errors to strings
    compileSystemCallErrorStrings(errConsts);

    //child process starts
    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME);
        raise(SIGSTOP);
        execvp(argv[numFlags + 1], argv + numFlags + 1);
        return 0;
    }

    int status;
    waitpid(pid, &status, 0);
    assert(WIFSTOPPED(status));
    ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

    //receive system calls
    int exitStatus = receiveSyscall(pid, simple);
    
    //end of tracing
    cout << "<no return>" << endl;
    cout << "Program exited normally with status " << exitStatus << "." << endl;

    return 0;
}

