#include <cassert>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include "subprocess.h"
#include "fork-utils.h"  // this has to be the last #include'd statement in the file
using namespace std;

static const size_t kNumCPUs = sysconf(_SC_NPROCESSORS_ONLN);

static const char *kWorkerArguments[] = {"./factor.py", "--self-halting", NULL};


//Spawn a self-halting factor.py process for each core and update the supplied workers vector
static void spawnAllWorkers(vector<subprocess_t>& workers) {
    cout << "There are this many CPUs: " << kNumCPUs << ", numbered 0 through " << kNumCPUs - 1 << "." << endl;

    //assign each process to always execute on a particular core
    size_t i = 0;
    while( i < kNumCPUs) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(i, &set);
        
        subprocess_t subp = subprocess(kWorkerArguments, true, false);
        workers.push_back(subp);
        sched_setaffinity(subp.pid, sizeof(cpu_set_t), &set);
        cout << "Worker " << subp.pid << " is set to run on CPU " << i << "." << endl;
        i += 1;
  }
}

//Helpter function to decompose the broadcastNumbersToWorkers function
static const subprocess_t& getAvailableWorker(vector<subprocess_t>& workers) {
    //wait for a worker to self-halt
    int status;
    pid_t pid = waitpid(-1, &status, WUNTRACED);

    //return its subprocess_t struct from the workers vector
    for (size_t i = 0; i < kNumCPUs; i++) {
        if (pid == workers[i].pid) {
            return workers[i];
        }
    }
    assert(false);
}

//Read numbers from stdin and distribute the numbers across the factor.py child processes
static void broadcastNumbersToWorkers(vector<subprocess_t>& workers) {
    while (true) {
        string line;
        getline(cin, line);
        if (cin.fail()) break;
        size_t endpos;
        long long num = stoll(line, &endpos);
        if (endpos != line.size()) break;

        //once a number is read, send it to an available worker
        const subprocess_t& subp = getAvailableWorker(workers);
        dprintf(subp.supplyfd, "%lld\n", num);
        kill(subp.pid, SIGCONT);
    }    
}

//Wait for all workers to self-halt
static void waitForAllWorkers(vector<subprocess_t>& workers) {
    int status;
    size_t i = 0;
    while(i < kNumCPUs) {
        waitpid(-1, &status, WUNTRACED);
        i += 1;
    }
}

//Close the workers' input pipes, wake them up, and wait for them to all exit
static void closeAllWorkers(vector<subprocess_t>& workers) {
    //close their input pipes
    for (size_t i = 0; i < kNumCPUs; i++) {
        close(workers[i].supplyfd);
        kill(workers[i].pid, SIGCONT);
    }

    //wait for them to all exit
    size_t c = 0;
    while(c < kNumCPUs) {
        waitpid(-1, NULL, 0);
        c += 1;
    }

    //ensure no zombie processes are left behind
    pid_t pid = waitpid(-1, NULL, 0);
    assert(pid == -1 && errno == ECHILD);
}

int main(int argc, char *argv[]) {
    vector<subprocess_t> workers;
    spawnAllWorkers(workers);
    broadcastNumbersToWorkers(workers);
    waitForAllWorkers(workers);
    closeAllWorkers(workers);
    return 0;
}
