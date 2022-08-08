/* 
g++ server.cpp -o server -lpthread 
./server <port_no>
*/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <map>
#include <pthread.h>
#include <sys/stat.h> 
#include <dirent.h>
using namespace std;


#define MAX_CONNECTION 10 
#define BUFF_SIZE 512
#define ROOT "FTP_SERVER/" 

int PORT;

struct Connection{
    int recent_data_channel_fd;
    int control_channel_fd; 
    string username;
    string ip_addr ;
    bool is_auth;
};

struct TransferParam{
    string filename;
    int control_channel_fd;
    int data_channel_fd;
    string username;
    bool binary_mode;
};

Connection client[MAX_CONNECTION];

int avl_indices[MAX_CONNECTION];
map<string,string> user_pass; /* username -> password */
map<string,int> ip_idx;     /* ip -> idx */
pthread_mutex_t global_mutex;

int socketSetup(int port_no){
	int sock_fd ;
	struct sockaddr_in serv_addr ;
	
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_fd < 0){
		perror("ERROR: on opening socket\n");
		exit(1);
	}
	
	bzero((char *) &serv_addr, sizeof(serv_addr));
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_no);
	
	if(bind(sock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		perror("ERROR: on binding\n");
		exit(1);
	}
	
	listen(sock_fd, 5);

	return sock_fd ;
}

string getIP(struct sockaddr_in cli_addr){
    string ip = to_string(cli_addr.sin_addr.s_addr);
    return ip;
}

int fileExists(string username, string filepath){
    DIR *mydir;
    struct dirent *mydirent ;
    int exists = 0;
 
    int pos = filepath.find(username);
    string filename = filepath.substr(pos + username.length()+1);
    
    string path = "./" + string(ROOT) + username;

    mydir = opendir(path.c_str());
    
    while((mydirent = readdir(mydir)) != NULL ){
    
        if(strcmp(mydirent->d_name, filename.c_str()) == 0) {exists = 1; break;}
    }
    
    closedir(mydir);
    return exists;
}

string listFiles(string username){
	string res = "Uploaded files are:\n";
	DIR *mydir;
    struct dirent *mydirent ;
    
    string path = "./" + string(ROOT) + username;

    mydir = opendir(path.c_str());
    
    while((mydirent = readdir(mydir)) != NULL ){
    	if((mydirent->d_name)[0] == '.') continue;
    	res = res + string(mydirent->d_name) + "\n" ;
    }
    
    res = res + "\n" ;
    closedir(mydir);

    return res;
	
}

void *acceptDataChannels(void *arg){
    int sock_fd = *(int *)arg;
    struct sockaddr_in cli_addr;
	socklen_t cli_addr_len = sizeof(cli_addr);


    while(1){
        int new_sock_fd = accept(sock_fd, (struct sockaddr *) &cli_addr, (socklen_t *) &cli_addr_len);
        if(new_sock_fd < 0){
		    perror("ERROR: on accepting data channel connection\n");
        }
        
        string ip = getIP(cli_addr);
        if(ip_idx.find(ip) == ip_idx.end()) continue;

        int idx = ip_idx[ip];
        client[idx].recent_data_channel_fd = new_sock_fd ;
    }
}

TransferParam getTransferParam(char cmd[], int idx){
    TransferParam req;
    req.username = client[idx].username;
    req.data_channel_fd = client[idx].recent_data_channel_fd;
    req.control_channel_fd = client[idx].control_channel_fd ;

    string filename = string(ROOT) + req.username + "/" ;

    const char sp = ' ';
    char *ptr;
    ptr = strchr(cmd+4, sp);

    if(ptr == NULL){
        filename += string(cmd + 4);
        filename.pop_back();
        req.binary_mode = false;
    }
    else{
        int dist = ptr - cmd;
        filename += string(cmd + 4, cmd+dist);

        if(strncmp(ptr, " -b", 3) == 0){
            req.binary_mode = true;
        }
        else req.binary_mode = false;
    }

    req.filename = filename ;
    return req;
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
   // cout<<"mode: " << st.st_mode<<endl;
    string res = "FILE SIZE " + to_string(file_size) ;
    strcpy(data, res.c_str());
    write(req->data_channel_fd, data, BUFF_SIZE);
   
    bzero(data, BUFF_SIZE); 
    int cur_data_size;

    while(!feof(fp)){
        cur_data_size = fread(data, sizeof(char), BUFF_SIZE, fp);
        write(req->data_channel_fd, data, cur_data_size);
        
	    bzero(data, BUFF_SIZE); 
    }

    fclose(fp);
    close(req->data_channel_fd);
    //write(req->control_channel_fd, "FILE downloaded successfully\n", BUFF_SIZE);

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
        
        tot_size_recvd +=  cur_data_size;
        
    }
    
    
    fclose(fp);
    close(req->data_channel_fd);
    
    int pos = (req->filename).find(req->username);
    string n_file = (req->filename).substr(pos + (req->username).length()+1);
    string tr_mode = ((req->binary_mode) == true) ? "binary" : "ascii";

    string res = "FILE TRANSFER SUCCESSFUL:\nRequest: PUT\nMode: " + tr_mode + "\nFilename: " + n_file + "\nFile_size: " + to_string(file_size) + " bytes\n" + "\n" ;
    
    bzero(data, BUFF_SIZE); 
    strcpy(data, res.c_str());
    write(req->control_channel_fd, data, BUFF_SIZE);

    delete(req);
    return 0;
}

int createUser(string username, string password){
    string path = string(ROOT) + username;
    if(mkdir(path.c_str(), 0777) != 0) return 0;

    pthread_mutex_lock(&global_mutex);

    FILE *fp;
    string filepath = string(ROOT) + "auth.txt" ;
    fp = fopen(filepath.c_str(), "a+");
    if(fp == NULL){
        perror("Error opening auth file\n");
        rmdir(path.c_str());
        pthread_mutex_unlock(&global_mutex);
        return 0;
    }

    fprintf(fp, "%s %s\n", username.c_str(), password.c_str());
    fclose(fp);

    pthread_mutex_unlock(&global_mutex);

    return 1;
}

void authenticate(int idx){
    int control_channel_fd = client[idx].control_channel_fd;
    char buffer[BUFF_SIZE] ; 
    string username = "", password;

    bzero(buffer, BUFF_SIZE); 
    read(control_channel_fd, buffer, BUFF_SIZE);

    if(strcmp(buffer, "LOGIN\n") == 0){
        while(1){
            bzero(buffer, BUFF_SIZE); 
            read(control_channel_fd, buffer, BUFF_SIZE);

            if(strncmp(buffer, "USER ", 5) == 0){
                username = buffer + 5;
                if(username.back() == '\n') username.pop_back();
                if(user_pass.find(username) == user_pass.end()){
                    write(control_channel_fd, "USER NOT FOUND\n", BUFF_SIZE);
                    username = "";
                }
                else{
                    write(control_channel_fd, "USER OK\n", BUFF_SIZE);
                }
            }
            else if(strncmp(buffer, "PASS ", 5) == 0  && username != ""){
                password = buffer + 5;
                if(password.back() == '\n') password.pop_back();
                if(user_pass[username] != password){
                    write(control_channel_fd, "PASSWORD WRONG\n", BUFF_SIZE);
                }
                else{
                    write(control_channel_fd, "LOGIN SUCCESSFUL\n", BUFF_SIZE);
                    break;
                }
            }
            else if(strcmp(buffer, "close\n") == 0){
            	return ;
            }
            else{
                write(control_channel_fd, "INVALID COMMAND\n", BUFF_SIZE);
            }
        }
    }
    else{
        while(1){
            bzero(buffer, BUFF_SIZE); 
            read(control_channel_fd, buffer, BUFF_SIZE);

            if(strncmp(buffer, "USER ", 5) == 0){
                username = buffer + 5;
                if(username.back() == '\n') username.pop_back();
                if(user_pass.find(username) != user_pass.end()){
                    write(control_channel_fd, "USERNAME ALREADY TAKEN\n", BUFF_SIZE);
                    username = "";
                }
                else{
                    write(control_channel_fd, "USER OK\n", BUFF_SIZE);
                }
            }
            else if(strncmp(buffer, "PASS ", 5) == 0  && username != ""){
                password = buffer + 5;
                if(password.back() == '\n') password.pop_back();
                int success = createUser(username, password);
                if(success) break;
                else{
                    write(control_channel_fd, "SIGNUP FAILED\n", BUFF_SIZE);
                    return ;
                }
            }
            else if(strcmp(buffer, "close\n") == 0){
            	return ;
            }
            else{
                write(control_channel_fd, "INVALID COMMAND\n", BUFF_SIZE);
            }
        }
        pthread_mutex_lock(&global_mutex);
        user_pass[username] = password;
        pthread_mutex_unlock(&global_mutex);
        write(control_channel_fd, "SIGNUP SUCCESSFUL\n", BUFF_SIZE);
    }

    client[idx].username = username ;
    client[idx].is_auth = true;
}

void *clientHandler(void *arg){
    int idx = *(int *)arg ;
    int control_channel_fd = client[idx].control_channel_fd;
    authenticate(idx);

    if(client[idx].is_auth == false){
        avl_indices[idx] = 1;
        close(control_channel_fd);
        pthread_mutex_lock(&global_mutex);
        ip_idx.erase(client[idx].ip_addr);
        pthread_mutex_unlock(&global_mutex);
        free((int*)arg) ;
        printf("\033[1;31mClient with sock_fd: %d disconnected\033[0m\n", control_channel_fd);
    
        return 0;
    }

    char buffer[BUFF_SIZE];  

    //printf("\033[1;32mClient with sock_fd: %d connected\033[0m\n", control_channel_fd);

    while(1){
		bzero(buffer, BUFF_SIZE); 
        read(control_channel_fd, buffer, BUFF_SIZE);
        printf("cmd recvd: %s\n", buffer);
    
        if(strncmp(buffer, "GET ", 4) == 0){
            TransferParam* get_req = new TransferParam();
            *get_req = getTransferParam(buffer, idx);

            if(fileExists(get_req->username, get_req->filename) == 0){
                write(control_channel_fd, "FILE DOES NOT EXISTS\n", BUFF_SIZE);
                continue;
            }
            //cout<<"starting file transfer"<<endl;

            client[idx].recent_data_channel_fd = -1;
            write(control_channel_fd, "CREATE DATA CHANNEL\n", BUFF_SIZE);
            bzero(buffer, BUFF_SIZE); 
            read(control_channel_fd, buffer, BUFF_SIZE);

            if(strcmp(buffer, "DATA CHANNEL CREATED\n") != 0) {
                if(client[idx].recent_data_channel_fd != -1){
                    close(client[idx].recent_data_channel_fd);
                }
                continue;
            }
           // cout<<"starting file transfer2"<<endl;
            get_req->data_channel_fd = client[idx].recent_data_channel_fd ;
            pthread_t th;
            if(pthread_create(&th, NULL, &sendFile, get_req) !=0 ) {
			    write(control_channel_fd, "Some ERROR occured in server side. Try again later.\n" , BUFF_SIZE); 
                close(get_req->data_channel_fd);
			    perror("ERROR: Thread creation for sending file failed\n");
            }

        }
        else if(strncmp(buffer, "PUT ", 4) == 0){
            TransferParam* put_req = new TransferParam();
            *put_req = getTransferParam(buffer, idx);

            // if(fileExists(get_req->username, get_req->filename) == 1){
            //     write(control_channel_fd, "FILE ALREADY EXISTS\n", BUFF_SIZE);
            //     continue;
            // }

            client[idx].recent_data_channel_fd = -1;
            write(control_channel_fd, "CREATE DATA CHANNEL\n", BUFF_SIZE);
            bzero(buffer, BUFF_SIZE); 
            read(control_channel_fd, buffer, BUFF_SIZE);

            if(strcmp(buffer, "DATA CHANNEL CREATED\n") != 0) {
                if(client[idx].recent_data_channel_fd != -1){
                    close(client[idx].recent_data_channel_fd);
                }
                continue;
            }
             put_req->data_channel_fd = client[idx].recent_data_channel_fd ;
            pthread_t th;
            if( pthread_create(&th, NULL, &recvFile, put_req) !=0 ) {
			    write(control_channel_fd, "Some ERROR occured in server side. Try again later.\n" , BUFF_SIZE); 
                close(put_req->data_channel_fd);
			    perror("ERROR: Thread creation for recieving file failed\n");
            }

        }
        else if(strcmp(buffer, "ls\n") == 0){
        	string file_names = listFiles(client[idx].username);
        	bzero(buffer, BUFF_SIZE); 
        	strcpy(buffer, file_names.c_str());
        	write(control_channel_fd, buffer, BUFF_SIZE);
        }
        else if(strcmp(buffer, "close\n") == 0){
        	break;
        }
        else{
             write(control_channel_fd, "Invalid command!\n", BUFF_SIZE);	

	}
	}
	
	avl_indices[idx] = 1;

    close(control_channel_fd);
    pthread_mutex_lock(&global_mutex);
    ip_idx.erase(client[idx].ip_addr);
    pthread_mutex_unlock(&global_mutex);
    free((int*)arg) ;
    printf("\033[1;31mClient with sock_fd: %d disconnected\033[0m\n", control_channel_fd);
    
    return 0;
}

void init(){
    pthread_mutex_init(&global_mutex, NULL); 

    for(int i=0;i<MAX_CONNECTION;i++){
	    avl_indices[i] = 1;
	}

    // string folderpath = string(ROOT);
    // folderpath.pop_back();
    // mkdir(folderpath.c_str(), 0777) ;
	
    FILE *fp;
    string filepath = string(ROOT) + "auth.txt" ;
    fp = fopen(filepath.c_str(), "r");
    if(fp == NULL){
       perror("Error opening auth file\n");
       exit(1);
    }

    char username[30], password[30];

    while(fscanf(fp, "%s", username) == 1){
        fscanf(fp, "%s", password);
        user_pass[username] = password;
    }

    fclose(fp);
}

int main(int argc , char *argv[]){
	if(argc < 2){
		printf("Enter %s <port_no>\n", argv[0]);
		exit(1);
	}

    PORT = atoi(argv[1]);
    int sock_fd = socketSetup(PORT);
    printf("\033[1;33mSERVER LISTENING on PORT: %d for control channels\033[0m\n\n", PORT);

    // listen for data channels;
    int *data_sock_ptr = (int *)malloc(sizeof(int));
    *data_sock_ptr =  socketSetup(PORT - 1);
    printf("\033[1;33mSERVER LISTENING on PORT: %d for data channels\033[0m\n\n", PORT-1);
    
    pthread_t th_data_channels ;
    if( pthread_create(&th_data_channels, NULL, &acceptDataChannels, data_sock_ptr) !=0 ) {
		perror("ERROR: Thread creation failed\n");
    }

    init(); /* initialize variables */

	struct sockaddr_in cli_addr;
	socklen_t cli_addr_len = sizeof(cli_addr);

	// server runs infinitely
    while(1){
        int new_sock_fd = accept(sock_fd, (struct sockaddr *) &cli_addr, (socklen_t *) &cli_addr_len);
        if(new_sock_fd < 0){
		    perror("ERROR: on accepting control channel connection\n");
			continue;
        }

        int *next_avl_ptr = (int *)malloc(sizeof(int));
        *next_avl_ptr = -1;
        for(int i=0;i<MAX_CONNECTION;i++){
            if(avl_indices[i] == 1){
                *next_avl_ptr = i;
                break;
            }
        }

		if(*next_avl_ptr == -1) {
			write(new_sock_fd, "MAX_CONNECTION limit reached. Try connecting again later.\n" , BUFF_SIZE); 
			close(new_sock_fd);
			continue;
		}

        string cli_ip = to_string(cli_addr.sin_addr.s_addr);
		
        client[*next_avl_ptr].control_channel_fd = new_sock_fd;
        client[*next_avl_ptr].ip_addr = cli_ip;
        client[*next_avl_ptr].is_auth = false;

        if(ip_idx.find(cli_ip) != ip_idx.end()){
           write(new_sock_fd, "Some other user is using the same IP address as yours. Try connecting using different IP address. Closing socket\n" , BUFF_SIZE); 
			close(new_sock_fd);

            continue;
        }

        pthread_mutex_lock(&global_mutex);
        ip_idx[cli_ip] = *next_avl_ptr ;
        pthread_mutex_unlock(&global_mutex);

	    pthread_t th ;
        if( pthread_create(&th, NULL, &clientHandler, next_avl_ptr) !=0 ) {
			write(new_sock_fd, "Some ERROR occured in server side. Try connecting again later.\n" , BUFF_SIZE); 
			close(new_sock_fd);

            pthread_mutex_lock(&global_mutex);
            ip_idx.erase(cli_ip) ;
            pthread_mutex_unlock(&global_mutex);

			perror("ERROR: Thread creation failed\n");
        }
        printf("\033[1;32mClient with sock_fd: %d connected\033[0m\n", client[*next_avl_ptr].control_channel_fd );

        avl_indices[*next_avl_ptr] = 0;

    }
	
	close(sock_fd);
	
	return 0;
}

