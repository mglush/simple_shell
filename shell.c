#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

/*
 * This function parses the next subcommand seperated by |, <, > etc
 * Place the content in args where args[0] = command name
 * args[1] [2] etc will be the arguments
 *
 * Return the leading character after this command,
 * which could be |, <, >, or a regular character
 * For example,  for "ls > tmp",  return the position of ">"
 * For example,  for "ls | wc ",  return the position of "|"
 */
char* parse(char * lineptr, char **args)
{
  //while lineIn isn't done. 
  while (*lineptr != '\0') 
  {
    // If it's whitespace, tab or newline,
    // turn it into \0 and keep moving till we find the next token.
    // This makes sure each arg has a \0 immidiately after it
    // without needing to copy parts of lineIn to new strings. 
    while (!(isdigit(*lineptr) || isalpha(*lineptr) || *lineptr == '-'
        || *lineptr == '.' || *lineptr == '\"'|| *lineptr == '/'
        || *lineptr == '_'))
    {   
      //break out if we reach an "end"
      if(*lineptr == '\0' || *lineptr == '<' || *lineptr == '>'
          || *lineptr == '|' || *lineptr == '&')
        break;

      *lineptr = '\0';
      lineptr++;
    }

    //break out of the 2nd while loop if we reach an "end".
    if(*lineptr == '\0' || *lineptr == '<' || *lineptr == '>'
        || *lineptr == '|' || *lineptr == '&' )
      break;

    //mark we've found a new arg
    *args = lineptr;    
    args++;

    // keep moving till the argument ends. Once we reach a termination symbol,
    // the arg has ended, so go back to the start of the loop.
    while (isdigit(*lineptr) || isalpha(*lineptr) || *lineptr == '-'
        || *lineptr == '.' || *lineptr == '\"'|| *lineptr == '/'
        || *lineptr == '_')
      lineptr++;
  }

  *args = NULL; 

  return lineptr;
}


/*
 * This function forks a child to execute  a command stored
 * in a string array *args[].
 * inPipe -- the input stream file descriptor.  For example,  wc < temp 
 * outPipe --the output stream file descriptor.  For example, ls > temp 
 */
void fchild(char **args, int inPipe, int outPipe)
{
  pid_t pid;
  
  pid = fork();
  if (pid > 0) // parent process.
    waitpid(pid, NULL, 0); // wait for children to finish up.
    // this area can be expanded to collect child error codes.
  else if (pid == 0)/*Child  process*/
  {
    int execReturn=-1;

    /*Call dup2 to setup redirection, and then call excevep*/
    // check if we actually need to redirect input.
    if (inPipe != 0) {
      // close stdin.
      close(0);
      // use dup2 to set up inPipe.
      dup2(inPipe, 0);
    }

    // check if we actually need to redirect output.
    if (outPipe != 1) {
      // close stdout.
      close(1);
      // use dup2 to set up outPipe.
      dup2(outPipe, 1);
    }

    // now that the in and out pipes are set up, run da command.
    execReturn = execvp(args[0], args);

    // if exec fails, print error.
    perror(args[0]);

    // exit at the end, with error code.
    _exit(1); // using _exit because this is a child.
  } 
  else // pid < 0
  {
    perror("fork");
    exit(1);
  }

  // if we redirected input above, we need to now close this thang.
  if(inPipe != 0)
    close(inPipe); /*clean up, release file control resource*/
    
  // if we redirected output above, we need to now close this thang.
  if(outPipe != 1)
    close(outPipe); /*clean up, release file control  resource*/
}


/*
 * This function parses and executes a command started from the string position
 * pointed by linePtr. This function is called recursively to execute a
 * subcommand one by one.
 *
 * linePtr --  points to the starting position of the next command in the input command string
 * length -- the length of the remaining command string
 * inPipe is the input file descriptor, initially it is 0, gradually it may be changed as we parse subcommmands.
 * out is the output file descriptor, initially it is 1, gradually it may be changed as we parse subcommmands.
 * The inPipe default value  is 0 and  outPipe default value is 1.
 */
void runcmd(char * linePtr, int length, int inPipe, int outPipe)
{
  char * args[length];
  char * nextChar = parse(linePtr, args);

  if (args[0] != NULL)
  {
    // exit shell when exit command is typed!
    if (strcmp(args[0], "exit") == 0)
      exit(0);
            
    if (*nextChar == '<' && inPipe == 0) 
    {
      /*INPUT REDIRECTION, setup the file name to read from*/
      
      //nextChar+1 moves the character position after <,
      //thus points to a file name
      char* in[length];
      nextChar = parse(nextChar+1,in);

      /* Change inPipe so it follows the redirection */ 
      // basically, we want to open the file we will read from (down below when fchild() gets called.)
      // the child takes care of closing stdin, using dup2, and closing inPipe at the end.
      if ((inPipe = open(in[0], O_RDONLY)) == -1)
        perror("open");
    }

    if (*nextChar == '>')
    {   /*OUTPUT REDIRECTION, setup the file name to write*/

        //nextChar+1 moves the character position after >,
        //thus points to a file name
        char* out[length];
        nextChar = parse(nextChar+1,out);

        /* Change outPipe so it follows the redirection */ 
        // basically, we want to create a file we will later write to (down below when fchild() gets called.)
        // the child takes care of closing stdout, using dup2, and closing outPipe at the end.
        if ((outPipe = creat(out[0], 0644)) == -1)
          perror("creat");
    }

    if (*nextChar == '|')
    { /*It is a pipe, setup the input and output descriptors */
      /*execute the subcommand that has been parsed, but setup the output using this pipe*/

      // gotta build the pipe between the command on the left and command on the right.
      // we can executed the left command, create a pipe, use output of the command as input
      // into the pipe, carry on with our day.

      // create the pipe.
      int fd[2];
      pipe(fd);

      // execute fchild command, putting its output into the write end of the pipe.
      fchild(args, inPipe, fd[1]);

      // calculate remaining length we have to process.
      while (*linePtr != *nextChar) {
        length--;
        linePtr++;
      }

      // skip past the pipe sign and the whitespace following it.
      while (*(++linePtr) == '\0') {}
      
      // connect the pipe's read end to our inPipe and recursively process the rest of the line!
      runcmd(linePtr, length, fd[0], outPipe);

      // close the other end of the pipe now that we are done using it.
      close(fd[0]);

      // return, we are done processing this line of shellcode. 
      return;
    }

    if (*nextChar == '\0') 
    { /*There is nothing special after this subcommand, so we just execute in a regular way*/
      fchild(args, inPipe, outPipe);
      return;
    }

    //else: some problem, so throw a fit.
    printf("ERROR: Invalid input: %c\n",*nextChar);
  }
}


int main(int argc, char *argv[])
{
  /*Your solution*/
  char lineIn[1024];

  while(1) 
  {
    if (fgets(lineIn,1024,stdin) == NULL)
      break;
              
    int len = 0;
    while (lineIn[len] != '\0')
      len++;
      
    /* remove the \n that fgets adds to the end */
    if (len != 0 && lineIn[len-1] == '\n')
    {
      lineIn[len-1] = '\0';
      len--;
    }
        
    //Run this string of subcommands with 0 as default input stream
    //and 1 as default output stream
    runcmd(lineIn, len,0,1);
  
    /*Wait for the child completes */
    pid_t pid = getpid();
    if (pid > 0) // parent process.
      waitpid(pid, NULL, 0);
  }

  return 0;
}
