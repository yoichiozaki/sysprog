#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_HISTORY_NO 3

int
main(void)
{
  char *prompt = getenv("PS2");
  char *line = NULL;
  int lineSize = 0;
  int index = 0;
  int history_no = 0;
  HIST_ENTRY *history = NULL;

  // readline呼び出し
  while (line = readline(prompt)) {
    lineSize = strlen(line);
    for (index = lineSize - 1; index >= 0; index--) {
      printf("%c", line[index]);
    }
    printf("\n");

    add_history(line);

    if (++history_no > MAX_HISTORY_NO) {
      history = remove_history(0);
      free(history);
    }

    // 入力文字列領域開放
    free(line);
  }
  printf("\n");

  clear_history();
  line = readline(prompt);
  printf(" last input = [ %s ]\n", line);
  free(line);

  return EXIT_SUCCESS;
}
