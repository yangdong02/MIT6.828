#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"
#define NULL 0
#define MXLINE 50

char buf[MXLINE], *ptr[MAXARG+1], *echo[MAXARG+1];
int main(int argc, char *argv[]) {
	if(argc > MAXARG) {
		printf("%s: Too many arguments...\n", argv[0]);
		exit(0);
	}
	if(argc == 1) {
		echo[0] = argv[0];
		echo[1] = "echo";
		argv = echo;
		argc = 2;
	}
	memcpy(ptr, argv+1, sizeof(char*) * argc);
	for(;;) {
		int i;
		for(i = 0; i < MXLINE-1; ++i) {
			if(read(0, &buf[i], 1) == 0 || buf[i] == '\n')
				break;
		}
		if(i == 0) break;
		buf[i] = 0;
		ptr[argc-1] = buf;
		int pid = fork();
		if(pid < 0) { fprintf(2, "fork error\n"); exit(1); }
		if(pid > 0) { // parent
			wait(NULL);
		} else { // child
			exec(ptr[0], ptr);
			fprintf(2, "exec %s error\n", ptr[0]);
			exit(1);
		}
	}
	exit(0);
}
