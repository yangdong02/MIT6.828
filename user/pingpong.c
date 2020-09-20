/*
 * Hint: a pipe can be used in one direction only!
 */
#include<kernel/types.h>
#include<user/user.h>

int Fork() {
	int pid = fork();
	if(pid < 0) {
		fprintf(2, "fork error");
		exit(1);
	}
	return pid;
}
int main() {
	int p2c[2], c2p[2];
	char buf[2];
	pipe(p2c);
	pipe(c2p);
	
	int pid = Fork();
	if(pid != 0) { // parent
		write(p2c[1], "i", 1);
		read(c2p[0], buf, 1);
		printf("%d: received p%cng\n", getpid(), buf[0]);
	} else { // child
		read(p2c[0], buf, 1);
		printf("%d: received p%cng\n", getpid(), buf[0]);
		write(c2p[1], "o", 1);
	}
	close(p2c[0]);
	close(p2c[1]);
	close(c2p[0]);
	close(c2p[1]);

	exit(0);
}
