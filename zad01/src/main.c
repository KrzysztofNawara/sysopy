
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include <pthread.h>

#include "queue.h"

#define MODE_LOCAL 'l'
#define MODE_REMOTE 'r'
#define UNIX_SOCKET_PATH_MAX 108
#define USERNAME_MAX 16
#define MSG_LEN_MAX 128
#define DATAGRAM_SIZE 2*(USERNAME_MAX+1) + MSG_LEN_MAX + 1
#define MIN_PORT 1024
#define MAX_PORT 65535
#define MSG_QUEUES_CAPACITY 64

/* Interface */
#define USR_CMD_EXIT "e\n"
#define USR_CMD_TYPE "t\n"

#define EXIT() exit(1);

/*
 * @ToDo:
 * - synchronous signal handler
 * - rewrite networking thread to poll for output and wait for signal when input
 */

typedef struct {
	char *username;
	char mode;
	struct sockaddr *address;
	size_t address_size;
	int sock_type;
} program_arguments;

program_arguments program_args;
int sd = -1;
volatile short should_exit = 0;

void process_arguments(int argc, char **argv, program_arguments *args) {
	if(argc < 3) {
		printf("Too few arguments\n");
		EXIT();
	}

	if(strlen(argv[1]) > USERNAME_MAX) {
		printf("Username too long; max: %i\n", USERNAME_MAX);
		EXIT();
	}
	args->username = argv[1];

	char mode_tmp = argv[2][0];
	if(mode_tmp != MODE_LOCAL && mode_tmp != MODE_REMOTE) {
		printf("Mode unrecognized\n");
		EXIT();
	}
	args->mode = mode_tmp;

	if(args->mode == MODE_LOCAL) {
		char *unix_socket_path = argv[3];
		int path_len = strlen(unix_socket_path);

		if(path_len > UNIX_SOCKET_PATH_MAX) {
			printf("Socket path too long\n");
			EXIT();
		}

		/*
		 * @todo: free this memory
		 */
		struct sockaddr_un *unix_address = calloc(sizeof(struct sockaddr_un), 1);
		unix_address->sun_family = AF_UNIX;
		strcpy(unix_address->sun_path, unix_socket_path);

		args->address = unix_address;
		args->address_size = sizeof(struct sockaddr_un);
		args->sock_type = AF_UNIX;
	} else {
		if(argc < 4) {
			printf("Too few arguments!\n");
			EXIT();
		}

		struct sockaddr_in *inet_address = calloc(sizeof(struct sockaddr_in), 1);
		inet_address->sin_family = AF_INET;
		int ret = inet_pton(AF_INET, argv[3], &(inet_address->sin_addr));
		if(ret != 1) {
			printf("Wrong IP format\n");
			EXIT();
		}

		long unvalidated_port = strtol(argv[4], NULL, 10);
		if(unvalidated_port < MIN_PORT || unvalidated_port > MAX_PORT) {
			printf("Wrong port\n");
			EXIT();
		}
		inet_address->sin_port = (in_port_t)unvalidated_port;

		args->address = inet_address;
		args->address_size = sizeof(struct sockaddr_in);
		args->sock_type = AF_INET;
	}
}

/* ------------------------------- */

void open_socket(program_arguments *args) {
		sd = socket(args->sock_type, SOCK_DGRAM, 0);
}

void close_socket() {
	close(sd);
}



/* ------------------------------- */

typedef struct {
	char from[USERNAME_MAX+1];
	char to[USERNAME_MAX+1];
	char msg[MSG_LEN_MAX+1];
} message;

/* ------------------------------- */

short input_avaliable() {
	struct pollfd fds[1];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	poll(fds, 1, 0);

	return fds[0].revents & POLLIN;
}

void print_command_prompt() {
	printf("What do you want to do? [t - start typing message|e - exit]: ");
}

void print_content_query() {
	printf("Type message body:\n");
}

void print_recipient_query() {
	printf("Type recipient: ");
}

void print_all_pending_msgs(queue_t *q_out) {

	short at_least_one_printed = 0;
	message *msg = NULL;
	while(msg = queue_dequeue(q_out), msg != NULL) {
		at_least_one_printed = 1;
		printf("[%s] %s", msg->from, msg->msg);
		free(msg);
	}

	if(at_least_one_printed != 0) {
		print_command_prompt();
	}
}

message *pack_message(char *from, char *to, char *content) {
	message *msg = calloc(sizeof(message), 1);
	strcpy(msg->from, from);
	strcpy(msg->to, to);
	strcpy(msg->msg, content);

	return msg;
}

/* ------------------------------- */

typedef struct {
	program_arguments *program_args;
	queue_t *q_in;
	queue_t *q_out;
} thread_data;

void *thread_io(void *_data) {
	thread_data *data = _data;

	char *buffer_for_user_input = NULL;
	size_t bui_length = 0;
	#define GET_LINE() getline(&buffer_for_user_input, &bui_length, stdin)

	char recipient[USERNAME_MAX+1] = {0};

	print_command_prompt();
	while(should_exit != 1) {
		if(input_avaliable() != 0) {
			GET_LINE();

			if(strcmp(buffer_for_user_input, USR_CMD_EXIT)) {
				print_all_pending_msgs(data->q_out);
				should_exit = 1;
			} else if(strcmp(buffer_for_user_input, USR_CMD_TYPE)) {
				print_recipient_query();
				GET_LINE();
				strcpy(recipient, buffer_for_user_input);

				print_content_query();
				GET_LINE();

				message *packed_msg = pack_message(data->program_args->username, recipient, buffer_for_user_input);
				queue_enqueue(data->q_in, packed_msg);
			} else {
				print_command_prompt();
			}
		} else {
			print_all_pending_msgs(data->q_out);
		}
	}

	#undef GET_LINE
}

void thread_networking(thread_data *data) {
	open_socket(&program_args);

	struct pollfd fds[1];
	fds[0].fd = sd;
	fds[0].events = POLLIN | POLLOUT;
	fds[0].revents = 0;

	int ret = 0;
	while(should_exit != 1) {
		ret = poll(fds, 1, -1);
		if(ret > 0) {
			if((fds[0].revents & POLLIN) != 0) {
				message *msg = queue_dequeue(data->q_in);
				if(msg != NULL) {
					sendto(sd, msg, sizeof(message), 0, data->program_args->address, data->program_args->address_size);
					free(msg);
				}
			}

			if((fds[0].revents & POLLOUT) != 0) {
				message *incoming_msg = malloc(sizeof(message));
				recvfrom(sd, incoming_msg, sizeof(message), 0, NULL, NULL);
				queue_enqueue(data->q_out, incoming_msg);
			}
		}
	}
}

/* ------------------------------- */


int main(int argc, char **argv) {
	process_arguments(argc, argv, &program_args);

	// create and initialize bounded queues
	void* in_buffer[MSG_QUEUES_CAPACITY];
	void* out_buffer[MSG_QUEUES_CAPACITY];
	queue_t q_in = QUEUE_INITIALIZER(in_buffer);
	queue_t q_out = QUEUE_INITIALIZER(out_buffer);

	thread_data data;
	data.program_args = &program_args;
	data.q_in = &q_in;
	data.q_out = &q_out;

	pthread_t io_thread;
	pthread_create(&io_thread, NULL, &thread_io, &data);

	thread_networking(&data);

	void *dummy = NULL;
	pthread_join(io_thread, &dummy);

	close_socket();
	return 0;
}