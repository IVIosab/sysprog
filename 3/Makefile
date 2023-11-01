GCC_FLAGS_MEM_LEAK = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic
GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: test.o userfs.o
	gcc $(GCC_FLAGS) test.o userfs.o

all_mem_leak: test.o userfs.o
	gcc $(GCC_FLAGS_MEM_LEAK) test.o userfs.o ../utils/heap_help/heap_help.c

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o -I ../utils

userfs.o: userfs.c
	gcc $(GCC_FLAGS) -c userfs.c -o userfs.o
