#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

#define STDIN 			0
#define STDOUT 			1
#define STDERR 			2

#define LF 				10

#define DIR 			"no such file or directory"
#define EXEC 			"exec error"
#define PERM 			"permission denied"

#define BUILT 			"Builtin %s error.\n"

#define SYNTAX 			{fprintf(stderr, "%s\n", SYNTAX_ERROR_STR); fflush(stderr);}

#define TERM 			"exited with status"
#define KILL 			"killed by signal"

#define BPT(proc,err,i)	{fprintf(stdout, "Background process %d terminated. (%s %d)\n", proc, err, i); fflush(stdout);}

#define PROC_LIMIT 		10000




pid_t to_wait[PROC_LIMIT];
volatile pid_t* lfp;

pid_t bp[PROC_LIMIT];
int bs[PROC_LIMIT];
volatile int bind;

sigset_t cblock;
sigset_t cwait;

struct sigaction dsint;
struct sigaction ssint;
struct sigaction dsigc;
struct sigaction ssigc;





void block()
{
	sigemptyset(&cblock);
	sigaddset(&cblock, SIGCHLD);
	sigprocmask(SIG_BLOCK, &cblock, 0);
}

void unblock()
{
	sigprocmask(SIG_UNBLOCK, &cblock, 0);
}

void handler(int sig)
{
	pid_t p;
	int s;

	while((p = waitpid(-1,&s,WNOHANG))>0)
	{
		int flag=0;

		for(int* i=to_wait; *i; ++i)
			if(*i==p)
			{
				--lfp;
				flag=1;
				break;
			}

		if(!flag)
		{
			*(bp+bind) = p;
			*(bs+bind) = s;
			bind++;
		}
	}
}

void presig()
{
	sigprocmask(SIG_BLOCK, 0, &cwait);

	ssint.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ssint, &dsint);

	ssigc.sa_handler = handler;
	sigfillset(&ssigc.sa_mask);

	ssigc.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &ssigc, &dsigc);
}



void print_prompt()
{
	struct stat stats;
	
	if(fstat(STDOUT, &stats)>=0 && S_ISCHR(stats.st_mode))
	{
		block();

		for(int i=0; i<bind; ++i)
			if(WIFSIGNALED(bs[i]))
				BPT(bp[i], KILL, WTERMSIG(bs[i]))
			else
				BPT(bp[i], TERM, bs[i])
 		
		bind = 0;

		fprintf(stdout, PROMPT_STR);
		fflush(stdout);

		unblock();
	}
}



int read_com(char* ln, char* buffer, int* bufsize, int* end)
{
	int len = *bufsize;
	int readed = 0;
	int last = 0;

	do
	{
		for(int i=last; i<len; ++i)
			if(buffer[i] == LF)
			{	
				*bufsize = len-i-1;
		
				if(i>MAX_LINE_LENGTH)
				{
					SYNTAX	
					readed = 0;
				}
				else
				{
					memcpy(ln, buffer, sizeof(char)*i);
					ln[i] = 0;
					readed = i;
				}
			
				memmove(buffer, buffer+i+1, sizeof(char)*(*bufsize));

				
				return readed;
			}

		if(len>MAX_LINE_LENGTH)
		{
			SYNTAX

			if(buffer[readed-1])
			{
				char temp;
				while(read(STDIN, &temp, 1) && temp!=LF);
			}

			*bufsize = 0;					
			
			return 0;
		}

		readed = read(STDIN, buffer + len, MAX_LINE_LENGTH+1);

		last = len;
		len += readed;
	}
	while(readed);

	*end = 1;
	return readed;
}



int cor_bui(command* com)
{
	for(int i=0; builtins_table[i].name; ++i)
		if(!strcmp(builtins_table[i].name, com->argv[0]))
		{
			if(builtins_table[i].fun(com->argv)==BUILTIN_ERROR)
			{
				fprintf(stderr, BUILT, com->argv[0]);
				fflush(stderr);
			}

			return 0;
		}

	return 1;

}

void error(char* name)
{
	char* err;

	if(errno==ENOENT)
		err = DIR;
	else if(errno==EPERM || errno==EACCES)
		err = PERM;
	else
		err = EXEC;
	
	fprintf(stderr, "%s: %s\n", name, err);
	fflush(stderr);

	exit(EXEC_FAILURE);
}

void redir(command* com, int* last, int* next)
{
	if(last)
	{
		dup2(last[0],STDIN);
		close(last[0]);
		close(last[1]);

	}

	if(next)
	{
		close(next[0]);
		dup2(next[1],STDOUT);
		close(next[1]);
	}


	for(int i=0; com->redirs[i]; ++i)
	{
		if(IS_RIN(com->redirs[i]->flags))
		{
			int in = open(com->redirs[i]->filename, O_RDONLY);

			if(in<0)
				error(com->redirs[i]->filename);
				
			dup2(in, STDIN);
			close(in);
		}

		if(IS_ROUT(com->redirs[i]->flags) || IS_RAPPEND(com->redirs[i]->flags))
		{
			int out;

			if(IS_RAPPEND(com->redirs[i]->flags))
				out = open(com->redirs[i]->filename, O_WRONLY | O_CREAT | O_APPEND, S_IWUSR | S_IRUSR);
			else
				out = open(com->redirs[i]->filename, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);

			if(out<0)
				error(com->redirs[i]->filename);
				
			dup2(out, STDOUT);
			close(out);
		}
	}

}

pid_t exec_com(command* com, int* last, int* next, int back)
{
	if(!com || !com->argv[0] || !cor_bui(com))
		return 0;

	pid_t child = fork();

	if(child>0)
		return child;
	
	else if(child<0)
		exit(EXEC_FAILURE);
	
	if(back)
		setsid();

	sigaction(SIGINT,&dsint,0);
	sigaction(SIGCHLD,&dsigc,0);

	unblock();

	redir(com,last,next);

	if(execvp(com->argv[0], com->argv)<0)
		error(com->argv[0]);

	return 0;
}

void exec_line(line* parsed)
{
	if(!parsed)
		return;

	int back = parsed->flags&LINBACKGROUND;

	for(pipeline* p=parsed->pipelines; *p; ++p)
	{
		int flag=0;

		for(command** c=*p+1; *c; ++c)
			if(!(*c)->argv[0])
			{
				SYNTAX
				flag=1;
				break;
			}

		if(flag)
			continue;
		

		block();


		int* last;
		int* next = 0;
		lfp = to_wait;
		*lfp = 0;

		for(command** c=*p; *c; ++c)
		{
			last = next;

			if(*(c+1))
			{
				next = (int*)malloc(2);
				pipe(next);
			}
			else
				next = 0;

			*lfp = exec_com(*c,last,next,back);
			
			if(*lfp)
				*(++lfp) = 0;

			if(last)
			{
				close(last[0]);
				close(last[1]);
				free(last);
			}
		}

		while(!back && lfp>to_wait)
			sigsuspend(&cwait);

		*to_wait = 0;

		unblock();
	}
}




int main(int argc, char* argv[])
{
	char ln[MAX_LINE_LENGTH+1];
	char buffer[MAX_LINE_LENGTH*2];
	
	int bufsize = 0;
	int end = 0;

	presig();

	while(!end)
	{
		print_prompt();
		
		if(read_com(ln, buffer, &bufsize, &end) && ln)
			exec_line(parseline(ln));
	}

return 0;
}