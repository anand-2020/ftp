/* 
g++ client.cpp -o client -lpthread
./client <host_name> <port_no> 
*/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <sys/sendfile.h> 
#include <fcntl.h> 
#include <sys/stat.h> 
#include <dirent.h>
#include <termios.h>
using namespace std;

#define BUFF_SIZE 512
char ip[50];
int PORT;

pthread_mutex_t req_mutex;
char last_req[BUFF_SIZE];

struct TransferParam{
    string operation;
    string filename;
    int data_channel_fd;
    bool binary_mode;
};

void clearStdinBuffer(){
	int stdin_copy = dup(STDIN_FILENO);
	tcdrain(stdin_copy);
	tcflush(stdin_copy, TCIFLUSH);
	close(stdin_copy);
}

TransferParam getTransferParam(char cmd[], int fd){
    TransferParam req;
    req.data_channel_fd = fd;
    req.operation = string(cmd, cmd+3);
    string filename;

    const char sp = ' ';
    char *ptr;
    ptr = strchr(cmd+4, sp);

    if(ptr == NULL){
        filename = string(cmd + 4);
        filename.pop_back();
        req.binary_mode = false;
    }
    else{
        int dist = ptr - cmd;
        filename = string(cmd + 4, cmd+dist);

        if(strncmp(ptr, " -b", 3) == 0){
            req.binary_mode = true;
        }
        else req.binary_mode = false;
    }

    req.filename = filename ;
    return req;
}

int fileExists(string filename){
    DIR *mydir;
    struct dirent *mydirent ;
    int exists = 0;

    string path = "./" ;

    mydir = opendir(path.c_str());
    while((mydirent = readdir(mydir)) != NULL ){
    	
        if(strcmp(mydirent->d_name, filename.c_str()) == 0) {
        	exists = 1; break;
        }
    }
    
    return exists;
}

int socketSetup(int port_no){
	int sock_fd ;
	struct sockaddr_in serv_addr;
	struct hostent *server;

	server = gethostbyname(ip);
	if(server == NULL){
		perror("ERROR: host does not exists\n");
		return -1;
	}
	
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_fd < 0){
		perror("ERROR: on opening socket\n");
		return -1;
	}
	

	bzero((char *) &serv_addr, sizeof(serv_addr));
	
	serv_addr.sin_family = AF_INET;
	bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(port_no);
	
	if(connect(sock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		perror("ERROR: on connection\n");
		return -1;
	}

	return sock_fd ;
}

void *sendFile(void *arg){
    TransferParam* req = new TransferParam();
    req = (TransferParam *)arg;
	
	char data[BUFF_SIZE];
	
    FILE *fp;
    if(req->binary_mode){
        fp = fopen((req->filename).c_str(), "rb");
    }
    else{
        fp = fopen((req->filename).c_str(), "r");
    }
    
    if(fp == NULL){
       perror("Error opening file\n");
    }

    struct stat st;
    stat((req->filename).c_str(), &st);
    int file_size = st.st_size;
    string res = "FILE SIZE " + to_string(file_size) ;
    strcpy(data, res.c_str());
    write(req->data_channel_fd, data, BUFF_SIZE);

	bzero(data, BUFF_SIZE); 
	
	int cur_data_size ;
    while(!feof(fp)){
        cur_data_size = fread(data, sizeof(char), BUFF_SIZE, fp);
        
        write(req->data_channel_fd, data, cur_data_size);
    
	    bzero(data, BUFF_SIZE); 
    }
    
    fclose(fp);
    close(req->data_channel_fd);
    //printf("File sent successfully to server\n");

    delete(req);
    
    return 0;
}

void *recvFile(void *arg){
    TransferParam* req = new TransferParam();
    req = (TransferParam *)arg;
    
    char data[BUFF_SIZE];

    bzero(data, BUFF_SIZE);   
	read(req->data_channel_fd, data, BUFF_SIZE);
	int file_size = atoi(data + 10);

    FILE *fp;
    if(req->binary_mode){
        fp = fopen((req->filename).c_str(), "wb");
    }
    else{
        fp = fopen((req->filename).c_str(), "w");
    }
   
    if(fp == NULL){
       perror("Error opening file\n");
    }
  
    int tot_size_recvd = 0;
    int cur_data_size ;

    while(tot_size_recvd < file_size){
        bzero(data, BUFF_SIZE); 
        cur_data_size = read(req->data_channel_fd, data, BUFF_SIZE);

        fwrite(data, sizeof(char), cur_data_size, fp);
        
        tot_size_recvd += cur_data_size ;
    }
    
    fclose(fp);
    close(req->data_channel_fd);
   // printf("File recieved from server succesfully\n");
    
    string tr_mode = ((req->binary_mode) == true) ? "binary" : "ascii";
   	printf("\nFILE TRANSFER SUCCESSFUL:\n");
   	printf("Request: GET\n");
    printf("Mode: %s\n", (tr_mode).c_str());
   	printf("Filename: %s\n", (req->filename).c_str());
   	printf("File_size: %d bytes\n\n", file_size);

    delete(req);
    
    return 0;
}

void *recieveMsg(void *arg){
    int control_channel_fd = *(int*)arg;
    char buffer[BUFF_SIZE];
   
    while(1){
    	clearStdinBuffer();
    	bzero(buffer, BUFF_SIZE);
		read(control_channel_fd, buffer, BUFF_SIZE);
	
        if(strcmp(buffer, "CREATE DATA CHANNEL\n") == 0){
            int data_channel_fd = socketSetup(PORT-1);
            if(data_channel_fd == -1){
                write(control_channel_fd, "ERROR: CREATING DATA CHANNEL\n", BUFF_SIZE);

                pthread_mutex_unlock(&req_mutex);
                continue;
            }

            TransferParam* req = new TransferParam();
            *req = getTransferParam(last_req, data_channel_fd);

            pthread_t th;
            int th_status ;

            if(req->operation == "GET"){
                th_status = pthread_create(&th, NULL, &recvFile, req) ; 
            }
            else{
                if(fileExists(req->filename) == 0){
                    cout<<"file "<<(req->filename)<<" not found in current working directory"<<endl;
			
			write(control_channel_fd, "ERROR: CREATING DATA CHANNEL\n", BUFF_SIZE);
                    pthread_mutex_unlock(&req_mutex);
                    continue;
                }
                th_status = pthread_create(&th, NULL, &sendFile, req);
            }

            if(th_status != 0){
                close(req->data_channel_fd);
			    perror("ERROR: Thread creation for transferring file failed\n");
                write(control_channel_fd, "ERROR: CREATING DATA CHANNEL\n", BUFF_SIZE);

                pthread_mutex_unlock(&req_mutex);
                continue;
            }

            write(control_channel_fd, "DATA CHANNEL CREATED\n", BUFF_SIZE);
           
            pthread_mutex_unlock(&req_mutex);
            continue;
        }

	    printf("SERVER: %s", buffer);

        // for GET req;
        if(strcmp(buffer, "FILE DOES NOT EXISTS\n") == 0){
            pthread_mutex_unlock(&req_mutex);
        }			
    }
    
    return 0;
}

void *sendMsg(void *arg){
    int control_channel_fd = *(int*)arg;
    char buffer[BUFF_SIZE];
     
     
    while(1){
        //printf("Enter Command:\n");
    	clearStdinBuffer();
    	bzero(buffer, BUFF_SIZE);
    	fgets(buffer, BUFF_SIZE, stdin);
    	if(buffer[0] == '\n') continue;


		if(strncmp(buffer, "GET ", 4) == 0 || strncmp(buffer, "PUT ", 4) == 0){
		    pthread_mutex_lock(&req_mutex);
            bzero(last_req, BUFF_SIZE);
            strcpy(last_req, buffer);
		}
		
	    write(control_channel_fd, buffer, BUFF_SIZE);
		
	 	if(strcmp(buffer, "close\n") == 0) break;
    }

    close(control_channel_fd);
    return 0;
}

int authenticate(int control_channel_fd){  
    char buffer[BUFF_SIZE];
    string res, username, password;
    int ch = 0;
    while(1){
        printf("ENTER 1 for LOGIN 2 for SIGNUP\nYOUR CHOICE: ");
        scanf("%d", &ch);

        if(ch==1 || ch==2) break;
        printf("INVALID CHOICE!\n");
    }

    bzero(buffer, BUFF_SIZE);
    if(ch==1) {
        strcpy(buffer, "LOGIN\n");
        printf("\nLOGIN TO FTP SERVER:\n\n");
    }
    else {
        strcpy(buffer , "SIGNUP\n");
        printf("\nSIGNUP TO FTP SERVER:\n\n");
    }

    write(control_channel_fd, buffer, BUFF_SIZE);

    while(1){
        printf("ENTER USERNAME: ");
        cin>>username;
        
        if(username == "close"){
        	write(control_channel_fd, "close\n", BUFF_SIZE);
        	return 0;
        }

        string res = "USER " + username + "\n" ;
        bzero(buffer, BUFF_SIZE);
        strcpy(buffer, res.c_str());
        write(control_channel_fd, buffer, BUFF_SIZE);

        bzero(buffer, BUFF_SIZE);
        read(control_channel_fd, buffer, BUFF_SIZE);
        printf("SERVER: %s\n", buffer);

        if(strcmp(buffer, "USER OK\n") == 0) break;
    }

    while(1){
        printf("ENTER PASSWORD: ");
        cin>>password;
        
        if(password == "close"){
        	write(control_channel_fd, "close\n", BUFF_SIZE);
        	return 0;
        }

        string res = "PASS " + password + "\n" ;
        bzero(buffer, BUFF_SIZE);
        strcpy(buffer, res.c_str());
        write(control_channel_fd, buffer, BUFF_SIZE);

        bzero(buffer, BUFF_SIZE);
        read(control_channel_fd, buffer, BUFF_SIZE);
        printf("SERVER: %s\n", buffer);

        if(strcmp(buffer, "SIGNUP FAILED\n") == 0){
            close(control_channel_fd);
            exit(1);
        }
        else if(strcmp(buffer, "LOGIN SUCCESSFUL\n") == 0 || strcmp(buffer, "SIGNUP SUCCESSFUL\n") == 0){
            break;
        }
        else;
    }
    
    return 1;
    
}


int main(int argc , char *argv[]){
	if(argc < 3){
		printf("Enter %s <host_name> <port_no>\n", argv[0]);
		exit(1);
	}

	strcpy(ip, argv[1]);
	PORT = atoi(argv[2]);
	printf("%s %d\n\n", ip, PORT);
	
	int control_channel_fd = socketSetup(PORT);
    if(control_channel_fd == -1) exit(1);
	
    printf("Connected to server.\n\n" );
 
    pthread_mutex_init(&req_mutex, NULL); 
    
    int is_verified  =  authenticate(control_channel_fd);
    if(is_verified == 0) return 0;
        
    pthread_t th_read, th_write;
	 
    if( pthread_create(&th_read, NULL, &recieveMsg, &control_channel_fd) != 0 ){
		perror("ERROR: Thread creation for recieving messages failed\n");
		exit(1);
	}

    if( pthread_create(&th_write, NULL, &sendMsg, &control_channel_fd) != 0 ){
		perror("ERROR: Thread creation for sending messages failed\n");
		exit(1);
	}
        
    if( pthread_join(th_write, NULL) != 0 ){
		perror("ERROR: Thread joining of sending messages failed\n");
		exit(1);
	}

	/* 
	thread for sending messages has been joined. It means client wants to end connection.
	So forcefully cancel the thread for recieving messages.
	*/
    if( pthread_cancel(th_read) != 0 ){
		perror("ERROR: Thread cancellation of recieving messages failed\n");
		exit(1);
	}
	printf("Session Ended.\n" );
        
	close(control_channel_fd);
	
	return 0;
}

