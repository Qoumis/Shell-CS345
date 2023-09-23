/************************************
*
* file: cs345sh.c
*
*@Author:   Koumakis Emmanouil
*@email:    csd4281@csd.uoc.gr
*
*
*************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include <sys/wait.h>

#define RD_SIZE 256

//For a colored prompt
#define GREEN   "\x1B[1;31m"
#define BLUE    "\x1B[1;34m"
#define DEFAULT "\x1B[0m"

char *count_slash(char * currpath); //so compiler can see these functions
void set_global_var(char **commands);
int is_valid_name(char * name);
void get_var(char * name);
int exec_pipe(char **commands, int prev, int first, int last);

pid_t pid;
pid_t bg_proc = -2;

int is_pipe = 0;


/*This function is used to handle ctrl+Z signal to send a proccess to the background*/
void stop_signal_handler(int signum){
	if(pid > 0){
		kill(pid,SIGSTOP);
		bg_proc = pid;
		printf("\n cs345sh moved process to the background\n");
	//	kill(pid,SIGCONT);
	}
	else
		printf("\n No process running\n");
}

/*This function is used to change terminal settings (to enable screen freeze and unfreeze with the use of ctrl+S and ctrl+Q)*/
void terminal_confg(){
	struct termios termios_p;

	if(tcgetattr(0, &termios_p) < 0) //get current configuration
		perror("cs345sh");

	termios_p.c_iflag = ICRNL | IXON | IXOFF;  //enable start/stop output control
	//(for some annoying reason ICRNL was turning off when i configure the settings, so we enable that as well)

	if(tcsetattr(0, TCSANOW, &termios_p) < 0) //update configuration
		perror("cs345sh");
}

void print_prompt(){
	int size = pathconf(".",_PC_PATH_MAX);      // get maximum length of current wd
	char * buf = (char *) malloc((size_t)size);

	printf(GREEN"%s@cs345sh"DEFAULT":"BLUE"%s$ " DEFAULT,getlogin(),getcwd(buf,size));
	free(buf);
}

/*Reads input from stdin, stores it in a string and returns it*/
char * read_line(){

	int c;
	int i = 0;
	char *line = malloc(sizeof(char) * RD_SIZE);

	while((c = getchar()) != EOF){
		if(c == '\n'){
			line[i] = '\0';
			return line;
		}
		else if( c == '|')
			is_pipe = 1;
		line[i++] = c;
	}
	return line;
}


/*Breaks the line we read into tokens*/
char **parse_line(char * line){

	int i = 0;
	char ** tokens = malloc(sizeof(char *) * 32);
	char * delim;

	if(is_pipe)
		delim = "|";
	else
		delim = " \t\n";

	char * currToken = strtok(line, delim);

	while(currToken != NULL){
		tokens[i++] = currToken;
		currToken = strtok(NULL, delim);
	}
	tokens[i] = '\0';
	return tokens;
}

/*This function is responsible for the execution of simple commands*/
void exec_simple(char **commands){

	pid = fork();
	if(pid == 0){ //child process executes command given by the user

		if(execvp(commands[0], commands) == -1){ //if command execution was unsuccessful
			fprintf(stderr,"%s: command not found\n",commands[0]);
			exit(1);
		}

	}else if(pid > 0){		//parent process

		int status;
		do{
			waitpid(pid, &status, WUNTRACED);	// wait for child process to finish running
		}while(!WIFEXITED(status) && !WIFSIGNALED(status)); //wait  till the (child) process exits or is killed
		pid = -1;
	}else				   //fork error
		printf("Error! Fork not executed successfully!\n");
}

/*This function checks the command(s) that the user typed and then
 * proceeds to execute it by calling the appropriate functions
 */
void exec_command(char **commands){
	if(commands[0] == NULL)   //I don't want to get (core dumped) if enter is pressed
		printf("\r");

	else if(strcmp(commands[0], "cd") == 0){			//cd case

		if(commands[1] == NULL){
			/*Get current path*/
			int size = pathconf(".",_PC_PATH_MAX);
			char * buf = (char *) malloc((size_t)size);
			char * currpath = getcwd(buf,size);
			/*Count number of slashes and return the proper "../" string*/
			commands[1] = count_slash(currpath);
			free(buf);
		}
		if(chdir(commands[1]) == -1)
			perror("cs345sh cd");

	}else if(strcmp(commands[0], "exit") == 0){		//exit case
		printf("--Exiting cs345sh--\n");
		exit(0);

	}else if(commands[1] == NULL && strchr(commands[0],'=') != NULL) //Environment variable case
		set_global_var(commands);

	else if(strcmp(commands[0],"echo") == 0 && commands[1] != NULL && commands[1][0] == '$') //echo enviroment variable
		get_var(commands[1]);

	else if(strcmp(commands[0],"fg") == 0){		//still needs work...?
		if(kill(bg_proc, SIGCONT) == -1)
			printf("No process running in the background\n");
		else
			bg_proc = -2;

	}else if(is_pipe){					//pipe case

		is_pipe = 0;
		char** currCommand = malloc(sizeof(char*) * 16);
		int is_first = 1, is_last = 0;
		int prev;  //used to save write end of pipe-> In each subsequent loop, read end of pipe will read from here

		int i = 0;
		while(commands[i] != NULL){  //for every pipe command
			currCommand = parse_line(commands[i++]);
			prev = exec_pipe(currCommand, prev, is_first, is_last);
			is_first = 0;
			if(commands[i+1] == NULL)
				is_last = 1;
		}
		free(currCommand);

	}else								//Simple command case
		exec_simple(commands);
}

/**This function creates a pipe, replaces standard input and output with each end of
 * the pipe and executes each command given by the user
 */
int exec_pipe(char **commands, int prev, int is_first, int is_last){

		int fd[2]; /*fd[0]->read fd[1]->write*/

		if(pipe(fd) == -1){
			perror("cs345sh pipe:");
			exit (1);
		}

		pid = fork();
		if(pid == 0){
			if(is_first){          //first call just write to pipe
				close(fd[0]);
				dup2(fd[1],1);
			}
			else if(is_last){     //last call just read from pipe
				close(fd[1]);
				dup2(prev,0);
				close(prev);
				close(fd[0]);
			}
			else{				//somewhere in the middle of the pipe-> read from one end and write to the other
				dup2(prev,0);
				dup2(fd[1],1);
				close(prev);
			}
			if(execvp(commands[0], commands) == -1){
				fprintf(stderr,"%s: command not found\n",commands[0]);
				exit(1);
			}

		}
		else if(pid > 0){
			int status;
			do{
				waitpid(pid, &status, WUNTRACED);	// wait for child process to finish running
			}while(!WIFEXITED(status) && !WIFSIGNALED(status)); //wait  till the (child) process exits or is killed
			close(fd[1]);
		}
		else
			printf("Error! Fork not executed successfully!\n");

			return fd[0];
}

/**This function counts the number of slashes on the string currpath
 * and returns a string with that many "../"
 *
 * We do this to count how many directories we have to go back (to reach the first dir)
 * when user enters the command cd without any other arguments
 */
char *count_slash(char * currpath){

	char * go_back = malloc(sizeof(char) * 60);
	go_back[0] = '\0';
	int i =0;
	while(currpath[i] != '\0'){
		if(currpath[i++] == '/')
			strcat(go_back,"../");
	}

	return go_back;
}

void set_global_var(char **commands){

	char *name = strtok(commands[0],"=");

	char *value = strtok(NULL,"");

	if(is_valid_name(name) == 0){
		printf("%s: Not a valid variable name\n", commands[0]);
		return;
	}
	setenv(name,value,1);  //gets in the environment that is also inherited to children processes
}

/*this function removes the dollar sign ($) from the variables name and prints its contents if they exist.*/
void get_var(char * name){
	char * var = malloc(sizeof(name));

	int i = 1, j = 0;
	while(name[i] != '\0')
		var[j++] = name[i++];
	var[j] = '\0';

	if(getenv(var) != NULL)
		printf("%s\n",getenv(var));
	else
		printf("\n");
	free(var);
}

/*This functions checks if the identifier given by the user is a valid environment variable name.
 * (It must contain only alphanumeric characters and the underscore + it can't start with a number)
 */
int is_valid_name(char * name){
	if((name[0]) >= '0' && name[0] <= '9') //first digit is a number
		return 0;

	int cnt = 0;
	for(int i = 0; i <= strlen(name); i++){	//Some other digit isn't (a-z A-Z 1-9 _)
		if((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z')
		|| (name[i] >= '0' && name[i] <= '9') || name[i] == '_')
			cnt++;
	}
	if(cnt != strlen(name))
		return 0;

	return 1;
}


int main(){
	terminal_confg();
	signal(SIGTSTP, stop_signal_handler);

	char * line;
	char ** commands;

	while(1){

		print_prompt();
		line = read_line();
		commands = parse_line(line);

		exec_command(commands);

		free(line);
		free(commands);
	}
	return 0;
}
