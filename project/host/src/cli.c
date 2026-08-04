#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s -m <2|4|8> -p <4|2> -g <guest1.img> [guest2.img ...] "
          "[-f <file1> [file2 ...]]\n"
          "\n"
          "Options:\n"
          "  -m, --memory <MB>    Guest physical memory size (2, 4 or 8)\n"
          "  -p, --page <size>    Page size: 4 (4KB) or 2 (2MB)\n"
          "  -g, --guest <imgs>   One or more guest image files\n"
          "  -f, --file <files>   Shared files between VMs (optional)\n",
          prog);
}

static int is_option(const char *arg) { return arg[0] == '-'; }

int parse_args(int argc, char **argv, struct config *cfg) {
  memset(cfg, 0, sizeof(*cfg));

  if (argc < 2) {
    print_usage(argv[0]);
    return -1;
  }

  int i = 1;
  int got_memory = 0, got_page = 0, got_guest = 0;

  while (i < argc) {
    if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--memory") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "Error: --memory requires a value\n");
        return -1;
      }
      int mb = atoi(argv[i]);
      if (mb != 2 && mb != 4 && mb != 8) {
        fprintf(stderr, "Error: --memory must be 2, 4, or 8 (got %d)\n", mb);
        return -1;
      }
      cfg->mem_size = (size_t)mb * 1024u * 1024u;
      got_memory = 1;
      i++;

    } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--page") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "Error: --page requires a value\n");
        return -1;
      }
      int ps = atoi(argv[i]);
      if (ps == 4) {
        cfg->page_size = 4096;
      } else if (ps == 2) {
        cfg->page_size = 2u * 1024u * 1024u;
      } else {
        fprintf(stderr, "Error: --page must be 4 (4KB) or 2 (2MB) (got %d)\n", ps);
        return -1;
      }
      got_page = 1;
      i++;

    } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--guest") == 0) {
      i++;
      if (i >= argc || is_option(argv[i])) {
        fprintf(stderr, "Error: --guest requires at least one image file\n");
        return -1;
      }
      while (i < argc && !is_option(argv[i])) {
        if (cfg->guest_count >= MAX_GUESTS) {
          fprintf(stderr, "Error: too many guest images (max %d)\n", MAX_GUESTS);
          return -1;
        }
        cfg->guest_images[cfg->guest_count++] = argv[i];
        i++;
      }
      got_guest = 1;

    } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
      i++;
      if (i >= argc || is_option(argv[i])) {
        fprintf(stderr, "Error: --file requires at least one file path\n");
        return -1;
      }
      while (i < argc && !is_option(argv[i])) {
        if (cfg->shared_file_count >= MAX_SHARED_FILES) {
          fprintf(stderr, "Error: too many shared files (max %d)\n", MAX_SHARED_FILES);
          return -1;
        }
        cfg->shared_files[cfg->shared_file_count++] = argv[i];
        i++;
      }

    } else {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      print_usage(argv[0]);
      return -1;
    }
  }

  if (!got_memory) {
    fprintf(stderr, "Error: --memory is required\n");
    return -1;
  }
  if (!got_page) {
    fprintf(stderr, "Error: --page is required\n");
    return -1;
  }
  if (!got_guest) {
    fprintf(stderr, "Error: --guest is required\n");
    return -1;
  }

  return 0;
}
