GCC_FLAGS_MEM_LEAK = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic
GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: parser.c solution.c
	gcc $(GCC_FLAGS) parser.c solution.c

all_mem_leak: parser.c solution.c
	gcc $(GCC_FLAGS_MEM_LEAK) parser.c solution.c ../utils/heap_help/heap_help.c

clean: 
	rm -f *.out