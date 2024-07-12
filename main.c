#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>

int main(int argc , char* argv[])
{
	printf("%u\n" , O_APPEND);
	printf("%u\n" , O_CREAT);
	printf("%u\n" , O_WRONLY);
	printf("%u\n" , O_TRUNC);
	printf("%u\n" , O_CREAT | O_WRONLY | O_TRUNC);
	return 0;
}
