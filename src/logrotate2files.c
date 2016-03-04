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
#include <time.h>
#include<errno.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<string.h>

#define LOG_SIZE_LIMIT    (1024 * 5)
#define LOG_FILES_CNT_MAX 8
#define LOG_FILENAME_LEN  16

struct log_rotate_file {
  FILE *logfile[LOG_FILES_CNT_MAX];                 // pointers to files streams
  int file_pos[LOG_FILES_CNT_MAX];                  // position to write
  int file_full[LOG_FILES_CNT_MAX];                 // file written to full and already rotated
  char logname[LOG_FILES_CNT_MAX][LOG_FILENAME_LEN];// strings with log file name
  int current;
  int files_cnt;
  pthread_mutex_t mutex_log;
};

struct test_container {
  struct log_rotate_file *log;
  char *input_file_name;
  pthread_mutex_t mutex;
};

static int initLog(struct log_rotate_file *log, int files_cnt);
static int putLineToLog(const char *line, ssize_t len, struct log_rotate_file *log);
static void log_destroy(struct log_rotate_file *log);
static void send_log_as_http11chunks(int sockfd, struct log_rotate_file *log);
void* WriterThread(void *arg);
void* ReaderThread(void *arg);
void print_log_to_file(const char *name, struct log_rotate_file *log);

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
  for(i = 0; i < LOG_SIZE_LIMIT; i++)
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
	struct log_rotate_file log;
	int iret;

	if (argc != 2)
	{
	  printf("Wrong parameter\nGive only input file name as parameter.\n");
	  return EXIT_FAILURE;
	}

	if (initLog(&log, 8) != EXIT_SUCCESS)
	{
	  printf("Error initializing LOG file\n");
	  return EXIT_FAILURE;
	}
//	  generate_input_file(argv[1]);
//	  return 0;
	container.log = &log;
	container.input_file_name = argv[1];

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

  struct sockaddr_in sockserv, sockclient;
  int socketfd, clientfd;
  socklen_t clientsocklen;
  int finalcnt = 0;

  char buff[BUFSIZ];

  socketfd = socket(AF_INET,SOCK_STREAM,0);
  printf("Socket Creation: %s\n", strerror(errno));

  bzero(&sockserv,sizeof(sockserv));

  sockserv.sin_family = AF_INET;
  sockserv.sin_addr.s_addr = INADDR_ANY;
  sockserv.sin_port = htons(8080);

  bind(socketfd,(struct sockaddr *)&sockserv, sizeof(sockserv));
  printf("Socket Bind: %s\n", strerror(errno));

  listen(socketfd,10);
  printf("Socket Listen: %s\n",strerror(errno));

  sleep(2);

  do
  {
    clientfd = accept(socketfd, (struct sockaddr*)&sockclient, &clientsocklen);
    printf("request accepted\n");
    buff[read(clientfd, buff, BUFSIZ)] = '\0';
    printf("Request recieved as \n%s\n",buff);

    snprintf(buff, BUFSIZ, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nDate: Mon, 28 Feb 2011 10:38:19 GMT\r\nTransfer-Encoding: chunked\r\nServer: Myserver\r\n\r\n%x\r\n%s\r\n",(unsigned int)strlen("<html><body><p> HI I am anirudh tomer </p><br> <pre>"),"<html><body><p> HI I am anirudh tomer </p><br> <pre>");
    printf("\n\nREPLY TO THE CLIENT\n\n%s\n\n",buff);
    finalcnt = send(clientfd, buff, strlen(buff), 0);
    printf("Sent %d bytes to the client : %s\n", finalcnt, strerror(errno));

    pthread_mutex_lock(&log->mutex_log);
    printf("Log size is:     %d\n", (int)ftell(log->logfile[log->current]));
    printf("Log position:    %d\n", log->file_pos[log->current]);
    printf("Log current file %d\n", log->current);
    printf("Reader obtained semaphore\n");
    //    print_log_to_file("output.log", log);
    if (log->file_pos[log->current] > 0)
    {
      send_log_as_http11chunks(clientfd, log);
    }
    else
    {
      finalcnt = snprintf(buff, BUFSIZ, "%x\r\nLog is empty!!!\nSorry.0\r\n\r\n", (unsigned int)strlen("Log is empty!!!\nSorry."));
      finalcnt = send(clientfd, buff, finalcnt, 0);
    }
    pthread_mutex_unlock(&log->mutex_log);

    finalcnt = snprintf(buff, BUFSIZ, "%x\r\n</pre> </body></html>0\r\n\r\n", (unsigned int)strlen("</pre> </body></html>"));
    finalcnt = send(clientfd, buff, finalcnt, 0);

    printf("\n\nREPLY TO THE CLIENT\n\n%s\n\n",buff);
    usleep(10000);
    close(clientfd);

  } while(1);


  close(socketfd);
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
  printf("Input file name: %s\n", container->input_file_name);
  sleep(10);
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
      usleep(100000);
    }
  } while(read_size >= 0);

  printf("Ending Writer Thread\n");
  free(line);
  fclose(finput);

  return NULL;
}

/* *****************************************************************************
 *
 * ****************************************************************************/
static void send_log_as_http11chunks(int sockfd, struct log_rotate_file *log)
{
  char *line = NULL;
  size_t line_len = 0;
  int cur = log->current;           // index of currently written log file
  int itr = cur;                    // index for reading files
  ssize_t read_len;
  unsigned int finalcnt;
  char header[256];

  line = malloc(1024);
  if (line == NULL)
    line_len = 0;
  else
    line_len = 1024;

  do
  {
    itr = ((itr + 1) % log->files_cnt);
    if (log->file_full[itr] || log->current == itr)   /* skip empty files but print current*/
    {
      printf("Printing file %d\n", itr);
      fseek(log->logfile[itr], 0, SEEK_SET);
      while (1)
      {
        read_len = getline(&line, &line_len, log->logfile[itr]);
        if (read_len > 0)
        {
          snprintf(header, sizeof(header), "%x\r\n", (unsigned int)read_len);
          finalcnt = send(sockfd, header, strlen(header), 0);
          finalcnt += send(sockfd, line, (size_t)read_len, 0);
          finalcnt += send(sockfd, "\r\n", 2, 0);
          printf("Response: %s %s\n", header, line);
          printf("Sent %d bytes to the client : %s\n", finalcnt, strerror(errno));
          continue;
        }
        printf("Printing file finished\n");
        break;
      }
    }
    else
    {
      printf("Skipping %d file\n", itr);
    }
  }while (itr != cur);

  free(line);
}

/* *****************************************************************************
 * print_log_to_file("output.log", log);
 * ****************************************************************************/
void print_log_to_file(const char *name, struct log_rotate_file *log)
{
  char *line = NULL;
  size_t line_len = 0;
  FILE *fd_out = NULL;
  int cur = log->current;           // index of currently written log file
  int itr = cur;                    // index for reading files

  fd_out = fopen(name, "w+");

  line = malloc(1024);
  if (line == NULL)
    line_len = 0;
  else
    line_len = 1024;

  printf("Will print contents of %d files\n", log->files_cnt);

  do
  {
    itr = ((itr + 1) % log->files_cnt);
    if (log->file_full[itr] || log->current == itr)   /* skip empty files but print current*/
    {
      printf("Printing file %d\n", itr);
      fseek(log->logfile[itr], 0, SEEK_SET);
      while (getline(&line, &line_len, log->logfile[itr]) > 0)
      {
        fprintf(fd_out, "%s", line);
      }
    }
    else
    {
      printf("Skipping %d file\n", itr);
    }
  }while (itr != cur);

  fclose(fd_out);
  free(line);
}


/* *****************************************************************************
 *
 * ****************************************************************************/
static int putLineToLog(const char *line, ssize_t len, struct log_rotate_file *log)
{
  int cur;
  if (line == NULL || len < 0 || log == NULL)
  {
    printf("Wrong line contents\n");
    return -1;
  }
  pthread_mutex_lock(&log->mutex_log);
  cur = log->current;
//  printf("Inserting line to file %d at pos %d\n", cur, log->file_pos[cur]);
  usleep(100000);
  fseek(log->logfile[cur], log->file_pos[cur], SEEK_SET);
  fwrite(line, len, 1, log->logfile[cur]);
  log->file_pos[cur] = ftell(log->logfile[cur]);
//  printf("Inserted %d bytes\n", (int)len);

  if (log->file_pos[cur] > LOG_SIZE_LIMIT)
  {
//    printf("Size limit %d reached in file %d, rotating file.\n", LOG_SIZE_LIMIT, cur);
    fflush(log->logfile[cur]);
    log->file_full[cur] = 1;
    cur = ((cur + 1) % log->files_cnt);
    log->file_pos[cur] = 0;
    log->current = cur;
    if (ftruncate(fileno(log->logfile[cur]), 0) == -1){
      perror("Could not truncate");
    }
  }
  pthread_mutex_unlock(&log->mutex_log);
  return 0;
}

/* *****************************************************************************
 *
 * ****************************************************************************/
static void log_destroy(struct log_rotate_file *log)
{
  int i;
  pthread_mutex_lock(&log->mutex_log);
  for (i = 0; i < log->files_cnt; i++)
    fclose(log->logfile[i]);
  pthread_mutex_unlock(&log->mutex_log);
  pthread_mutex_destroy(&log->mutex_log);
}

/* *****************************************************************************
 *
 * ****************************************************************************/
static int initLog(struct log_rotate_file *log, int files_cnt)
{
  int ret = EXIT_SUCCESS;
  int i;

  if (!log || files_cnt > LOG_FILES_CNT_MAX)
  {
    printf("%s - Error", __FUNCTION__);
    return -1;
  }

  for(i = 0; i < files_cnt; i++)
  {
    snprintf(log->logname[i], LOG_FILENAME_LEN, "logwebmon%d.log", i);
    log->logfile[i] = fopen(log->logname[i], "w+");
    if (log->logfile[i] == NULL)
    {
      printf("Error opening file %s\n", log->logname[i]);
      ret = EXIT_FAILURE;
      do {
        fclose(log->logfile[--i]);
      }
      while(i);
      printf("Error opening file\n");
      break;
    }
    log->file_full[i] = 0;
    log->file_pos[i] = 0;
  }
  log->current = 0;
  log->files_cnt = files_cnt;
  pthread_mutex_init(&log->mutex_log, NULL);
  return ret;
}
