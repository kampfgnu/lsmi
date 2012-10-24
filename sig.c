
#include <stdio.h>
#include <signal.h>

void die __P(( int sig ));

/*
 * Handle signals 
 */
void
set_traps ( void )
{
	signal( SIGINT, die );
	signal( SIGQUIT, die );
	signal( SIGILL, die );
	signal( SIGTRAP, die );
	signal( SIGABRT, die );
	signal( SIGIOT, die );
	signal( SIGFPE, die );
	signal( SIGKILL, die );
	signal( SIGUSR1, die );
	signal( SIGSEGV, die );
	signal( SIGUSR2, die );
	signal( SIGPIPE, die );
	signal( SIGTERM, die );
	#ifdef SIGSTKFLT
	signal( SIGSTKFLT, die );
	#endif
	signal( SIGCHLD, die );
	signal( SIGCONT, die );
	signal( SIGSTOP, die );
	signal( SIGTSTP, die );
	signal( SIGTTIN, die );
	signal( SIGTTOU, die );
}


