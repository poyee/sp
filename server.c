#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

#define read_lock(fd, offset, whence, len) lock_reg((fd), F_SETLK, F_RDLCK, (offset), (whence), (len))
#define write_lock(fd, offset, whence, len) lock_reg((fd), F_SETLK, F_WRLCK, (offset), (whence), (len))
#define un_lock(fd, offset, whence, len) lock_reg((fd), F_SETLK, F_UNLCK, (offset), (whence), (len))

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    char* filename;  // filename set in header, end with '\0'.
    int header_done;  // used by handle_read to know if the header is read or not.
} request;

typedef struct {
    int fd;
    char* filename;
}fileStruct;


server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_header = "ACCEPT\n";
const char* reject_header = "REJECT\n";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len);

int has_filename(char* filename, fileStruct* file_table, int biggest_fd);

void set_filename(char* dst, char* src);

int main(int argc, char** argv) {
    int i, ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client

    char buf[512];
    int buf_len;

    fd_set request_set, listen_set, read_set;
    FD_ZERO(&request_set);

    struct timeval timeout;



    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    int biggest_fd; //fd number;

    biggest_fd = svr.listen_fd;

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();


    fileStruct file_table[maxfd]; //map conn_fd to fild_fd


    for(int i = 0; i <= biggest_fd; i++) {
        file_table[i].fd = -1;
    }

    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd; //server initilized listen socket
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while (1) {
        // TODO: Add IO multiplexing
        // Check new connection
        FD_ZERO(&listen_set);
        FD_SET(svr.listen_fd, &listen_set);

        clilen = sizeof(cliaddr);

        ret = select(svr.listen_fd+1, &listen_set, NULL, NULL, &timeout);
        /*if(ret == -1)
            fprintf(stderr, "select error:%d\n", errono);*/
        if(ret > 0) {
            if(FD_ISSET(svr.listen_fd, &listen_set)) {
                conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                if (conn_fd < 0) {
                    if (errno == EINTR || errno == EAGAIN) continue;  // try again
                    if (errno == ENFILE) {
                        (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                        continue;
                    }
                    ERR_EXIT("accept")
                }
                requestP[conn_fd].conn_fd = conn_fd;
                strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                
                FD_SET(conn_fd, &request_set);

                file_table[conn_fd].fd = -1;
                file_table[conn_fd].filename = NULL;
                
                if(conn_fd > biggest_fd)
                    biggest_fd = conn_fd;
            }
        }

        

        FD_ZERO(&read_set);
        
        memcpy(&read_set, &request_set, sizeof(request_set));

        ret = select(biggest_fd+1, &read_set, NULL, NULL, &timeout);
        if(ret == -1)
            fprintf(stderr, "select error:%d\n", errno);
        if(ret > 0) {
            for(int i = 0; i <= biggest_fd; i++) {
               		//fprintf(stderr, "for loop\n");
      

#ifdef READ_SERVER

                if(FD_ISSET(i, &read_set)){

                	

                    ret = handle_read(&requestP[i]);
                    if (ret < 0) {
                        fprintf(stderr, "bad request from %s\n", requestP[i].host);
                        //continue;
                    }

                    else if(ret > 0)  {
                        // open the file here.
                        
                        // TODO: Add lock
                        // TODO: check if the request should be rejected.
                       	fprintf(stderr, "ret > 0\n");
                        int fd = open(requestP[i].filename, O_RDONLY, 0);
                        if(read_lock(fd, 0, SEEK_SET, 0 ) == -1) {
                            if (errno == EACCES || errno == EAGAIN) {
                                fprintf(stderr, "[%s] is locked, request rejected.\n", requestP[i].filename);
                                write(requestP[i].conn_fd, reject_header, sizeof(accept_header));
                        	}
                        }

                        else {
                            while (1) {
                                fprintf(stderr, "Opening file [%s]\n", requestP[i].filename);
                                write(requestP[i].conn_fd, accept_header, sizeof(accept_header));
                                ret = read(fd, buf, sizeof(buf));
                                if (ret < 0) {
                                    fprintf(stderr, "Error when reading file %s\n", requestP[i].filename);
                                    break;
                                } 
                                else if (ret == 0) break;
                                write(requestP[i].conn_fd, buf, ret);
                            }
                            fprintf(stderr, "Done reading file [%s]\n", requestP[i].filename);
                            

                        }
                        close(fd);
                        
                    }
                    close(requestP[i].conn_fd);
                    FD_CLR(requestP[i].conn_fd, &request_set);
                    free_request(&requestP[i]);
                }
                
            
#endif


#ifndef READ_SERVER

                if(FD_ISSET(i, &read_set)){
 				    ret = handle_read(&requestP[i]);
                    if (ret < 0) {
                        fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                        continue;
                    }
                    	// TODO: Add lock
                    	// TODO: check if the request should be rejected.

                    conn_fd = requestP[i].conn_fd;
                    

                        
					if (ret == 0) {
						if(file_table[conn_fd].fd != -1) {
								close(file_table[conn_fd].fd);
								file_table[conn_fd].fd = -1;
                                free(file_table[conn_fd].filename);
								fprintf(stderr, "Done writing file [%s]\n", requestP[conn_fd].filename);
						}
						close(requestP[i].conn_fd);
                        FD_CLR(requestP[i].conn_fd, &request_set);
        				free_request(&requestP[i]);
					}

					else{
                   		if (file_table[conn_fd].fd == -1) {
                            // open the file here.
                            file_table[conn_fd].fd = open(requestP[i].filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
                            if(write_lock(file_table[conn_fd].fd, 0, SEEK_SET, 0 ) == -1 || has_filename(requestP[i].filename, file_table, biggest_fd)){
                                if (errno == EACCES || errno == EAGAIN) {
                                    fprintf(stderr, "[%s] is locked, request rejected.\n", requestP[i].filename);
                                    write(requestP[i].conn_fd, reject_header, sizeof(reject_header));
                                    //close write file
                                    close(file_table[conn_fd].fd);
									file_table[conn_fd].fd = -1;
                                    free(file_table[conn_fd].filename);
                                    //close conn file
									close(requestP[i].conn_fd);
                        			FD_CLR(requestP[i].conn_fd, &request_set);
        							free_request(&requestP[i]);	
                                }
                            }
                        
                        	else {
                                set_filename(file_table[conn_fd].filename, requestP[i].filename);
                            	fprintf(stderr, "Opening file [%s]\n", requestP[conn_fd].filename);
                            	write(requestP[i].conn_fd, accept_header, sizeof(accept_header));
                        	}
                        }
                        write(file_table[conn_fd].fd, requestP[i].buf, requestP[i].buf_len);
                    }
                }
           
#endif

            }

    	}

    	//fprintf(stderr, "while loop\n");
        
    }

    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->filename = NULL;
    reqP->header_done = 0;
}

static void free_request(request* reqP) {
    if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (reqP->header_done == 0) {
        char* p1 = strstr(buf, "\015\012");
        int newline_len = 2;
        // be careful that in Windows, line ends with \015\012
        if (p1 == NULL) {
            p1 = strstr(buf, "\012");
            newline_len = 1;
            if (p1 == NULL) {
                // This would not happen in testing, but you can fix this if you want.
                ERR_EXIT("header not complete in first read...");
            }
        }
        size_t len = p1 - buf + 1;
        reqP->filename = (char*)e_malloc(len);
        memmove(reqP->filename, buf, len);
        reqP->filename[len - 1] = '\0';
        p1 += newline_len;
        reqP->buf_len = r - (p1 - buf);
        memmove(reqP->buf, p1, reqP->buf_len);
        reqP->header_done = 1;
    } else {
        reqP->buf_len = r;
        memmove(reqP->buf, buf, r);
    }
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
    struct flock lock;
    lock.l_type = type; /* F_RDLCK, F_WRLCK, F_UNLCK */
    lock.l_start = offset; /* byte offset, relative to l_whence */
    lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
    lock.l_len = len; /* #bytes (0 means to EOF) */
    return(fcntl(fd, cmd, &lock));
}

int has_filename(char* filename, fileStruct* file_table, int biggest_fd)
{
    for(int i = 0; i <= biggest_fd; i++) {
        if(file_table[i].fd > 0){
            if(strcmp(filename, file_table[i].filename))
                return 1;
        }
    }

    return 0;
}

void set_filename(char* dst, char* src)
{
    dst = (char*)malloc(sizeof(strlen(src)+1));
    strcpy(dst, src);
}