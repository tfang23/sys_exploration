/**
 * File: stsh.cc
 * -------------
 * Defines the entry point of the stsh executable.
 */

#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>  // for fork
#include <csignal>  // for kill
#include <sys/wait.h>
#include "stsh.h"
#include "stsh-parser/stsh-readline.h"
#include "stsh-parser/stsh-parse-exception.h"
#include "stsh-exception.h"
#include "stsh-job-list.h"
#include "stsh-job.h"
#include "stsh-process.h"
#include "fork-utils.h" // this needs to be the last #include in the list

using namespace std;
STSHJobList joblist;
//global variable of signals to block
sigset_t sigs;


/**
 * Configure signal handling behavior for the shell. You can block or defer any
 * signals here.
 */
void STSHShell::configureSignals() {
    signal(SIGQUIT, [](int sig) { exit(0); });
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTSTP);
    sigprocmask(SIG_BLOCK, &sigs, NULL);
}

//handles child process signals
void STSHShell:: waitChildSigs() {
    while (true) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED);   
        if (pid <= 0) break;
        
        STSHJob& job = joblist.getJobWithProcess(pid);

        //handles different signals
        if (joblist.containsProcess(pid)) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                job.getProcess(pid).setState(kTerminated);
            } else if (WIFSTOPPED(status)) {
                job.getProcess(pid).setState(kStopped);
            } else if (WIFCONTINUED(status)) {
                job.getProcess(pid).setState(kRunning);
            }
         }
        //syncrhonizes
        joblist.synchronize(job);
    }
}

//helper function to check whether a token is valid
bool STSHShell::isValid(char *token) {
    char *temp = token;
    while (*temp != '\0') {
        if (!isdigit(*temp)) return false;
        temp += 1;
    }
    return true;
}

//helper function to set sigmask
void STSHShell::setSigmask(sigset_t& addSigs) {
    sigaddset(&addSigs, SIGINT);
    sigaddset(&addSigs, SIGTSTP);
    sigaddset(&addSigs, SIGCHLD);
    sigaddset(&addSigs, SIGCONT);
    sigprocmask(SIG_BLOCK, &addSigs, &sigs);
}

//handles slay, halt, and cont
void STSHShell::handleSlayHaltCont(const std::string& name, const char *const arguments[], int sig) {       
    char *token1 = const_cast<char *>(arguments[0]);
    char *token2 = const_cast<char *>(arguments[1]);
    
    if (token1 == NULL || !isValid(token1) || arguments[2] != NULL)
        throw STSHException("Usage: " + name + " <jobid> <index> | <pid>.");
    
    //one token situation
    if (token2 == NULL) {           
        pid_t pid = atoi(token1);
        if (!joblist.containsProcess(pid))
            throw STSHException("No process with pid " + to_string(pid) + ".");
            
        kill(pid, sig);
        
    } else {  //two tokens situation     
        if (!isValid(token2))
            throw STSHException("Usage: " + name + " <jobid> <index> | <pid>.");  
            
        int jobId = atoi(token1);
        if (!joblist.containsJob(jobId))
            throw STSHException("No job with id of " + to_string(jobId) + ".");
            
        int index = atoi(token2);
        std::vector<STSHProcess>& processes = joblist.getJob(jobId).getProcesses();
        if (index >= processes.size()) 
            throw STSHException("Job " + to_string(jobId) + " doesn't have a process at " + to_string(index) + ".");

        kill(processes[index].getID(), sig);
    }
}

//waits for sigint, sigtstp, and child processes
void STSHShell::waitSigs(sigset_t *addSigs, STSHJob& job) {
    //clear previous signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    while (joblist.hasForegroundJob()) {
        int sig;
        sigwait(addSigs, &sig);
        if (WIFSTOPPED(sig)) {
            killpg(job.getGroupID(), sig);
        } else {
            waitChildSigs();
        }
    }
    
    //gets back control 
    if (tcsetpgrp(STDIN_FILENO, getpgid(getpid())) < 0)
        throw STSHException("A more serious problem happens.");
}

//handles fg and bg
void STSHShell::handleFgBg(const std::string& name, const char *const arguments[]) {
    char *token = const_cast<char *>(arguments[0]);
    if (token == NULL || !isValid(token) || arguments[1] != NULL)
        throw STSHException("Usage: " + name + " <jobid>.");          
        
    int jobId = atoi(token);
    if (!joblist.containsJob(jobId))
        throw STSHException(name + " " + to_string(jobId) + ": No such job.");

    STSHJob& job = joblist.getJob(jobId);

    sigset_t addSigs;
    if (name == "fg") {
        setSigmask(addSigs);
        sigprocmask(SIG_UNBLOCK, &addSigs, NULL);

        //grants control
        if (tcsetpgrp(STDIN_FILENO, job.getGroupID()) < 0)
            throw STSHException("A more serious problem happens.");        
    }
    
    killpg(job.getGroupID(), SIGCONT);

    if (name == "fg") {        
        job.setState(kForeground);
        sigprocmask(SIG_BLOCK, &addSigs, NULL);
        waitSigs(&addSigs, job);
    } else {
        job.setState(kBackground);
    }   
}

/**
 * Define a mapping of commands as typed by the user to the Builtin enum
 * (defined in stsh.h). Any aliases can be defined here (e.g. "quit" and "exit"
 * are the same)
 */
const map<std::string, STSHShell::Builtin> STSHShell::kBuiltinCommands = {
  {"quit", STSHShell::Builtin::QUIT}, {"exit", STSHShell::Builtin::QUIT},
  {"fg", STSHShell::Builtin::FG},
  {"bg", STSHShell::Builtin::BG},
  {"slay", STSHShell::Builtin::SLAY},
  {"halt", STSHShell::Builtin::HALT},
  {"cont", STSHShell::Builtin::CONT},
  {"jobs", STSHShell::Builtin::JOBS},
};

/**
 * Handle execution of builtin commands (defined in the STSHShell::Builtin enum)
 */
void STSHShell::handleBuiltin(Builtin command, const char *const arguments[]) {
    if (command == Builtin::QUIT) {
        waitChildSigs();
        exit(0);
    } else if (command == Builtin::JOBS) {       
        waitChildSigs();
        cout << joblist;
    } else if (command == Builtin::FG) {
        handleFgBg("fg", arguments);        
    } else if (command == Builtin::BG) {
        handleFgBg("bg", arguments);
    } else if (command == Builtin::SLAY) {
        handleSlayHaltCont("slay", arguments, SIGKILL);       
    } else if (command == Builtin::HALT) {
        handleSlayHaltCont("halt", arguments, SIGTSTP);        
    } else if (command == Builtin::CONT) {
        handleSlayHaltCont("cont", arguments, SIGCONT);
    }
    else {
        throw STSHException("Internal error: builtin not yet implemented");
    }
}

/**
 * Run the REPL loop: take input from the user, execute commands, and repeat
 */
void STSHShell::run(int argc, char *argv[]) {
    pid_t stshpid = getpid();
    rlinit(argc, argv);
    
    while (true) {       
        string line;
        if (!readline(line)) break;
        if (line.empty()) continue;
        try {
            pipeline p(line);
            if (kBuiltinCommands.contains(p.commands[0].command)) {
                Builtin command = kBuiltinCommands.at(p.commands[0].command);
                handleBuiltin(command, p.commands[0].tokens);
            } else {
                createJob(p);
            }
        } catch (const STSHException& e) {
            cerr << e.what() << endl;
            // if exception is thrown from child process, kill it
            if (getpid() != stshpid) exit(0);
        }
    }
}

/**
 * Create a new job for the provided pipeline. Spawns child processes with
 * input/output redirected to the appropriate pipes and/or files, and updates
 * the joblist to keep track of these processes.
 */
void STSHShell::createJob(const pipeline& p) {
    /*
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    */
    
    sigset_t addSigs;
    setSigmask(addSigs);
    
    // creates a new job and adds any new child processes
    pid_t pgid = 0;
    STSHJob& job = joblist.addJob(kForeground);
    
    if (p.background) job.setState(kBackground);
    //creates the pipes
    size_t size = p.commands.size();
    int fds[size - 1][2];
    for (unsigned int i = 0; i < size - 1; i++) {
        pipe2(fds[i], O_CLOEXEC);
    }
    //opens redirection   
    int infd;
    if (!p.input.empty()) infd = open(p.input.c_str(), O_RDONLY);

    int outfd;
    if (!p.output.empty()) outfd = open(p.output.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
    
    pid_t pids[size];   
    
    for (unsigned int i = 0; i < size; i++) {
        command command = p.commands[i];
        pids[i] = fork();
        if (pids[i] < 0) {
            exit(0);
        }
        //enters child process
        if (pids[i] == 0) {
            sigprocmask(SIG_UNBLOCK, &addSigs, NULL);
            //handles redirection and process group
            if (i == 0) {                 
                pgid = getpid();
                setpgid(pgid, pgid);

                if(!p.background) {
                //grants control
                    if (tcsetpgrp(STDIN_FILENO, pgid) < 0)
                        throw STSHException("A more serious problem happens.");
                }
                if (!p.input.empty()) {
                    dup2(infd, STDIN_FILENO);
                    close(infd);
                }
            } else {
                dup2(fds[i - 1][0], STDIN_FILENO);
                if (pgid != 0) {
                    setpgid(getpid(), pgid);
                }
            }
                
            if (i == size - 1) {
                if (!p.output.empty()) {
                    dup2(outfd, STDOUT_FILENO);
                    close(outfd);
                }
            } else {
                dup2(fds[i][1], STDOUT_FILENO);
            }
            
            //handles execvp arguments
            char *argv[kMaxArguments + 2];
            argv[0] = const_cast<char *>(command.command);

            unsigned int j = 0;
            while (command.tokens[j] != NULL) {
                argv[j + 1] = const_cast<char *>(command.tokens[j]);
                j += 1;
            }
            argv[j + 1] = NULL;
           
            //call execvp
            if (execvp(argv[0], argv) < 0) {  //handle errors
                cerr << command.command << ": Command not found." << endl;
                
                if (i != 0) {
                    close(fds[i - 1][0]);
                }
                if (i != size - 1) {
                    close(fds[i][1]);
                }
                exit(0);
            }          
        }
        
        job.addProcess(STSHProcess(pids[i], command));
        //sets process group
        if (i == 0) {
            pgid = pids[i];
            setpgid(pgid, pgid);

            /*
            */            
        } else {
            if (pgid != 0) {
                setpgid(pids[i], pgid);
            }
            close(fds[i - 1][0]);
        }
        //closes pipes for parent
        if (i != size - 1) {
            close(fds[i][1]);
        }     
    }
    
    //prints process for background job
    if (p.background) {
        cout << "[" << job.getNum() << "]";
        for (int i = 0; i < size; i ++) {
            cout << " " << pids[i];
        }
        cout << endl;
    } else {
        sigprocmask(SIG_BLOCK, &addSigs, NULL);
        waitSigs(&addSigs, job);        
    }
}


/**
 * Define the entry point for a process running stsh.
 */
int main(int argc, char *argv[]) {
    STSHShell shell;
    shell.configureSignals();  
    shell.run(argc, argv);
    return 0;
}
