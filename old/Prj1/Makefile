all: mycompress mydecompress forkcompress pipecompress parfork minishell moreshell dupshell parthread bench

mycompress: MyCompress.c
	gcc -o mycompress MyCompress.c

mydecompress: MyDecompress.c
	gcc -o mydecompress MyDecompress.c

forkcompress: ForkCompress.c
	gcc -o forkcompress ForkCompress.c

pipecompress: PipeCompress.c
	gcc -o pipecompress PipeCompress.c

parfork: ParFork.c
	gcc -o parfork ParFork.c

minishell: MiniShell.c
	gcc -o minishell MiniShell.c

moreshell: MoreShell.c
	gcc -o moreshell MoreShell.c

dupshell: DupShell.c
	gcc -o dupshell DupShell.c

parthread: ParThread.c
	gcc -pthread -o parthread ParThread.c

bench: bench.c
	gcc -o bench bench.c

clean:
	rm -f mycompress mydecompress forkcompress pipecompress parfork minishell moreshell dupshell parthread bench

