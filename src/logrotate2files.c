/*
 ============================================================================
 Name        : logrotate2files.c
 Author      : ukaszyk
 Version     :
 Copyright   : private
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <semaphore.h>

#define LOG_SIZE_LIMIT  512

struct log_rotate_file {
  FILE *logfile;         // pointers to files streams
  int file_pos;          // position to write
  int file_full;         // file written to full and already rotated
  pthread_mutex_t file_mx;
};

struct test_container {
  struct log_rotate_file *log;
  char *input_file_name;
  pthread_mutex_t mutex;
};

static int initLog(struct log_rotate_file *log);
static int putLineToLog(const char *line, ssize_t len, struct log_rotate_file *log);
static void log_destroy(struct log_rotate_file *log);
void* WriterThread(void *arg);
void* ReaderThread(void *arg);
void print_log_file(FILE *file, int starting_pos);

void generate_input_file(const char *name)
{
  FILE *fd = NULL;
  int i;
  char buf[64];

  fd = fopen(name, "w+");
  if (fd == NULL)
  {
    printf("Error opening file %s\n", name);
    return;
  }
  srand(time(NULL));
  for(i = 0; i < 100000; i++)
  {
    snprintf(buf, sizeof(buf), "%d-Testowa linijka numer %d\n", i, (rand() % 10000));
    fputs(buf, fd);
  }
  fclose(fd);
}

static struct test_container container = {NULL, NULL, PTHREAD_MUTEX_INITIALIZER};
/*
 *
 */
int main(int argc, char **argv)
{
	pthread_t writer_tid, reader_tid;
	struct log_rotate_file log = { NULL, 0, 0, PTHREAD_MUTEX_INITIALIZER};
	int iret;

	if (argc != 2)
	{
	  printf("Wrong parameter\nGive only input file name as parameter.\n");
	  return EXIT_FAILURE;
	}

//	generate_input_file(argv[1]);
//  return 0;
	container.log = &log;
	container.input_file_name = argv[1];

	if (initLog(&log) != EXIT_SUCCESS)
	{
	  printf("Error initializing LOG file\n");
	  return EXIT_FAILURE;
	}

	iret = pthread_create(&writer_tid, NULL, WriterThread, (void*)&container);
	if (iret)
	{
	    fprintf(stderr,"Error - pthread_create(WriterThread) return code: %d\n",iret);
	    log_destroy(&log);
	    exit(EXIT_FAILURE);
	}

	iret = pthread_create(&reader_tid, NULL, ReaderThread, (void*)&container);
	if (iret)
	{
	    fprintf(stderr,"Error - pthread_create(ReaderThread) return code: %d\n",iret);
	    log_destroy(&log);
	    exit(EXIT_FAILURE);
	}



	pthread_join(reader_tid, NULL);
	pthread_join(writer_tid, NULL);
	pthread_mutex_destroy(&container.mutex);

	log_destroy(&log);
	return EXIT_SUCCESS;
}

void* ReaderThread(void *arg)
{
  struct test_container *container = (struct test_container *)arg;
  struct log_rotate_file *log = container->log;

  do
  {
    pthread_mutex_lock(&log->file_mx);
    printf("Reader obtained semaphore\n");
    printf("Log size is:    %d\n", (int)ftell(log->logfile));
    printf("Log position:   %d\n", log->file_pos);
    printf("Log is%s overlapped\n\n", (log->file_full ? "" : " not"));
    if (log->file_full == 0)
    {
      print_log_file(log->logfile, 0);
    }
    else
    {
      print_log_file(log->logfile, log->file_pos);
    }
    pthread_mutex_unlock(&log->file_mx);
    printf("Reader released semaphore\n");
    usleep(1000000);
  } while(1);
}

void* WriterThread(void *arg)
{
  struct test_container *container = (struct test_container *)arg;
  struct log_rotate_file *log = container->log;
  FILE *finput = NULL;
  char *line = NULL;
  size_t line_len = 0;
  ssize_t read_size = 0;
  int i;

  finput = fopen(container->input_file_name, "r");
  if (finput == 0)
  {
    printf("Error opening file %s", container->input_file_name);
    return NULL;
  }

  line = malloc(1024);
  if (line == NULL)
    line_len = 0;
  else
    line_len = 1024;
  printf("%s thread started. Line buf %d length allocated\n", __FUNCTION__, (int)line_len);
  printf("Container consist of:\nFilename: %s\n", container->input_file_name);

  do
  {
    for( i=0; i < 2 && read_size >= 0; i++)
    {
      read_size = getline(&line, &line_len, finput);
      if (read_size == -1)
      {
        printf("Error reading line\n");
        continue;
      }
      putLineToLog(line, read_size, log);
    }
    sleep(1);
  } while(read_size >= 0);

  printf("Ending Writer Thread\n");
  free(line);
  fclose(finput);

  return NULL;
}

/* *****************************************************************************
 *
 * ****************************************************************************/
void print_log_file(FILE *file, int starting_pos)
{
  char *line = NULL;
  size_t line_len = 0;
  FILE *fd_out = NULL;

  fd_out = fopen("output.log", "w+");

  line = malloc(1024);
  if (line == NULL)
    line_len = 0;
  else
    line_len = 1024;
  if (starting_pos == 0)
  {
    fseek(file, 0, SEEK_SET);
    while (getline(&line, &line_len, file) > 0)
    {
      fprintf(fd_out, "%s", line);
    }
  }
  else
  {
    fseek(file, starting_pos, SEEK_SET);
    while (getline(&line, &line_len, file) > 0)
    {
      fprintf(fd_out, "%s", line);
    }
    fseek(file, 0, SEEK_SET);
    while (getline(&line, &line_len, file) > 0)
    {
      fprintf(fd_out, "%s", line);
      if (ftell(file) >= starting_pos)
      {
        break;
      }
    }
  }
  fclose(fd_out);
  free(line);
}


/* *****************************************************************************
 *
 * ****************************************************************************/
static int putLineToLog(const char *line, ssize_t len, struct log_rotate_file *log)
{
  if (line == NULL || len < 0 || log == NULL)
  {
    printf("Wrong line contents\n");
    return -1;
  }
  pthread_mutex_lock(&log->file_mx);
  printf("Inserting line to file at pos %d\n", log->file_pos);
  sleep(1);
  fseek(log->logfile, log->file_pos, SEEK_SET);
  fwrite(line, len, 1, log->logfile);
  log->file_pos = ftell(log->logfile);
  printf("Inserted %d bytes\n", (int)len);
  if (log->file_pos > LOG_SIZE_LIMIT)
  {
    printf("Size limit %d reached, rotating file.\n", LOG_SIZE_LIMIT);
    fflush(log->logfile);
    log->file_full = 1;
    log->file_pos = 0;
  }
  pthread_mutex_unlock(&log->file_mx);
  return 0;
}

/* *****************************************************************************
 *
 * ****************************************************************************/
static void log_destroy(struct log_rotate_file *log)
{
  pthread_mutex_lock(&log->file_mx);
  fclose(log->logfile);
  pthread_mutex_unlock(&log->file_mx);
  pthread_mutex_destroy(&log->file_mx);
}

/* *****************************************************************************
 *
 * ****************************************************************************/
static int initLog(struct log_rotate_file *log)
{
  int ret = EXIT_SUCCESS;

  pthread_mutex_lock(&log->file_mx);
  log->logfile = fopen("log.log", "w+");
  if (log->logfile == NULL)
  {
    printf("Error opening file loga.log");
    ret = EXIT_FAILURE;
  }
  else
  {
    printf("Log file prepared.\n");
    ret = EXIT_SUCCESS;
  }
  pthread_mutex_unlock(&log->file_mx);
  return ret;
}
