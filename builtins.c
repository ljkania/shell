#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/param.h>
#include <signal.h>

#include "builtins.h"

int lexit(char* []);
int echo(char* []);
int lcd(char* []);
int lkill(char* []);
int lls(char* []);
int undefined(char* []);


builtin_pair builtins_table[] =
{
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&lcd},
	{"lkill",	&lkill},
	{"lls",		&lls},
	{NULL,		NULL}
};

int lexit(char* argv[])
{
	exit(0);
}

int echo(char* argv[])
{
	int i = 1;

	if(argv[i])
		printf("%s", argv[i++]);

	while(argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	
	return 0;
}

int lcd(char* argv[])
{
	if((!argv[1] && chdir(getenv("HOME"))==-1) || (argv[1] && (argv[2] || (!argv[2] && chdir(argv[1])==-1))))
		return BUILTIN_ERROR;

	return 0;
}

int lkill(char* argv[])
{
	int sig = -1;
	int pid;
	int ipid;

	if(!argv[1] || (argv[1] && argv[2] && argv[3]))
		return BUILTIN_ERROR;

	else if(!argv[2])
	{
		pid = atoi(argv[1]);
		ipid = 1;
		sig = SIGTERM;
	}

	else
	{
		pid = atoi(argv[2]);
		ipid = 2;

		char num[strlen(argv[1])];
		sig = atoi(argv[1]+1);
		sprintf(num, "%d", sig);
	
		if(argv[1][0]!='-' || strcmp(num, argv[1]+1))
			return BUILTIN_ERROR;
	}

	char str[strlen(argv[ipid])];
	sprintf(str, "%d", pid);
	
	if(strcmp(str, argv[ipid]) || kill(pid,sig)==-1)
		return BUILTIN_ERROR;

	return 0;
}

int lls(char* argv[])
{
	char path[PATH_MAX];

	if(!argv[1] && getcwd(path, PATH_MAX))
	{
		DIR* dir = opendir(path);
		
		if(!dir)
			return BUILTIN_ERROR;

		struct dirent* ent;

		while((ent = readdir(dir)))
			if(strncmp(ent->d_name, ".", 1))
			{
				fprintf(stdout, "%s\n", ent->d_name);
				fflush(stdout);
			}

		closedir(dir);
	}
	else
		return BUILTIN_ERROR;

	return 0;
}

int undefined(char* argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	
	return BUILTIN_ERROR;
}