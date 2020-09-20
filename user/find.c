#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

#define MX 50
void dfs(const char* path, const char* goal) {
	char buf[MX], tmp[MX];
	struct dirent de;
	struct stat st;

	int len = strlen(path);
	if(len > MX-2) {
		fprintf(2, "find: directory name %s is too long\n", path);
		return;
	}
	strcpy(buf, path);
	while(len && (buf[len - 1] == ' ' || buf[len - 1] == '/'))
		buf[--len] = 0;
	if(len == 0) return;

	int fd = open(buf, 0);
	if(fd < 0) {
		fprintf(2, "find: cannot open %s\n", buf);
		return;
	}
	if(fstat(fd, &st) < 0) {
		fprintf(2, "find: cannot fstat %s\n", buf);
		close(fd);
		return;
	}
	int slash = -1;
	for(int i = 0; i < len; ++i)
		if(buf[i] == '/') slash = i;
	if(strcmp(buf+slash+1, goal) == 0) {
		printf("%s\n", buf);
	}
	if(st.type == T_DIR) {
		if(len + 1 + DIRSIZ + 1 > sizeof buf) {
			printf("find: path too long\n");
			close(fd);
			return;
		}
		buf[len++] = '/', buf[len] = 0;
		while(read(fd, &de, sizeof(de)) == sizeof(de)) {
			if(de.inum == 0 || !strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
			strcpy(tmp, buf);
			memmove(tmp+len, de.name, DIRSIZ);
			tmp[len+DIRSIZ] = 0;
			if(stat(tmp, &st) < 0) {
				printf("find: cannot stat %s\n", tmp);
				continue;
			}
			dfs(tmp, goal);
		}
	}
	close(fd);
}
int main(int argc, char *argv[]) {
	if(argc != 3) {
		fprintf(2, "Usage: %s DIRECTORY NAME\n", argv[0]);
		exit(1);
	}
	dfs(argv[1], argv[2]);
	exit(0);
}
