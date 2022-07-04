#pragma once

#include "stsh-job-list.h"
#include "stsh-parser/stsh-parse.h"

class STSHShell {
public:
    void configureSignals();  
    void run(int argc, char *argv[]);

private:
    enum Builtin {QUIT, FG, BG, SLAY, HALT, CONT, JOBS};
    static const std::map<std::string, Builtin> kBuiltinCommands;

    STSHJobList joblist;
    sigset_t sigs;

    void waitChildSigs();
    bool isValid(char *token);
    void setSigmask(sigset_t& sigs);
    void handleSlayHaltCont(const std::string& name, const char *const arguments[], int sig);       
    void waitSigs(sigset_t *sigs, STSHJob& job);
    void handleFgBg(const std::string& name, const char *const arguments[]);
    void handleBuiltin(Builtin command, const char *const arguments[]);
    void createJob(const pipeline& p);
};
