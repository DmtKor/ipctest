#include <fcntl.h>        /* open, modes & flags */
#include <poll.h>         /* poll, structs and constants */
#include <sys/stat.h>     /* mkfifo */
#include <wait.h>         /* wait, kill, signal codes */
#include <stdio.h>        /* printf, perror */
#include <stdlib.h>       /* exit, EXIT_FAILURE, EXIT_SUCCESS */
#include <string.h>       /* strcmp, strsignal */
#include <unistd.h>       /* exit, fork, read, write, sleep, getppid */
#include <sys/signalfd.h> /* signalfd function */


int main(int argc, char *argv[], char *envp[]) {
    /* -d option can help test if FIFO part is working */
    int debug = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) 
        debug = 1;
    
    pid_t pid;
    pid = fork();
    /* Error */
    if (pid == -1) {
        perror("Error occurred while trying to fork process.");
        exit(EXIT_FAILURE);
    }
    /* Child */
    else if (pid == 0) { 
        pid_t ppid = getppid();
        int pipefd = -1;
        /* Trying to open FIFO created by parent process */
        while (pipefd == -1) {
            pipefd = open("./requests", O_WRONLY );
        }
        char buf[] = "Hello from child process!\n";
        for(;;) {
            sleep(3);
            /* Sending signal */
            if (kill(ppid, SIGUSR1) == -1) {
                perror("Unable to send SIGUSR1 from child process to parent");
                exit(EXIT_FAILURE);
            }
            /* When option -d enabled, sends message through the pipe to parent process */
            if (debug) {
                write(pipefd, buf, sizeof(buf));
            }
        }
    }
    /* Parent */
    else {
        pid_t cpid = pid; /* Just for clarity, it's PID of the child */
        if (mkfifo("./requests", S_IRUSR | S_IWUSR) == -1) {
            perror("Unable to create FIFO");
            kill(cpid, SIGTERM);
            exit(EXIT_FAILURE);
        }
        /* Signals SIGUSR1 and SIGINT will be handled manually in parent process */
        sigset_t sig;
        sigemptyset(&sig);
        sigaddset(&sig, SIGUSR1);
        sigaddset(&sig, SIGINT);
        sigprocmask(SIG_BLOCK, &sig, NULL);
        int sigfd = signalfd(-1, &sig, 0);
        int pipefd = open("requests", O_RDONLY);
        if (pipefd == -1) {
            perror("Unable to open pipe");
            kill(cpid, SIGTERM);
            exit(EXIT_FAILURE);
        }
        struct pollfd pfd[] = {
            {
                .fd = sigfd,
                .events = POLLIN
            },
            {
                .fd = pipefd,
                .events = POLLIN
            }
        };
        char buf[16]; /* For debug */
        /* Signal handling cycle */
        while(1) {
            poll(pfd, 2, -1);
            if ((pfd[0].revents & POLLIN) != 0) {
                struct signalfd_siginfo siginfo = {};
                read(sigfd, &siginfo, sizeof(siginfo));
                if (siginfo.ssi_signo == SIGUSR1) {
                    printf("'%s' has been received.\n", strsignal(siginfo.ssi_signo));
                }
                else if (siginfo.ssi_signo == SIGINT) {
                    /* Ctrl-C */
                    break;
                }
            }
            if (debug && ((pfd[1].revents & POLLIN) != 0)) {
                int n = read(pipefd, buf, sizeof(buf));
                if (n != 0) {
                    printf("%.*s", n, buf); /* write everything from requests to stdout */
                }
            }
        }

        close(pipefd);
        close(sigfd);
        if (kill(cpid, SIGTERM) == -1) {
            perror("Unable to terminate child process");
            exit(EXIT_FAILURE);
        }
        int res;
        cpid = wait(&res);
        printf("Child process with PID %d ended with status code %d.\n", cpid, res);
        if (unlink("./requests") == -1) { /* Remove FIFO */
            perror("Unable to delete FIFO");
            exit(EXIT_FAILURE);
        }
    }

    exit(EXIT_SUCCESS);
}