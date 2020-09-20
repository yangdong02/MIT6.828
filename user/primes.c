/* Hints:
 * 1. maximum available fd: 15 
 * 2. transfer int through pipe directly
 */
#include<kernel/types.h>
#include<user/user.h>
#define NULL 0

void solve(int *pre) {
	int nxt[2], pid = 0, cur = 0;
	if(pre != NULL) {
		if(read(pre[0], &cur, sizeof(int)) == 0)
			return;
		printf("prime %d\n", cur);
	}

	pipe(nxt);
	pid = fork();

	if(pid > 0) { // parent
		close(nxt[0]);
		if(pre == NULL) {
			for(int i = 2; i <= 35; ++i) {
				write(nxt[1], &i, sizeof(int));
			}
		} else {
			int val;
			while(read(pre[0], &val, sizeof(int)) == 4) {
				if(val % cur != 0)
					write(nxt[1], &val, sizeof(int));
			}
		}
		close(nxt[1]);
		wait(NULL);
	} else { // child
		close(nxt[1]);
		solve(nxt);
		close(nxt[0]);
	}
}
int main() {
	solve(NULL);
	exit(0);
}
