#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
	if(argc != 2) {
		fprintf(2, "Usage: %s NUMBER\nPause for NUMBER seconds\n", argv[0]);
		exit(1);
	}
	uint len = strlen(argv[1]);
	for(uint i = 0; i < len; ++i) if(argv[1][i] > '9' || argv[1][i] < '0') {
		fprintf(2, "Usage: %s NUMBER\nPause for NUMBER seconds\n", argv[0]);
		exit(1);
	}
	sleep(atoi(argv[1]));
	exit(0);
}
