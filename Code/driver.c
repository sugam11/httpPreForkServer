#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <poll.h>
#include <stropts.h>
#define MAX_POOL_SIZE 30

key_t msgkey=3645;

struct msg_node{
	long int msg_type;
	char timestamp[128];
	struct in_addr ipaddress;
	unsigned short port;
	char agent[256];
	int req_size;
	char qstr[128];
	clock_t ptime;
	int error;
};


struct stats_struct{
    int pid[MAX_POOL_SIZE];
    int busy[MAX_POOL_SIZE];
    int count[MAX_POOL_SIZE];
};

typedef struct stats_struct stats;

void childFxn(stats *stat,int initialPoolSize, key_t key);

static int process_index_no = 0;
int connfd;

void initialProcessPool(stats * stat,int initialPoolSize, key_t key ){
    
    for(int i = 0;i < initialPoolSize;i++){
        pid_t pid;
        pid = fork();
        if(pid == 0){
            //printf("Process created with id : %d\n",getpid());
            int shmid = shmget(key,MAX_POOL_SIZE*sizeof(struct stats_struct),0666);
            stats * stat = shmat(shmid,NULL,0);
            childFxn(stat,initialPoolSize, key);
        }
        else if(pid > 0){
            stat->pid[i] = pid;
            stat->busy[i]=-1;
            stat->count[i]=0;
            process_index_no++;
            printf("Child created with id : %d at index %d\n",stat->pid[i],process_index_no);
        }
        else
            perror("Failed to start process pool\n");
    }
}

void updatePool(stats * stat,key_t key, int initialPoolSize ){
    int i,busy_count=0,idle_count=0;
    
    for(i=0;i<MAX_POOL_SIZE;i++)
        if(stat->busy[i]==1)
            busy_count++;
        else if(stat->busy[i]==0)
            idle_count++;
    
    if(idle_count<1*initialPoolSize/4)
        while(idle_count<1*initialPoolSize/4){
            pid_t pid;
            pid = fork();
            idle_count++;
            if(pid == 0){
                //printf("Process created with id : %d\n",getpid());
                int shmid = shmget(key,MAX_POOL_SIZE*sizeof(struct stats_struct),0666);
                stats * stat = shmat(shmid,NULL,0);
                pause();
            }
            else if(pid > 0){
                int i;
                for(i=0;i<MAX_POOL_SIZE;i++)
                    if(stat->pid[i]==-1)
                        break;
                stat->pid[i] = pid;
                stat->busy[i]=-1;
                stat->count[i]=0;
                process_index_no++;
                printf("Child created with id : %d at index %d\n",stat->pid[i],process_index_no);
            }
            else
                perror("Failed to start process pool\n");
        }
    
    else if(idle_count>1*initialPoolSize/4)
        while(idle_count>1*initialPoolSize/4){
            int max=0,i,max_index=0;
            idle_count--;
            for(i=0;i<MAX_POOL_SIZE;i++){
                if(max<=stat->count[i] && stat->busy[i]==0){
                    max = stat->count[i];
                    max_index = i;
                }
            }
            printf("child with id %d killed & index %d,process id %d, i=%d\n",stat->pid[max_index],max_index,getpid(),i);
            kill(stat->pid[max_index],SIGKILL);
            stat->pid[max_index]=-1;
            stat->busy[max_index]=-1;
            stat->count[max_index]=0;
        }
}

char *find_content_type (char *file_ext) {
    char *p;  // pointer to the type found
    char buf2[64];
    
    p = (char *)malloc(64);
    
    /* find the type: */
    if ( strcmp(file_ext, "html") == 0 || strcmp (file_ext, "hml") == 0) {
        strcpy (buf2, "Content-Type: text/html \r\n");
    }
    
    else if ( strcmp(file_ext, "txt") == 0) {
        strcpy (buf2, "Content-Type: text/plain \r\n");
    }
    else if ( strcmp(file_ext, "pdf") == 0) {
        strcpy (buf2, "Content-Type: application/pdf \r\n");
    }
    else if ( strcmp(file_ext, "doc") == 0) {
        strcpy (buf2, "Content-Type: application/msword \r\n");
    }
    else if ( strcmp(file_ext, "jpg") == 0 || strcmp (file_ext, "jpeg") == 0) {
        strcpy (buf2, "Content-Type: image/jpeg \r\n");
    }
    
    else if ( strcmp(file_ext, "gif") == 0) {
        strcpy (buf2, "Content-Type: image/gif \r\n");
    }
    
    else if ( strcmp(file_ext, "ico") == 0) {
        strcpy (buf2, "Content-Type: image/gif \r\n");
    }
    
    else {
        strcpy (buf2, "Content-Type: image/ico \r\n");
    }
    
    p = buf2;
   // printf ("content-type: %s\n", p);
    //return "Content-type: image/jpeg\r\n";
    return p;
}



void childFxn(stats *stat,int initialPoolSize, key_t key){
    int my_index,i,accept_fd,msg_len,recvRqstSize,flag=0;
    
    unsigned int clilen = sizeof(struct sockaddr_in);
    char buffer[1024],buffer_copy[1024];
    char del[3] = "\r\n";
    char file_del[2] = " ";
    char file_ext_del[3] = " .?";
    char header_line_copy[256],file_name_copy[128],*temp;
    char *header_line,*file_name,*file_ext;
    clock_t start,end;
    //char *htmlcontent;
    
    
    int msgid = msgget(msgkey,0666);
    
    for(i = 0; i < MAX_POOL_SIZE; i++)
        if(stat->pid[i] == getpid()){
            my_index = i;
            break;
        }
    
    while(1){
        stat->busy[my_index] = 0;
        //memset(&client,0,clilen);
        struct   sockaddr_in client;
        struct msg_node m;
        //printf("before accept\n");
        printf("ready to accept\n");
        stat->busy[my_index] = 0;
        accept_fd = accept(connfd,(struct sockaddr *)&client, &clilen);
        printf("\nClient accepted at %d and process id : %d\n",accept_fd,getpid());
        struct pollfd mypoll = { accept_fd, POLLIN|POLLPRI };
        stat->busy[my_index] = 1;
        stat->count[my_index]++;
        updatePool(stat,key,initialPoolSize);
    //Read request
        //while(1){
            //memset(buffer,0,sizeof(buffer));
            //set timer !!
            //printf("adada\n");
            while (1) {
            	if( poll(&mypoll, 1, 300000) )
            		//printf("adadagadgsfhbh\n");
                	recvRqstSize = recv(accept_fd,buffer,sizeof(buffer),0);
                else{
                	close(accept_fd);
                	break;
                }
                start = clock();
                
                printf("%s\n",buffer);
                fflush(stdout);
                memset(buffer_copy,0,sizeof(buffer_copy));
                strcpy(buffer_copy,buffer);
                
                //memset(header_line,0,sizeof(header_line));
                header_line = strtok(buffer_copy,del);
                temp = strtok(NULL,del);
                temp = strtok(NULL,del);
                strcpy(m.agent,temp);
               // printf("%s\n",header_line);
                
                memset(header_line_copy,0,sizeof(header_line_copy));
                strcpy(header_line_copy,header_line);
                
                //printf("asfasf : %s\n",header_line_copy);
                
                //memset(file_name,0,sizeof(file_name));
                file_name = strtok(header_line_copy,file_del);
                file_name = strtok(NULL,file_del);
                memmove(file_name, file_name+1, strlen(file_name));
               // printf("File name is : %s\n",file_name);
                
                memset(file_name_copy,0,sizeof(file_name_copy));
                strcpy(file_name_copy,file_name);
                
                file_ext = strtok(file_name_copy,file_ext_del);
                file_ext = strtok(NULL,file_ext_del);
               // printf("File ext is : %s\n",file_ext);
               
                time_t clk = time(NULL);
               	strcpy(m.timestamp,ctime(&clk));
               	m.ipaddress = client.sin_addr;
               	m.port = client.sin_port;
               	m.req_size = recvRqstSize;
               	strcpy(m.qstr,file_name);
               	m.msg_type = 1;
               	
               	if(strcmp(file_ext,"cgi") != 0){
            int file_des;
            printf("file name is : %s\n ",file_name);
            file_des = open(file_name,O_RDONLY,0);
            struct stat st;
            fstat(file_des,&st);
            int file_size = st.st_size;
            
            char status_code[50],response[1024];
            char header_buff [512];
            if(file_des == -1){
                //404 NOT FOUND
                strcpy(status_code,"404 Not Found\r\n");
                strcpy(response,"HTTP/1.1 ");
                strcat(response,status_code);
                
                time_t clk = time(NULL);
                strcat(response,"Date: ");
                strcat(response,ctime(&clk));
                //strcat(response,"\r\n");
                
                strcat(response,"Server: ");
                strcat(response,"webserver/0.1 (Mac64)\r\n");
                
                strcat(response,"Content-Length: 230\r\n");
                
                strcat(response,"Connection : Closed\r\n");
                strcat(response,"Content-Type: text/html; charset=iso-8859-1\r\n");
                
                strcat(response,"\r\n");
                
                strcat(response,"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\"><html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>");
                if(send(accept_fd,response,strlen(response),0) < strlen(response)){
                    perror("Error sending");
                }                
            }
            else{
                char send_buf[1024];
                int n;
                strcpy (header_buff, "HTTP/1.1 200 OK\r\nContent-Length: ");
                /* content-length: */
                char str_file_size[10];
                sprintf(str_file_size,"%d",file_size);
                strcat (header_buff, str_file_size);
                strcat (header_buff, "\r\n");
                /* content-type: */
                strcat (header_buff, find_content_type (file_ext));
                //printf ("%s\n", find_content_type (file_ext));
                strcat (header_buff, "Connection: keep-alive\r\n\r\n");
                send(accept_fd,header_buff,strlen(header_buff),0);
                while((n = read(file_des,send_buf,1024)) > 0){
                    send(accept_fd,send_buf,n,0);
                }
                close(file_des);
            }
            
        }
/*        else{*/
/*        	if(flag==0){*/
/*        		flag=1;*/
/*        		*/
/*        	}*/
/*        }*/
		
        end = clock();
        m.ptime = end-start;
        msgsnd(msgid,(const void *)&m,sizeof(struct msg_node),0);
                
/*                if(buffer[recvRqstSize-1] == '\n' && buffer[recvRqstSize-2] == '\r' &&buffer[recvRqstSize-3] == '\n' &&buffer[recvRqstSize-4] == '\r'){*/
/*                        //printf("breaking from inner while\n");*/
/*                        close(accept_fd);*/
/*                        break;*/
/*                    }*/
                
                }
    //Process request
        
        }
    //}
    close(accept_fd);
    exit(0);
}

int main(int argc,char *argv[]){
    //Variable declarations
    int status;
    int initialPoolSize = atoi(argv[1]);
    int portnum = atoi(argv[2]);
    int i;
    key_t key = 1414;
    
        //Connection variables
    struct sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    
    struct msg_node mnode;
    int msgid = msgget(msgkey,IPC_CREAT|0666);
    
    if (msgid == -1) {
	  perror("msgget failed with error");
	  exit(EXIT_FAILURE);
	}
	
	FILE *fp;
	fp = fopen("log.csv","w+");
    
        //shared memory variables
    int shmid;
    shmid = shmget(key,MAX_POOL_SIZE*sizeof(struct stats_struct),IPC_CREAT|0666);
    stats * stat = shmat(shmid,NULL,0);
    //End variable declarations
    
    for(i=0;i<MAX_POOL_SIZE;i++){
        stat->pid[i]=-1;
        stat->busy[i]=-1;
        stat->count[i]=0;
    }
    
    if( (connfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        perror("socket failed");
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");;
    serv_addr.sin_port = htons(portnum);
    
    if(bind(connfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) < 0){
        perror("Failed to bind");
        return -1;
    }
    else{
        printf("Server bound at %d port\n",portnum);
    }
    if(listen(connfd, MAX_POOL_SIZE) < 0){
        perror("Failed to listen");
        return -1;
    }
    else{
        printf("Server Listening at %d port\n",portnum);
    }
    
    initialProcessPool(stat,initialPoolSize,key);
    fprintf(fp,"%s,%s,%s,%s,%s,%s,%s\n","user ipaddress","user port","user agent","request size","query string","process time","timestamp");
    
    while(1) {
	  	if (msgrcv(msgid, (void *)&mnode, sizeof(struct msg_node), 1, 0) == -1) {
	    	perror("msgcrv failed with error");
	    	exit(EXIT_FAILURE);
	  	}
	  	//printf("typ=%d,stamp=%s,ip=%d\n,port=%d,agent=%s\n,req=%d,query=%s\n,ptime=%d", mnode.msg_type,mnode.timestamp,mnode.ipaddress.s_addr,mnode.port,mnode.agent,mnode.req_size,mnode.qstr,mnode.ptime);
	  	//fprintf(fp,"%s","asnfkj");
	  	fprintf(fp,"%d,%d,%s,%d,%s,%d,%s",mnode.ipaddress.s_addr,mnode.port,mnode.agent,mnode.req_size,mnode.qstr,mnode.ptime,mnode.timestamp);
	  	fflush(fp);
	}
    fclose(fp);
    pid_t pid;
    while((pid = wait(&status)) > 0){
        printf("status is : %d and pid of exiting process is : %d\n",status,pid);
    }
    return 0;
    
}
