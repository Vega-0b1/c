
all: p1/reverse_server p1/reverse_client p2/ls_server p2/ls_client p3/disk_server p3/disk_client p3/disk_rand p4_p5/file_system_server p4_p5/file_system_client

p1/reverse_server: p1/reverse_server.c
	gcc -pthread p1/reverse_server.c -o p1/reverse_server

p1/reverse_client: p1/reverse_client.c
	gcc p1/reverse_client.c -o p1/reverse_client

p2/ls_server: p2/ls_server.c
	gcc p2/ls_server.c -o p2/ls_server

p2/ls_client: p2/ls_client.c
	gcc p2/ls_client.c -o p2/ls_client

p3/disk_server: p3/disk_server.c
	gcc p3/disk_server.c -o p3/disk_server

p3/disk_client: p3/disk_client.c
	gcc p3/disk_client.c -o p3/disk_client

p3/disk_rand: p3/disk_rand.c
	gcc p3/disk_rand.c -o p3/disk_rand

p4_p5/file_system_server: p4_p5/file_system_server.c
	gcc p4_p5/file_system_server.c -o p4_p5/file_system_server

p4_p5/file_system_client: p4_p5/file_system_client.c
	gcc p4_p5/file_system_client.c -o p4_p5/file_system_client

clean:
	rm -f p1/reverse_server p1/reverse_client p2/ls_server p2/ls_client p3/disk_server p3/disk_client p3/disk_rand p4_p5/file_system_server p4_p5/file_system_client
