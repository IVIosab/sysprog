#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

static void execute_expr(struct expr *expr)
{
    struct command *cmd = &expr->cmd;
    char **args = malloc((cmd->arg_count + 2) * sizeof(char *));
    args[0] = cmd->exe;
    for (uint32_t j = 0; j < cmd->arg_count; ++j)
    {
        args[j + 1] = cmd->args[j];
    }
    args[cmd->arg_count + 1] = NULL;

    print_expr_info(expr);
    switch (expr->type)
    {
    case EXPR_TYPE_COMMAND:
        pid_t p = fork();
        if (p == 0)
        {
            if (strcmp(args[0], "cd") == 0)
            {
                exit(254);
            }
            else if (strcmp(args[0], "exit") == 0)
            {
                exit(255);
            }
            else
            {
                execvp(args[0], args);
            }
        }
        else
        {
            int status;
            waitpid(p, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 254)
            {
                chdir(args[1]);
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) == 255)
            {
                exit(0);
            }
        }
        break;
    default:
        printf("other");
        break;
    }
}

static void
execute_command_line(const struct command_line *line)
{
    execute_expr(line->head);
}

int main(void)
{
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc;
    struct parser *p = parser_new();
    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0)
    {
        parser_feed(p, buf, rc);
        struct command_line *line = NULL;
        while (true)
        {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE)
            {
                printf("Error: %d\n", (int)err);
                continue;
            }
            execute_command_line(line);
            command_line_delete(line);
        }
    }
    parser_delete(p);
    return 0;
}
