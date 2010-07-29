/*
 * dmon-monitor.c
 * Copyright (C) 2010 Adrian Perez <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#include "dmon.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#define _dmon_help_message \
    "Usage: %s [options] cmd [cmd-options] -- log [log-options]\n" \
    "Launch a simple daemon, providing logging and respawning.\n" \
    "\n" \
    "  -e         Redirect command stderr to stdout.\n" \
    "  -h, -?     Show this help text.\n" \
    "\n"


static int   log_fds[2]  = { -1, -1 };
static pid_t log_pid     = 0;
static pid_t cmd_pid     = 0;
static int   should_exit = 0;
static int   redir_errfd = 0;


static void
spawn_child (pid_t *pid, char **argv, int write_fd, int read_fd, int redir_err)
{
    assert (pid != NULL);
    assert (argv != NULL);

    if ((*pid = fork ()) < 0)
        die ("fork failed: %s", strerror (errno));

    /* We got a valid PID, return */
    if (*pid > 0) {
        dprint (("child pid = %lu\n", (unsigned long) *pid));
        return;
    }

    /* Execute child */
    if (write_fd >= 0) {
        dprint (("redirecting write_fd = %i -> %i\n", write_fd, STDOUT_FILENO));
        if (dup2 (write_fd, STDOUT_FILENO) < 0) {
            dprint (("redirection failed: %s\n", strerror(errno)));
            _exit (111);
        }
    }
    if (read_fd >= 0) {
        dprint (("redirecting read_fd = %i -> %i\n", read_fd, STDIN_FILENO));
        if (dup2 (read_fd, STDIN_FILENO) < 0) {
            dprint (("redirection failed: %s\n", strerror(errno)));
            _exit (111);
        }
    }

    if (redir_err) {
        dprint (("redirecting stderr -> stdout\n"));
        if (dup2 (STDOUT_FILENO, STDERR_FILENO) < 0) {
            dprint (("could not redirect stderr: %s\n", strerror (errno)));
            exit (111);
        }
    }

    execvp (argv[0], argv);
    _exit (111);
}



int
main (int argc, char **argv)
{
	char **log_argv = NULL;
	int    log_argc = 0;

	char **cmd_argv = NULL;
	int    cmd_argc = 0;

	char c;
	int i;

	while ((c = getopt (argc, argv, "+?he")) != -1) {
		switch (c) {
            case 'e':
                redir_errfd = 1;
                break;
			case 'h':
			case '?':
				printf (_dmon_help_message, argv[0]);
				exit (EXIT_SUCCESS);
			default:
				fprintf (stderr, "%s: unrecognized option '-%c'\n", argv[0], c);
				fprintf (stderr, _dmon_help_message, argv[0]);
				exit (EXIT_FAILURE);
		}
	}


	cmd_argv = argv + optind;
    i = optind;

	/* Skip over until "--" is found */
	while (i < argc && strcmp (argv[i], "--") != 0) {
		cmd_argc++;
		i++;
	}

	/* There is a log command */
	if (i < argc && strcmp (argv[i], "--") == 0) {
		log_argc = argc - cmd_argc - optind - 1;
		log_argv = argv + argc - log_argc;
        log_argv[log_argc] = NULL;
	}

    cmd_argv[cmd_argc] = NULL;

    if (log_argc > 0) {
        pipe (log_fds);
        dprint (("pipe_read = %i, pipe_write = %i\n", log_fds[0], log_fds[1]));
        fd_cloexec (log_fds[0]);
        fd_cloexec (log_fds[1]);
    }

#ifdef DEBUG_TRACE
    {
        char **xxargv = cmd_argv;
        fprintf (stderr, "cmd:");
        while (*xxargv) fprintf (stderr, " %s", *xxargv++);
        fprintf (stderr, "\n");
    }
#endif /* DEBUG_TRACE */

    spawn_child (&cmd_pid, cmd_argv, log_fds[1], -1, redir_errfd);
    if (log_argc > 0) {
#ifdef DEBUG_TRACE
        {
            char **xxargv = log_argv;
            fprintf (stderr, "log:");
            while (*xxargv) fprintf (stderr, " %s", *xxargv++);
            fprintf (stderr, "\n");
        }
#endif /* DEBUG_TRACE */
        spawn_child (&log_pid, log_argv, -1, log_fds[0], 0);
    }

    while (should_exit == 0)
        pause ();

	exit (EXIT_SUCCESS);
}


