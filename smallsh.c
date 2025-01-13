#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <stdbool.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char *expand(char const *word);
void handle_background_processes();
char *build_str(char const *start, char const *end);
int fg_pid;
int bg_pid;
bool background;


//does nothing for when getting user input
void handle_SIGINT(int signo){

    return;
}

int main(int argc, char * argv[]) {

  FILE * input = stdin;
  char * input_fn = "(stdin)";
  fg_pid = 0;
  bg_pid = 0;
  background = false;

  struct sigaction SIGINT_old_action = {0};
  struct sigaction SIGTSTP_old_action = {0};

  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) {
      perror("fopen");
      exit(1);
    }
  } else if (argc > 2) {
    fprintf(stderr, "too many arguments\n");
    exit(1);
  }

  char * line = NULL;
  size_t n = 0;

  struct sigaction SIGINT_action = {0};
  struct sigaction ignore_action = {0};

  //set SIGTSTP and SIGINT to ignore
  ignore_action.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &ignore_action, &SIGINT_old_action);
  sigaction(SIGINT, &ignore_action, &SIGTSTP_old_action);



  while (1) {

 //take care of background processes
    handle_background_processes();
    prompt:
    if (input == stdin) {

      //interactive mode
      char * ps1 = getenv("PS1");
      if (ps1 == NULL) {
        fprintf(stderr, "%s", "");
      } else if (ps1 != NULL) {
        fprintf(stderr, "%s", getenv("PS1"));
      }

    }

    //set SIGINT to the do nothing handler for input
    SIGINT_action.sa_handler = handle_SIGINT;
    sigaction(SIGINT, &SIGINT_action, NULL);


    //get input
    ssize_t line_len = getline( &line, &n, input);

   //set SIGINT back to ignore after input
    sigaction(SIGINT, &ignore_action, NULL);

    //if feof exit
    if (feof(input) != 0) {
      clearerr(stdin);
      exit(0);
    }

    //if getline returned error
    if (line_len == -1) {
      {
        if (errno == EINTR) {
          clearerr(input);
          fprintf(stderr, "\n");
          goto prompt;
        }

      }
    }

    //prevents new line crashes
    if (strcmp(line, "\n") == 0) {
      goto prompt;
    }

//populate words array with the expanded words
    size_t nwords = wordsplit(line);
    for (size_t i = 0; i < nwords; ++i) {
      char * exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
    }


    //check command of first word
    if(strcmp(words[0], "exit") == 0){
        goto exit_cmd;
    } 

    else if(strcmp(words[0], "cd") == 0){
        goto cd_cmd;
    }

    else{
      goto not_built_in;
    }


exit_cmd:
    //check for exit command and arguements
    if (strcmp(words[0], "exit") == 0) {
      // Exit Function
      if (nwords == 1) {
        exit(fg_pid);
      }
      if (nwords == 2) {
        char str[10];
        char * ptr;
        long value;

        //adopted from https://blog.udemy.com/c-string-to-int/
        strcpy(str, words[1]);
        value = strtol(str, & ptr, 10);
        if ( * ptr == '\0') {
          exit(value);
        } else {
          err(1, "%s", "Numeric arguement required");
        }
      }
      if (nwords > 2) {
        err(1, "%s", "Too many arguements");
      }
    }


cd_cmd:
  //check for cd command and arguements
    if (strcmp(words[0], "cd") == 0) {
      if (nwords == 1) {
        if (chdir(getenv("HOME")) == 0) {
          goto prompt;
        } else {
          err(1, "%s", "No such file or directory");
        }
      }
      if (nwords > 1) {
        if (chdir(words[1]) == 0) {
          goto prompt;
        } else {
          err(1, "%s", "No such file or directory");
        }
      }
      if (nwords > 2)
        err(1, "%s", "Too many arguements");

      goto prompt;
    }

not_built_in:


  //last & determines background
  if (memcmp(words[nwords - 1], "&", 1) == 0) {
      words[nwords - 1] = '\0';
      nwords -= 1;
      background = true;
    }
     //create child
    int child_status = -5;
    pid_t child_pid = fork();

    if (child_pid == -1) {
      perror("fork() failed");
      exit(1);
    }
    else if (child_pid ==0 ) {

      // Restore old signal handlers for child process
      sigaction(SIGINT, &SIGINT_old_action, NULL);
      sigaction(SIGTSTP, &SIGTSTP_old_action, NULL);


      // Prepare commands array
      char ** other_cmds = malloc((nwords + 1) * sizeof(char * ));
      if (other_cmds == NULL) {
        perror("malloc");
        exit(1);
      }
      
      int cmd_size = 0;
      int file_input = STDIN_FILENO; // Input file descriptor
      int file_output = -1; // Output file descriptor

      for (size_t i = 0; i < nwords; ++i) {
        if (memcmp(words[i], ">", 1) == 0 || memcmp(words[i], ">>", 2) == 0) {
          // Handle output redirection
          file_output = open(words[i + 1], O_WRONLY | O_CREAT | ((memcmp(words[i], ">>", 2) == 0) ? O_APPEND : O_TRUNC), 0777);
          if (file_output == -1) {
            perror("open output");
            exit(1);
          }
          i++; // Skip the next word as it's the filename
        } else if (memcmp(words[i], "<", 1) == 0) {
          // Handle input redirection
          file_input = open(words[i + 1], O_RDONLY, 0);
          if (file_input == -1) {
            perror("open input");
            exit(1);
          }
          i++; // Skip the next word as it's the filename
        } else{

          //put remaining commands in array
          if (memcmp(words[i], "<", 1) != 0 && memcmp(words[i], ">",1) != 0 && memcmp(words[i], ">>", 2) != 0){
            other_cmds[cmd_size] = strdup(words[i]);

            cmd_size++;
        }
        }
      }

      other_cmds[cmd_size] = '\0'; // Null-terminate the arguments array

      if (file_input != STDIN_FILENO) {
        if (dup2(file_input, STDIN_FILENO) == -1) {
          perror("dup2 input");
          exit(1);
        }
        close(file_input);
      }

      if (file_output != -1) {
        if (dup2(file_output, STDOUT_FILENO) == -1) {
          perror("dup2 output");
          exit(1);
        }
        close(file_output);
      }

  // Execute the commands using execvp
      execvp(other_cmds[0], other_cmds);

      // If execvp fails, print an error message and exit
      perror("execvp");

          // Clean up
      for (size_t i = 0; i < cmd_size; ++i) {
        free(other_cmds[i]);
      }
      free(other_cmds);
      _exit(EXIT_SUCCESS);
      break;
    }
    else {

        // This block is executed if the command is intended to run in the background.
      if (background == true) {

        // Add the child process PID to the bg pid
        bg_pid = child_pid;


      } else {
        // This block is executed if the command is intended to run in the foreground.

        // Wait for the child process to complete and handle its status.
        waitpid(child_pid, &child_status, WUNTRACED);
        
        // Check if the child process terminated normally or was stopped by a signal.
        if (WIFEXITED(child_status)) {
          // The child process terminated normally.

          // Extract the exit status and use it as needed.
          int exit_status = WEXITSTATUS(child_status);
          fg_pid = exit_status;
          //printf("Foreground process %d terminated with exit status: %d\n", child_pid, exit_status);

        }
        else if (WIFSIGNALED(child_status)) {
          // The child process was terminated by a signal.

          // Extract the signal number and use it as needed.
          int signal_number = WTERMSIG(child_status);
          bg_pid = child_pid;

        //shell variable shall be set to value 128 + [n]
          fg_pid = signal_number + 128;

          //printf("Foreground process %d terminated by signal: %d\n", child_pid, signalNumber);

        }
        else if (WIFSTOPPED(child_status)) {
          // The child process was stopped by a signal (e.g. Ctrl+Z).


          // Extract the signal number and use it as needed.
          int signal_number = WSTOPSIG(child_status);
          bg_pid = signal_number;
          kill(child_pid, SIGCONT);
          //goto prompt;
          //printf("Foreground process %d stopped by signal: %d\n", child_pid, signalNumber);
        }

      }

    }

    

  }

  //free resources
  free(line);
  fclose(input);
  exit(0);
}

// Function to handle background process signals and output their status
void handle_background_processes() {
  int child_status;
  pid_t child_pid;

  // Check if there are background processes to handle
  if (background == true) {
    // Iterate through background processes using waitpid with non-blocking options
    while ((child_pid = waitpid(bg_pid, &child_status, WNOHANG | WUNTRACED)) > 0) {
      // Check if the background process has terminated normally
      if (WIFEXITED(child_status)) {
        fprintf(stderr, "Background process %jd done. Exit status %d.\n", (intmax_t) child_pid, WEXITSTATUS(child_status));
      }
      // Check if the background process was stopped
      else if (WIFSTOPPED(child_status)) {
        fprintf(stderr, "Background process %jd stopped. Continuing.\n", (intmax_t) child_pid);
        // Continue the stopped process
        kill(child_pid, SIGCONT);

      }
      // Check if the background process was terminated by a signal
      else if (WIFSIGNALED(child_status)) {
        fprintf(stderr, "Background process %jd done. Signaled %d.\n", (intmax_t) child_pid, WTERMSIG(child_status));
      }
    }

    // Flush standard error stream to ensure output is displayed
    fflush(stderr);

    // Reset the background flag after handling background processes
    background = false;
  }

  // Flush standard error stream once again
  fflush(stderr);
}


/* Splits a string into words delimited by whitespace. Recognizes
 comments as '#' at the beginning of a word, and backslash escapes.
 
 Returns number of words parsed, and updates the words[] array
  with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char
  const * line) {
  size_t wlen = 0;
  size_t wind = 0;

  char
  const * c = line;
  for (;* c && isspace( * c); ++c);

  for (;* c;) {
    if (wind == MAX_WORDS) {
      break;
    }

    if ( * c == '#') {
      break; // ignore comments
    }

    for (;* c && !isspace( * c); ++c) {
      if ( * c == '\\') {
        ++c; // skip escaped character
      }

      void * tmp = realloc(words[wind], sizeof ** words * (wlen + 2));
      if (!tmp) {
        perror("realloc");
        exit(1);
      }
      words[wind] = tmp;
      words[wind][wlen++] = * c;
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;* c && isspace( * c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 start and end pointers to the start and end of the parameter
 token.
*/
char param_scan(char
  const * word, char
  const ** start, char
  const ** end) {
  static char
  const * prev;
  if (!word) {
    word = prev;
  }

  char ret = 0;
  * start = NULL;
  * end = NULL;

  for (char
    const * s = word;* s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) {
      break;
    }

    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      * start = s;
      * end = s + 2;
      break;

    case '{':
      char * e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        * start = s;
        * end = e + 1;
      }
      break;
    }
  }
  prev = * end;
  return ret;
}

/* Simple string-builder function. Builds up a base
* string by appending supplied strings/character ranges
* to it.
 */
char * build_str(char
  const * start, char
  const * end) {
  static size_t base_len = 0;
  static char * base = NULL;

  if (!start) {
    char * ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof * base * (base_len + n + 1);
  void * tmp = realloc(base, newsize);
  if (!tmp) {
    perror("realloc");
    exit(1);
  }
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}


/* Expands all instances of $! $$ $? and ${param} in a string 
 Returns a newly allocated string that the caller must free
*/
char * expand(char
  const * word) {
  char
  const * pos = word;
  char
  const * start, * end;
  char c = param_scan(pos, & start, & end);
  build_str(NULL, NULL);
  build_str(pos, start);

  while (c) {

    // process ID of the most recent background process 
    if (c == '!') {
      char pid_child[100] = {'\0'};
      if (bg_pid == 0){
        build_str("", NULL);
      }
      else{
        sprintf(pid_child, "%d", bg_pid);
        build_str(pid_child, NULL);
      }
      // proccess ID of the smallsh process
    } else if (c == '$') {
      int this_pid = getpid();
      char pid_str[100] = {'\0'};
      sprintf(pid_str, "%d", this_pid);
      build_str(pid_str, NULL);

       //exit status of the last foreground command 
    } else if (c == '?') {

      char pid_child[100] = {'\0'};
      sprintf(pid_child, "%d", fg_pid);
      build_str(pid_child, NULL);


      //${env var}
    } else if (c == '{') {
      char var_str[100] = {'\0'};
      strncpy(var_str, start + 2, (end - 1) - (start + 2));
      char * this_var = getenv(var_str);
      if (this_var == NULL) {
        build_str("", NULL);
      } else {
        build_str(this_var, NULL);
      }
    }
    pos = end;
    c = param_scan(pos, & start, & end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}



