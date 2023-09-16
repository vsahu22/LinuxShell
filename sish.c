#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

// Defining constants that set limits on the size 
#define HISTORY_SIZE 100 
#define MAX_COMMANDS 100
#define MAX_ARGS 1000

// Prototyping functions
int parseCommand(char* input, char* tokenizedInput[]);
int parsePipedCommand(char* input, char* tokenizedInput[]);
void addHistory(char* command, char* historyArr[], int* currentIndex, int* head);
void printHistory(char* historyArr[], int currentIndex, int head);
void removeNewLine(char* string);
int isDigit(char* string);
int usedArraySize(char* arr[], int size);
void clearArray(char* arr[], int size);
char* executeCommand(char* commandArgs[], int argNum, char* historyArr[], int *currentIndex, int head);
void executeCommandPiped(char* commandArgs[], int oldPipe[], int newPipe[], int iteration, int commandNum);

int
main(int argc, char* argv[]) {
	
	char* shellInput = NULL; // C string used to read in users input from keyboard
	char* historyCommand = NULL; // C string that stores a duplicate of shellInput and adds it to the history array
	char* parsedCommand[MAX_ARGS]; // Array that will store a command and its arguments
	char* commandList[MAX_COMMANDS]; // Array that will store each individual whole command separated by pipes
	char* cmdHistory[HISTORY_SIZE + 1]; // History size is 101 instead of 100 since index at 100 temporarily stores head if full 
	int historyIndex = 0; // Integer that keeps track of how many history entries there are until clear, then set back to 0
	int historyHead = 0; // Integer that stores the index of the head of the history array
	size_t length = 0; // Used in getline when reading input from the user
	int commandNum = 0; // Used to store the total number of piped commands
	int argNum = 0; // Used to store the total number of arguments in a command including the command itself
	int currentCommand = 0; // Keeps track of which command the shell is executing
	int oldPipe[2]; // Used for pipe that is read from. newPipe fd are stored after iteration
	int newPipe[2]; // Used for pipe that is written to. It is initialized each time in the loop that executes piped commands
	
	// Setting all the indexes in each array to NULL to be safe
	clearArray(cmdHistory, HISTORY_SIZE + 1);
	clearArray(commandList, MAX_COMMANDS);
	clearArray(parsedCommand, MAX_ARGS);
	while (1) { // Shell loop that runs until user types "exit"
		printf("sish> "); // Displaying shell prompt
		if (getline(&shellInput, &length, stdin) == -1) { // Reading input from the user
			perror("Error: ");
		}
		if (strcmp(shellInput, "exit\n") == 0) { // Exits shell if "exit" is typed
			break;
		}
		historyCommand = strdup(shellInput); // Makes a duplicate of the input with newly allocated space and stores it in history
		addHistory(historyCommand, cmdHistory, &historyIndex, &historyHead); // Function that adds the command entered by the user to history array
		commandNum = parsePipedCommand(shellInput, commandList); // Parses the command by pipes and returns the number of commands
		if (commandNum == -1) { // Either no commands or entered or there are too many commands, so it goes to the next iteration
			continue;
		}
		if (pipe(oldPipe) == -1) { // Initializing old pipe. Old pipe is always read from 
			perror("Pipe error: ");
		}
		// Loop that runs through each individual command, parses it and then executes it
		for (currentCommand = 0; currentCommand < commandNum; currentCommand++) {
		    if (pipe(newPipe) == -1) { // Initializing new pipe in loop. New pipe is always written to
		    	perror("Pipe error: ");
		    }
			argNum = parseCommand(commandList[currentCommand], parsedCommand); // Parsing the current command into its arguments
			if (argNum == -1) { // If there are too many arguments or error with parsing
			    break;
			}
			if (commandNum == 1) { // Only one command is being executed (no pipes and supports built-in)
			    // offsetCmd is set to offset if history [offset] is called. Otherwise it returns NULL
			    char* offsetCmd = executeCommand(parsedCommand, argNum, cmdHistory, &historyIndex, historyHead);
			    clearArray(parsedCommand, argNum);
			    clearArray(commandList, commandNum);
			    if (offsetCmd != NULL && strchr(offsetCmd, '|') != NULL) { // If history offset command has pipes
			        clearArray(commandList, commandNum);
			        commandNum = parsePipedCommand(offsetCmd, commandList); // Reassign commandNum depending on num of pipes in offsetCmd
			        currentCommand--; // Decrement to reset iteration to 0 after continue;
			        continue;
			    }
			    while (offsetCmd != NULL) {
				    argNum = parseCommand(offsetCmd, parsedCommand); // Parsing offset command
				    offsetCmd = executeCommand(parsedCommand, argNum, cmdHistory, &historyIndex, historyHead); 
			    }
			    break; // To be safe
			}
			else { // There is more than one command, so pipes are present
			    executeCommandPiped(parsedCommand, oldPipe, newPipe, currentCommand, commandNum); // Executing piped commands
			}
		}
		shellInput = NULL;
		// Closing all pipe fd's
		close(oldPipe[0]);
		close(oldPipe[1]);
		close(newPipe[0]);
		close(newPipe[1]);
		clearArray(commandList, commandNum); // Clearing the array to avoid potential problems next loop
	}
}

// Function that executes piped commands. Does not support built-in commands 
void executeCommandPiped(char* commandArgs[], int oldPipe[], int newPipe[], int iteration, int commandNum) {
	pid_t cpid;
	cpid = fork();
	if (cpid < 0) {
		perror("Error while forking");
		exit(0);
	}
	if (cpid == 0) { // Child program executes commands
		if (iteration > 0) { // There is a previous command so we read
		    close(oldPipe[1]); // Closing old pipe's write since we are reading from it
		    if (dup2(oldPipe[0], STDIN_FILENO) == -1) { // Read from old pipe
		        perror("Error with dup2 for read");
		    }
		    close(oldPipe[0]); // Closing read end fd since STDIN is reading from old pipe
		}
		if (iteration < commandNum - 1) { // There is another command so we write to pipe
		    close(newPipe[0]); // Closing new pipe's read since we are writing to it
		    if (dup2(newPipe[1], STDOUT_FILENO) == -1) { // Write to new pipe
		        perror("Error with dup2 for write");
		    }
		    close(newPipe[1]); // Closing write end fd since STDOUT is writing to new pipe
		}
		if (execvp(commandArgs[0], commandArgs) == -1) {
		    perror("Error while executing command");
		}
		exit(0);
	}
	else { // Parent program
	    //  There is a previous command so we need to close the old pipes.
	    //  The first command should not close the old pipes since we need them for next iteration
	    if (iteration > 0) { 
	        close(oldPipe[0]);
	        close(oldPipe[1]);
	    }
	    // There is another command after, so assign the fd of new pipes to old. Since we wrote to
	    // the new pipe, we need to read from it in the next iteration so we assign new fd to old.
	    if (iteration < commandNum - 1) {
	        oldPipe[0] = newPipe[0];
	        oldPipe[1] = newPipe[1];
	    }
		waitpid(cpid, NULL, 0); // Waiting for child to terminate
	}

}

// Function that executes single commands and supports built-in commands.
char* executeCommand(char* commandArgs[], int argNum, char* historyArr[], int *currentIndex, int head) {
	pid_t cpid;
	if (strcmp(commandArgs[0], "history") == 0) { // Checking if command is built-in command history
		if (argNum == 0) { // There are no arguments
			printHistory(historyArr, *currentIndex, head); // Printing history
			return NULL;
		}
		else if (argNum == 1) { // There is one argument
			if (strcmp(commandArgs[1], "-c") == 0) { // Checking if the argument is -c
				clearArray(historyArr, 100);
				*currentIndex = 0;
				return NULL;
			}
			else if (isDigit(commandArgs[1]) == 0) { // Checking if the argument is a digit
				int offset = atoi(commandArgs[1]); // Converts argument from string to digit
				if (offset > usedArraySize(historyArr, HISTORY_SIZE) - 1 || offset < 0) { // Check if offset is valid in array
					printf("Invalid offset\n");
					return NULL;
				}
				if (*currentIndex > HISTORY_SIZE - 1) { // If array is full, the offset is not the index anymore
					if (offset == 0) { // If offset is 0, that means the command is in index 100 (HISTORY_SIZE)
						offset = HISTORY_SIZE;
					}
					else {
						offset = (offset + head - 1) % HISTORY_SIZE; // Sets offset to the position of the command in the array
					}
				}
				return historyArr[offset]; // Returns the char pointer to the string of the command at offset
			}
			else { // Not a valid argument (Argument is not a digit or -c)
				printf("Invalid argument\n");
				return NULL;
			}

		}
		else { // If there is more than one argument
			printf("Error: Too many arguments\n");
			return NULL;
		}
	}
	if (strcmp(commandArgs[0], "cd") == 0) { // Checking if command is built in command cd
		int ch = chdir(commandArgs[1]);
			if (ch < 0) {
				if (commandArgs[1] == NULL) { // No argument given
					printf("Error: No path specified\n");
				}
				else { // Invalid argument (file or directory doesn't exist)
					printf("%s: No such file or directory\n", commandArgs[1]);
				}
			}
			return NULL;
	}
	cpid = fork(); // Forking
	if (cpid == 0) { // In child program
		if (execvp(commandArgs[0], commandArgs) == -1) { // Error checking and executing the command
			perror("Error while executing command");
		}
		exit(0); // If execution is successful, child shouldn't get here
	}
	else { // Parent program
		waitpid(cpid, NULL, 0); // Waiting for child to terminate
	}
	return NULL;
}

// Function that returns the number of used indexes in a c string array (The value at an index is not NULL)
int usedArraySize(char* arr[], int size) {
	int i;
	int usedIndex = 0;
	for (i = 0; i < size; i++) {
		if (arr[i] == NULL) { 
			break;
		}
		usedIndex++;
	}
	return usedIndex - 1;
}

// Function that checks if the given string is a digit or not. If it is, it returns 0. Otherwise it returns -1
int isDigit(char* string) {
	int i;
	int digitCounter = 0;
	int len = strlen(string);
	for (i = 0; i < len; i++) {
		if (*string >= 48 && *string <= 57) {
			digitCounter++;
		}
		string++;
	}
	if (digitCounter == len) { // String is a digit
		return 0;
	}
	return -1; // String isn't a digit
}

// Function that assigns the value at an index from 0 to size to NULL
void clearArray(char* arr[], int size) {
	int i = 0;
	for (i = 0; i < size; i++) {
		arr[i] = NULL;
	}
}

// Function that adds an entry to the history array. History array is stored in a circular fashion.
void addHistory(char* command, char* historyArr[], int* currentIndex, int* head) {
	removeNewLine(command);
	if (*currentIndex >= HISTORY_SIZE) {
		historyArr[HISTORY_SIZE] = historyArr[*head]; // Temporarily stores the command that was the previous head before it is changed
		*head = *currentIndex % HISTORY_SIZE + 1;
	}
	historyArr[*currentIndex % HISTORY_SIZE] = command;
	*currentIndex = *currentIndex + 1;
}

// Function that prints the history array, from the command at head to the newest command. 
void printHistory(char* historyArr[], int currentIndex, int head) {
	int histSize;
	int i;
	if (currentIndex >= HISTORY_SIZE) { // If there have been over 100 entries, set the size to 99 (HISTORY_SIZE - 1)
		histSize = HISTORY_SIZE - 1;
	}
	else { // Otherwise set the size to currentIndex - 1 
		histSize = currentIndex - 1;
	}
	for (i = 0; i <= histSize; i++) {
		printf("%d %s\n", i, historyArr[(head + i) % HISTORY_SIZE]);
	}
}

// Function that removes newline at the end of a string
void removeNewLine(char* string) { 
	char* newline = strchr(string, '\n');
	if (newline != NULL) {
		*newline = '\0';
	}
}

// Function that parses input entered by user into piped commands using '|' as delimiter and returns commandNum.
// If nothing is entered ('\n') or there is an error with parsing commands or there are too many arguments it returns -1. 
int parsePipedCommand(char* input, char* tokenizedInput[]) {
	char* cmdToken = NULL;
	char* savePtr = NULL;
	int commandNum;
	for (commandNum = 0; ; commandNum++, input = NULL) {
		cmdToken = strtok_r(input, "|", &savePtr);
		if (commandNum > MAX_COMMANDS) { // If there are too many commands, prints an error and returns -2
		    printf("Error: Too many commands\n");
		    return -1;
		}
		tokenizedInput[commandNum] = cmdToken;
		if (cmdToken == NULL || *cmdToken == '\n') {
			break;
		}
	}
	if (commandNum > 0) {
		return commandNum;
	}
	return -1;
}


// Function that tokenizes each individual command into the command and its arguments, separated by ' ' delimiter.
// It returns the number of arguments excluding the command. If there are too many arguments or there is an error
// with parsing, it returns -1
int parseCommand(char* input, char* tokenizedInput[]) { // Parses the command into tokens separated by whitespace. 
	char* cmdToken = NULL;
	char* savePtr = NULL;
	int argNum;
	for (argNum = 0; ; argNum++, input = NULL) {
		cmdToken = strtok_r(input, " ", &savePtr);
		if (argNum > MAX_ARGS) { // If there are too many arguments it returns -1
		    printf("Error: Too many arguments\n");
		    return -1;
		}
		if (cmdToken != NULL) {
			removeNewLine(cmdToken); // Removing '\n' so commands will execute properly
		}
		tokenizedInput[argNum] = cmdToken;
		if (cmdToken == NULL) {
			break;
		}
	}
	if (argNum > 0) {
		return argNum - 1;
	}
	printf("Error with parsing arguments\n");
	return -1;
}
