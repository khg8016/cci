#include "cci.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#define CONNECT_CONTEXT (void*)0xdeadbeef
#define SEND_CONTEXT (void*)0xdaedfeec
#define END_CONTEXT (void*)0xdaedfeed
#define MAX_BUFFER_SIZE 8192
int flags = 0;
cci_conn_attribute_t attr = CCI_CONN_ATTR_RO;

typedef struct Thread_data{	
	char *id;
    int flag;
    cci_connection_t *connection;
}t_data;    

static void poll_events(cci_endpoint_t * endpoint, cci_connection_t ** connection, int *done, char *id);
void *send_msg(void* param);
void *file_send(void *param);

int main(int argc, char *argv[])
{

	cci_os_handle_t *fd = NULL; //endpoint 생성시, process를 block하는데 사용
	cci_endpoint_t *endpoint = NULL;
	cci_connection_t *connection = NULL;
	int ret, c, ft_start = 0 , done = 0, i = 0, connect = 0;
    pthread_t send;	
	uint32_t caps = 0;//??
	char *server_uri = NULL;
	char id[16]="";
	t_data thread_data;

	while ((c = getopt(argc, argv, "h:c:b")) != -1) { //client 실행시 option check
		switch (c) {
		case 'h':
			server_uri = strdup(optarg);//-h에 대한 인자를 가리키는  optarg 포인터가 생김 http://weed2758.tistory.com/entry/Linux-C-getopt-%ED%95%A8%EC%88%98
			break;
		case 'c':
			if (strncasecmp ("ru", optarg, 2) == 0)
				attr = CCI_CONN_ATTR_RU;
			else if (strncasecmp ("ro", optarg, 2) == 0)
				attr = CCI_CONN_ATTR_RO;
			else if (strncasecmp ("uu", optarg, 2) == 0)
				attr = CCI_CONN_ATTR_UU;
			break;
		case 'b':
			flags |= CCI_FLAG_BLOCKING;
			break;
		default:
			fprintf(stderr, "usage: %s -h <server_uri> [-c <type>]\n",
			        argv[0]);
			fprintf(stderr, "\t-c\tConnection type (UU, RU, or RO) "
			                "set by client; RO by default\n");
			exit(EXIT_FAILURE);
		}
	}

	if (!server_uri) {
		fprintf(stderr, "usage: %s -h <server_uri> [-c <type>]\n", argv[0]);
		fprintf(stderr, "\t-c\tConnection type (UU, RU, or RO) "
                                        "set by client; RO by default\n");
		exit(EXIT_FAILURE);
	}

	ret = cci_init(CCI_ABI_VERSION, 0, &caps);
	if (ret) {
		fprintf(stderr, "cci_init() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	/* create an endpoint */
	ret = cci_create_endpoint(NULL, 0, &endpoint, fd);
	if (ret) {
		fprintf(stderr, "cci_create_endpoint() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}


	if (ret) {
		fprintf(stderr, "cci_set_opt() failed with %s\n",
			cci_strerror(endpoint, ret));
		exit(EXIT_FAILURE);
	}

	/* initiate connect */
	ret = cci_connect(endpoint, server_uri, "Connect request", 15, attr, CONNECT_CONTEXT, 0, NULL);
	if (ret) {
		fprintf(stderr, "cci_connect() failed with %s\n",
			cci_strerror(endpoint, ret));
		exit(EXIT_FAILURE);
	}
	
    while (!done)
       poll_events(endpoint, &connection, &done, id);

    
	if (!connection)
		exit(0);

    done = 0;
    thread_data.connection=connection;
    thread_data.flag=flags;
    thread_data.id=id;
    pthread_create(&send,NULL,send_msg,&thread_data);

    while(!done)
       poll_events(endpoint, &connection, &done, id);

	pthread_join(send,NULL);

	/* clean up */
	ret = cci_destroy_endpoint(endpoint);
	if (ret) {
		fprintf(stderr, "cci_destroy_endpoint() failed with %s\n",
			cci_strerror(endpoint, ret));
		exit(EXIT_FAILURE);
	}

	ret = cci_finalize();
	if (ret) {
		fprintf(stderr, "cci_finalize() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	return 0;
}

void *send_msg(void* param){//채팅 보내는 스레드, 받는거는 그냥 main 스레드가 하게 했음      
    char data[MAX_BUFFER_SIZE], output[MAX_BUFFER_SIZE];       
    char* temp_str = NULL;  
    int ret, end_client = 0;    
    uint32_t len;  
    t_data *argv=(t_data*)param;
    char *id = argv->id;    
	pthread_t file_thread;	

    memset(data,0x00,MAX_BUFFER_SIZE);
    while(!end_client){    
    	memcpy(output, id, strlen(argv->id));	//client #
		temp_str=fgets(data, MAX_BUFFER_SIZE, stdin); //문자열 입력 받기 		

		strcat(output, ":"); //client #:~~~이거 붙여줌 
		strcat(output, data); 

		len=strlen(output);
		if(strncasecmp(data,"bye\n",4)==0){ //종료를 원할 경우 
			fprintf(stderr,"exit program...\n");
           	end_client=1;
            ret = cci_send(argv->connection, output, len, END_CONTEXT, argv->flag);
            if (ret)
              	fprintf(stderr, "bye error\n");
            continue;
		}
		ret = cci_send(argv->connection, output, len ,SEND_CONTEXT, argv->flag); //문자열 서버로 보내줌
		if (ret)
			fprintf(stderr, "send failed\n");

		if(strncasecmp(data,"file\n",5)==0) //file 전송 원할 경우
			pthread_create(&file_thread, NULL, file_send, param);    
	
    	memset(output,0,MAX_BUFFER_SIZE);	
    }
}

void *file_send(void *param){

    char buf[MAX_BUFFER_SIZE];
	long file_size=0, cur_size=0, read_size=0;
	int ret;
    FILE* file;    
    t_data *argv=(t_data*)param;

    memset(buf, 0x00, MAX_BUFFER_SIZE);
    fprintf(stderr, "file send start!\n");

	file = fopen("test.bmp", "rb");
	if(!file)
		fprintf(stderr, "file is not opened\n");

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);    	/*file size check*/	
    fseek(file, 0, SEEK_SET);

    while(cur_size != file_size)
    {	
    	read_size = fread(buf, 1, argv->connection->max_send_size, file);
    	cur_size = cur_size+read_size;//현재까지 읽은 크기
    	fprintf(stderr, "%ld%%\n",(cur_size*100)/file_size );
       	ret = cci_send(argv->connection, buf, read_size, SEND_CONTEXT, argv->flag); //읽은만큼 보내줌      
       	if (ret)
			fprintf(stderr, "file send failed\n");								
    }
    ret = cci_send(argv->connection, "file send completed\0", 20, SEND_CONTEXT, argv->flag); //파일 종료 신호 보냄
    if (ret)
		fprintf(stderr, "send failed\n");
    fprintf(stderr,"file size is %ldbytes. file send completed\n",file_size);    		
    fclose(file);   
}

static void poll_events(cci_endpoint_t * endpoint, cci_connection_t ** connection, int *done, char *id)
{
	int ret;
	char buffer[MAX_BUFFER_SIZE];
	cci_event_t *event;
	static FILE *fp;
	static int ft_start = 0;
	ret = cci_get_event(endpoint, &event); //event 받아옴

	if (ret == CCI_SUCCESS && event) { // event가 제대로 받아 와 지면
		switch (event->type) {

		case CCI_EVENT_SEND:{ 
			if(event->send.context==END_CONTEXT)//bye일 경우
                *done=1;
            else 
            	assert(event->send.context==SEND_CONTEXT);    
            
			assert(event->send.connection == *connection);
			assert(event->send.connection->context == CONNECT_CONTEXT);
			break;
		}

		case CCI_EVENT_RECV:{	

			int len = event->recv.len;
			assert(event->recv.connection == *connection);
			assert(event->recv.connection->context == CONNECT_CONTEXT);
			memcpy(buffer, event->recv.ptr, len);

			if(strcasecmp(id,"")==0){
				strcpy(id,"client ");
				memcpy(id+7, buffer, 16); 
				fprintf(stderr, "Welcome to Chatting program! You are %s\n", id);							
				*done = 1;
				break;
			}

			if(strncasecmp((char*)event->recv.ptr, id, 7)==0){//채팅일경우 					
				buffer[len] = '\0';
				fprintf(stderr, "%s", buffer); //상대방 대화내용 띄워줌
				break;
			}else{ //file 전송 일 경우
				if(!ft_start){//처음온 경우
					fprintf(stderr, "file recv...\n");
					fp = fopen("test12.bmp", "wb");
					if(!fp)
						fprintf(stderr, "file open failed!\n");
					ft_start=1;					
					fwrite(buffer,1, len,fp);
				}else if(strncasecmp(buffer, "file send completed\0",20)==0){//파일 전송이 끝난 경우
					fprintf(stderr, "file recv complete\n");
					fclose(fp);
					ft_start=0;
				}else if(ft_start){
					fwrite(buffer,1, len,fp);	
				}
			} //end else

			break;
		}//end recv

		case CCI_EVENT_CONNECT: //	An outgoing connection request has completed.
			assert(event->connect.connection != NULL);
			assert(event->connect.connection->context == CONNECT_CONTEXT);
				*connection = event->connect.connection;
			break;
		default:
			fprintf(stderr, "ignoring event type %d\n",
				event->type);
		}
		cci_return_event(event);
	}
}
