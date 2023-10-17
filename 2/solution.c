#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

static int count_pipes(const struct command_line *line)
{
    int cnt = 0;
    struct expr *expr = line->head;
    while (expr != NULL)
    {
        if (expr->type == EXPR_TYPE_PIPE)
            ++cnt;
        expr = expr->next;
    }
    return cnt;
}

static int execute_command_line(const struct command_line *line)
{
    int index = 0;
    int last_exit = -1;
    int last_exit_index = -1;
    int exit_code = 0;
    struct expr *expr = line->head;
    int outfd = -1;

    // Check for output redirection
    if (line->out_type == OUTPUT_TYPE_FILE_NEW)
    {
        outfd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd == -1)
        {
            perror("open");
            return 1;
        }
    }
    else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
    {
        outfd = open(line->out_file, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (outfd == -1)
        {
            perror("open");
            return 1;
        }
    }

    int pipes = count_pipes(line);
    int fd[pipes][2];

    // Create all required pipes upfront
    for (int i = 0; i < pipes; i++)
    {
        if (pipe(fd[i]) == -1)
        {
            perror("pipe");
            return 1;
        }
    }

    int currentPipe = 0;
    while (expr != NULL)
    {
        if (expr->type == EXPR_TYPE_COMMAND)
        {
            // Handle the cd command if present
            if (strcmp(expr->cmd.exe, "cd") == 0)
            {
                if (expr->cmd.arg_count < 1)
                {
                    fprintf(stderr, "cd: missing argument\n");
                    return 1;
                }
                if (chdir(expr->cmd.args[0]) != 0)
                {
                    perror("cd");
                }
                expr = expr->next;
                continue;
            }
            if (strcmp(expr->cmd.exe, "exit") == 0)
            {
                if (expr->cmd.arg_count == 1)
                {
                    last_exit = atoi(expr->cmd.args[0]);
                    last_exit_index = index - 1;
                }
            }

            pid_t p = fork();
            if (p == 0) // child
            {

                if (currentPipe > 0)
                {
                    dup2(fd[currentPipe - 1][0], STDIN_FILENO);
                }
                if (outfd != -1 && currentPipe == pipes)
                {
                    dup2(outfd, STDOUT_FILENO);
                }
                else if (currentPipe < pipes)
                {
                    dup2(fd[currentPipe][1], STDOUT_FILENO);
                }

                // Close all file descriptors for safety
                for (int i = 0; i < pipes; i++)
                {
                    close(fd[i][0]);
                    close(fd[i][1]);
                }
                // Handle the exit command if present
                if (strcmp(expr->cmd.exe, "exit") != 0)
                {
                    char *args[expr->cmd.arg_count + 2];
                    args[0] = expr->cmd.exe;
                    for (uint32_t j = 0; j < expr->cmd.arg_count; ++j)
                    {
                        args[j + 1] = expr->cmd.args[j];
                    }
                    args[expr->cmd.arg_count + 1] = NULL;
                    execvp(args[0], args);
                    perror("execvp");
                    exit(1); // Exit if execvp fails
                }
                else
                {
                    char *args[2] = {"test", NULL};
                    execvp(args[0], args);
                    perror("execvp");
                    exit(1); // Exit if execvp fails
                }
            }
            currentPipe++;
        }
        expr = expr->next;
        index++;
    }

    // Close all file descriptors in the parent
    for (int i = 0; i < pipes; i++)
    {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    if (outfd != -1)
    {
        close(outfd); // Close the output redirection file descriptor
    }

    // Wait for all child processes to complete
    // printf("last exit index: %d\n", last_exit_index);
    for (int i = 0; i <= pipes; i++)
    {
        // printf("Waiting for child %d\n", i);
        int status;
        wait(&status);
        exit_code = WEXITSTATUS(status);
        if (last_exit_index != -1 && last_exit_index == i)
        {
            // printf("last_exit: %d\n", last_exit);
            exit_code = last_exit;
        }
    }
    return exit_code;
}

int main(void)
{
    const size_t buf_size = 1024;
    char buf[buf_size];
    int e_code = 0;
    int rc;
    struct parser *p = parser_new();
    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0)
    {
        // printf("rc: %d\n", rc);
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

            int cnt = count_pipes(line);

            if (!cnt && line->head && line->tail && line->head->type == EXPR_TYPE_COMMAND && line->tail->type == EXPR_TYPE_COMMAND && strcmp(line->tail->cmd.exe, "exit") == 0 && strcmp(line->head->cmd.exe, "exit") == 0)
            {
                if (line->head->cmd.arg_count == 1)
                {
                    e_code = atoi(line->head->cmd.args[0]);
                }
                command_line_delete(line);
                parser_delete(p);
                exit(e_code);
            }

            // printf("Executing command %s\n", line->head->cmd.exe);
            e_code = execute_command_line(line);
            command_line_delete(line);
        }
    }
    parser_delete(p);
    return e_code;
}
