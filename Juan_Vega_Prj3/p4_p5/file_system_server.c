
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK_SIZE 128
#define MAX_LINE 4096
#define MAX_FILES 256
#define MAX_NAME 64

#define FS_PORT_DEFAULT 7790
#define DISK_PORT_DEFAULT 7780

// one in-memory directory entry
struct fs_entry {
  int used;        // 0 = free, 1 = in use
  int is_dir;      // 0 = file, 1 = directory
  int parent;      // index of parent directory
  int first_block; // first disk block for file
  int nblocks;     // number of blocks
  int size;        // file size in bytes
  char name[MAX_NAME];
};

static struct fs_entry fs_table[MAX_FILES];
static int fs_initialized = 0;
static int cwd_index = 0;

static int disk_sock = -1;
static int cylinders = 0;
static int sectors = 0;
static int total_blocks = 0;
static char *block_used = NULL; // bitmap for allocated blocks

// generic I/O helpers
static ssize_t send_all(int fd, const void *buf, size_t n);
static ssize_t recv_all(int fd, void *buf, size_t n);
static ssize_t recv_line(int fd, char *buf, size_t cap);

// disk helpers
static int disk_connect(const char *ip, int port);
static int disk_write_block(int blk, const unsigned char *data, int len);
static int disk_read_block(int blk, unsigned char *data);

// filesystem helpers
static int fs_alloc_entry(void);
static int fs_find(const char *name, int is_dir, int parent);
static int fs_alloc_contiguous(int blocks);
static void fs_free_blocks(int first, int blocks);
static int fs_format(void);
static int fs_create_file(const char *name);
static int fs_delete_file(const char *name);
static int fs_write_file(const char *name, const unsigned char *data, int len);
static int fs_read_file(const char *name, unsigned char **out_data,
                        int *out_len);
static int fs_list(int verbose, int client_fd);
static int fs_mkdir(const char *name);
static int fs_cd(const char *name);
static int fs_pwd(char *buf, size_t cap);
static int fs_rmdir(const char *name);
static void serve_client(int client_fd);

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <disk_server_ip>\n", argv[0]);
    return 1;
  }

  const char *disk_ip = argv[1];

  // connect to lower-level disk server once at startup
  if (disk_connect(disk_ip, DISK_PORT_DEFAULT) < 0) {
    fprintf(stderr, "failed to connect to disk server\n");
    return 1;
  }

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("fs socket");
    return 1;
  }

  int yes = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  sa.sin_port = htons(FS_PORT_DEFAULT);

  if (bind(listen_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("bind fs");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 16) < 0) {
    perror("listen fs");
    close(listen_fd);
    return 1;
  }

  fprintf(stderr, "file_system_server listening on port %d\n", FS_PORT_DEFAULT);

  // simple single-threaded loop: serve one client at a time
  while (1) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR)
        continue;
      perror("accept");
      continue;
    }

    serve_client(client_fd);
    close(client_fd);
  }

  close(listen_fd);
  return 0;
}

/* --------------- generic I/O helpers --------------- */

// send exactly n bytes (unless error)
static ssize_t send_all(int fd, const void *buf, size_t n) {
  size_t off = 0;
  const char *p = buf;

  while (off < n) {
    ssize_t r = send(fd, p + off, n - off, 0);
    if (r <= 0) {
      if (r < 0 && errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)r;
  }
  return (ssize_t)off;
}

// recv exactly n bytes (or return 0 on EOF)
static ssize_t recv_all(int fd, void *buf, size_t n) {
  size_t off = 0;
  char *p = buf;

  while (off < n) {
    ssize_t r = recv(fd, p + off, n - off, 0);
    if (r == 0)
      return 0;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)r;
  }
  return (ssize_t)off;
}

// read one line (up to and including '\n')
static ssize_t recv_line(int fd, char *buf, size_t cap) {
  size_t n = 0;

  while (n < cap) {
    char c;
    ssize_t r = recv(fd, &c, 1, 0);
    if (r == 0)
      break;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    buf[n++] = c;
    if (c == '\n')
      break;
  }
  return (ssize_t)n;
}

/* --------------- disk helpers --------------- */

// connect to disk server and read geometry
static int disk_connect(const char *ip, int port) {
  disk_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (disk_sock < 0) {
    perror("disk socket");
    return -1;
  }

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
    fprintf(stderr, "bad disk ip\n");
    close(disk_sock);
    return -1;
  }

  if (connect(disk_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("connect disk");
    close(disk_sock);
    return -1;
  }

  if (send_all(disk_sock, "I\n", 2) < 0) {
    perror("send I");
    return -1;
  }

  char buf[64];
  ssize_t n = recv(disk_sock, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    perror("recv I");
    return -1;
  }
  buf[n] = '\0';

  if (sscanf(buf, "%d %d", &cylinders, &sectors) != 2 || cylinders <= 0 ||
      sectors <= 0) {
    fprintf(stderr, "bad disk geometry: %s\n", buf);
    return -1;
  }

  total_blocks = cylinders * sectors;
  fprintf(stderr, "disk: C=%d S=%d blocks=%d\n", cylinders, sectors,
          total_blocks);
  return 0;
}

// write one logical block to disk server
static int disk_write_block(int blk, const unsigned char *data, int len) {
  if (blk < 0 || blk >= total_blocks || len < 0 || len > BLOCK_SIZE)
    return -1;

  int c = blk / sectors;
  int s = blk % sectors;

  char cmd[64];
  int n = snprintf(cmd, sizeof(cmd), "W %d %d %d\n", c, s, len);
  if (send_all(disk_sock, cmd, (size_t)n) < 0)
    return -1;

  if (len > 0 && data) {
    if (send_all(disk_sock, data, (size_t)len) < 0)
      return -1;
  }

  if (send_all(disk_sock, "\n", 1) < 0)
    return -1;

  char ans[2];
  ssize_t r = recv(disk_sock, ans, sizeof(ans), 0);
  if (r <= 0)
    return -1;

  return (ans[0] == '1') ? 0 : -1;
}

// read one logical block from disk server
static int disk_read_block(int blk, unsigned char *data) {
  if (blk < 0 || blk >= total_blocks)
    return -1;

  int c = blk / sectors;
  int s = blk % sectors;

  char cmd[64];
  int n = snprintf(cmd, sizeof(cmd), "R %d %d\n", c, s);
  if (send_all(disk_sock, cmd, (size_t)n) < 0)
    return -1;

  char tag;
  if (recv_all(disk_sock, &tag, 1) <= 0)
    return -1;
  if (tag != '1')
    return -1;

  if (recv_all(disk_sock, data, BLOCK_SIZE) <= 0)
    return -1;
  return 0;
}

/* --------------- filesystem helpers --------------- */

// allocate an unused fs_table slot (skip 0, root)
static int fs_alloc_entry(void) {
  for (int i = 1; i < MAX_FILES; i++)
    if (!fs_table[i].used)
      return i;
  return -1;
}

// find entry by name / type / parent
static int fs_find(const char *name, int is_dir, int parent) {
  for (int i = 0; i < MAX_FILES; i++) {
    if (!fs_table[i].used)
      continue;
    if (fs_table[i].parent != parent)
      continue;
    if (fs_table[i].is_dir != is_dir)
      continue;
    if (strncmp(fs_table[i].name, name, MAX_NAME) == 0)
      return i;
  }
  return -1;
}

// find contiguous free blocks in bitmap
static int fs_alloc_contiguous(int blocks) {
  if (blocks <= 0)
    return -1;

  for (int start = 0; start + blocks <= total_blocks; start++) {
    int ok = 1;
    for (int i = 0; i < blocks; i++) {
      if (block_used[start + i]) {
        ok = 0;
        break;
      }
    }
    if (!ok)
      continue;
    for (int i = 0; i < blocks; i++)
      block_used[start + i] = 1;
    return start;
  }
  return -1;
}

// mark a run of blocks as free
static void fs_free_blocks(int first, int blocks) {
  if (first < 0 || blocks <= 0)
    return;
  for (int i = 0; i < blocks; i++) {
    int b = first + i;
    if (b >= 0 && b < total_blocks)
      block_used[b] = 0;
  }
}

// reset in-memory FS and bitmap
static int fs_format(void) {
  memset(fs_table, 0, sizeof(fs_table));

  free(block_used);
  block_used = calloc((size_t)total_blocks, 1);
  if (!block_used)
    return -1;

  fs_table[0].used = 1;
  fs_table[0].is_dir = 1;
  fs_table[0].parent = 0;
  fs_table[0].first_block = -1;
  fs_table[0].nblocks = 0;
  fs_table[0].size = 0;
  strncpy(fs_table[0].name, "/", MAX_NAME - 1);

  cwd_index = 0;
  fs_initialized = 1;
  return 0;
}

// create an empty file in cwd
static int fs_create_file(const char *name) {
  if (fs_find(name, 0, cwd_index) >= 0)
    return 1;

  int idx = fs_alloc_entry();
  if (idx < 0)
    return 2;

  fs_table[idx].used = 1;
  fs_table[idx].is_dir = 0;
  fs_table[idx].parent = cwd_index;
  fs_table[idx].first_block = -1;
  fs_table[idx].nblocks = 0;
  fs_table[idx].size = 0;
  strncpy(fs_table[idx].name, name, MAX_NAME - 1);
  fs_table[idx].name[MAX_NAME - 1] = '\0';
  return 0;
}

// delete file and free its blocks
static int fs_delete_file(const char *name) {
  int idx = fs_find(name, 0, cwd_index);
  if (idx < 0)
    return 1;

  fs_free_blocks(fs_table[idx].first_block, fs_table[idx].nblocks);
  memset(&fs_table[idx], 0, sizeof(fs_table[idx]));
  return 0;
}

// write full contents of file (overwrite)
static int fs_write_file(const char *name, const unsigned char *data, int len) {
  int idx = fs_find(name, 0, cwd_index);
  if (idx < 0)
    return 1;

  struct fs_entry *e = &fs_table[idx];

  int needed = (len <= 0) ? 0 : ((len + BLOCK_SIZE - 1) / BLOCK_SIZE);

  fs_free_blocks(e->first_block, e->nblocks);
  e->first_block = -1;
  e->nblocks = 0;
  e->size = 0;

  if (needed == 0)
    return 0;

  int first = fs_alloc_contiguous(needed);
  if (first < 0)
    return 2;

  e->first_block = first;
  e->nblocks = needed;
  e->size = len;

  int off = 0;
  for (int i = 0; i < needed; i++) {
    unsigned char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    int chunk = BLOCK_SIZE;
    if (off + chunk > len)
      chunk = len - off;
    if (chunk > 0)
      memcpy(buf, data + off, (size_t)chunk);

    if (disk_write_block(first + i, buf, BLOCK_SIZE) < 0)
      return 2;

    off += chunk;
  }
  return 0;
}

// read file contents into newly allocated buffer
static int fs_read_file(const char *name, unsigned char **out_data,
                        int *out_len) {
  int idx = fs_find(name, 0, cwd_index);
  if (idx < 0)
    return 1;

  struct fs_entry *e = &fs_table[idx];
  *out_len = e->size;

  if (e->size == 0) {
    *out_data = NULL;
    return 0;
  }

  unsigned char *buf = malloc((size_t)e->size);
  if (!buf)
    return 2;

  int off = 0;
  for (int i = 0; i < e->nblocks; i++) {
    unsigned char blk[BLOCK_SIZE];
    if (disk_read_block(e->first_block + i, blk) < 0) {
      free(buf);
      return 2;
    }
    int chunk = BLOCK_SIZE;
    if (off + chunk > e->size)
      chunk = e->size - off;
    memcpy(buf + off, blk, (size_t)chunk);
    off += chunk;
  }

  *out_data = buf;
  return 0;
}

// list entries in current directory
static int fs_list(int verbose, int client_fd) {
  int count = 0;
  for (int i = 0; i < MAX_FILES; i++) {
    if (!fs_table[i].used)
      continue;
    if (fs_table[i].parent != cwd_index)
      continue;
    count++;
  }

  dprintf(client_fd, "0 %d\n", count);

  for (int i = 0; i < MAX_FILES; i++) {
    if (!fs_table[i].used)
      continue;
    if (fs_table[i].parent != cwd_index)
      continue;

    if (!verbose) {
      dprintf(client_fd, "%s%s\n", fs_table[i].name,
              fs_table[i].is_dir ? "/" : "");
    } else {
      char t = fs_table[i].is_dir ? 'D' : 'F';
      dprintf(client_fd, "%c %s %d\n", t, fs_table[i].name, fs_table[i].size);
    }
  }
  return 0;
}

// create new directory under cwd
static int fs_mkdir(const char *name) {
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 2;

  if (fs_find(name, 0, cwd_index) >= 0 || fs_find(name, 1, cwd_index) >= 0)
    return 1;

  int idx = fs_alloc_entry();
  if (idx < 0)
    return 2;

  fs_table[idx].used = 1;
  fs_table[idx].is_dir = 1;
  fs_table[idx].parent = cwd_index;
  fs_table[idx].first_block = -1;
  fs_table[idx].nblocks = 0;
  fs_table[idx].size = 0;
  strncpy(fs_table[idx].name, name, MAX_NAME - 1);
  fs_table[idx].name[MAX_NAME - 1] = '\0';
  return 0;
}

// change current working directory
static int fs_cd(const char *name) {
  if (strcmp(name, "/") == 0) {
    cwd_index = 0;
    return 0;
  }
  if (strcmp(name, "..") == 0) {
    cwd_index = fs_table[cwd_index].parent;
    return 0;
  }

  int idx = fs_find(name, 1, cwd_index);
  if (idx < 0)
    return 1;
  cwd_index = idx;
  return 0;
}

// build absolute path of cwd into buf
static int fs_pwd(char *buf, size_t cap) {
  if (cwd_index == 0) {
    strncpy(buf, "/", cap);
    buf[cap - 1] = '\0';
    return 0;
  }

  const char *parts[MAX_FILES];
  int n = 0;
  int cur = cwd_index;

  while (cur != 0 && n < MAX_FILES) {
    parts[n++] = fs_table[cur].name;
    cur = fs_table[cur].parent;
  }

  size_t off = 0;
  buf[off++] = '/';

  for (int i = n - 1; i >= 0; i--) {
    size_t len = strlen(parts[i]);
    if (off + len + 1 >= cap)
      break;
    memcpy(buf + off, parts[i], len);
    off += len;
    if (i != 0)
      buf[off++] = '/';
  }

  buf[off] = '\0';
  return 0;
}

// remove empty directory
static int fs_rmdir(const char *name) {
  int idx = fs_find(name, 1, cwd_index);
  if (idx < 0)
    return 1;

  for (int i = 0; i < MAX_FILES; i++) {
    if (!fs_table[i].used)
      continue;
    if (fs_table[i].parent == idx)
      return 2; // directory not empty
  }

  memset(&fs_table[idx], 0, sizeof(fs_table[idx]));
  return 0;
}

/* --------------- per-client handler --------------- */

// handle one client connection until they disconnect
static void serve_client(int client_fd) {
  char line[MAX_LINE];

  cwd_index = 0; // each client starts at root

  while (1) {
    ssize_t n = recv_line(client_fd, line, sizeof(line));
    if (n <= 0)
      break;

    if (n == sizeof(line))
      n--;
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0';

    if (line[0] == '\0')
      continue;

    char cmd[16] = {0};
    if (sscanf(line, " %15s", cmd) != 1) {
      dprintf(client_fd, "2\n");
      continue;
    }

    if (strcmp(cmd, "F") == 0) {
      int rc = fs_format();
      dprintf(client_fd, "%d\n", (rc == 0) ? 0 : 2);

    } else if (strcmp(cmd, "C") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " C %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      int rc = fs_create_file(name);
      dprintf(client_fd, "%d\n", rc);

    } else if (strcmp(cmd, "D") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " D %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      int rc = fs_delete_file(name);
      dprintf(client_fd, "%d\n", rc);

    } else if (strcmp(cmd, "L") == 0) {
      int verbose = 0;
      if (sscanf(line, " L %d", &verbose) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      fs_list(verbose, client_fd);

    } else if (strcmp(cmd, "R") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " R %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }

      unsigned char *data = NULL;
      int len = 0;
      int rc = fs_read_file(name, &data, &len);

      if (rc != 0) {
        dprintf(client_fd, "%d 0\n", rc);
      } else {
        dprintf(client_fd, "0 %d\n", len);
        if (len > 0 && data) {
          if (send_all(client_fd, data, (size_t)len) < 0) {
            free(data);
            break;
          }
        }
        if (send_all(client_fd, "\n", 1) < 0) {
          free(data);
          break;
        }
      }
      free(data);

    } else if (strcmp(cmd, "W") == 0) {
      char name[MAX_NAME];
      int len = 0;
      if (sscanf(line, " W %63s %d", name, &len) != 2 || len < 0) {
        dprintf(client_fd, "2\n");
        continue;
      }

      unsigned char *buf = NULL;
      if (len > 0) {
        buf = malloc((size_t)len);
        if (!buf) {
          dprintf(client_fd, "2\n");
          continue;
        }
        if (recv_all(client_fd, buf, (size_t)len) <= 0) {
          free(buf);
          break;
        }
        char nl;
        if (recv_all(client_fd, &nl, 1) <= 0 || nl != '\n') {
          free(buf);
          break;
        }
      }

      int rc = fs_write_file(name, buf, len);
      free(buf);
      dprintf(client_fd, "%d\n", rc);

    } else if (strcmp(cmd, "mkdir") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " mkdir %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      int rc = fs_mkdir(name);
      dprintf(client_fd, "%d\n", rc);

    } else if (strcmp(cmd, "cd") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " cd %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      int rc = fs_cd(name);
      dprintf(client_fd, "%d\n", rc);

    } else if (strcmp(cmd, "pwd") == 0) {
      char buf2[1024];
      fs_pwd(buf2, sizeof(buf2));
      dprintf(client_fd, "0 %s\n", buf2);

    } else if (strcmp(cmd, "rmdir") == 0) {
      char name[MAX_NAME];
      if (sscanf(line, " rmdir %63s", name) != 1) {
        dprintf(client_fd, "2\n");
        continue;
      }
      int rc = fs_rmdir(name);
      dprintf(client_fd, "%d\n", rc);

    } else {
      dprintf(client_fd, "2\n");
    }
  }
}
