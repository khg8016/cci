#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "cci.h"

#define ACCEPT_CONTEXT (void*)0xfeebdaed
#define SEND_CONTEXT (void*)0xdaedfeeb
#define MAX_CONNECTION_SIZE 1024
#define MAX_BUFFER_SIZE 8192

int main(int argc, char *argv[])
{
	int ret, number_of_connections=0;
	int client_end[MAX_CONNECTION_SIZE]={0,};
	uint32_t caps = 0;
	char *uri = NULL;	
	long file_size[MAX_CONNECTION_SIZE]={0,};
	cci_endpoint_t *endpoint = NULL;
	cci_os_handle_t *ep_fd = NULL;
	cci_connection_t *connection[MAX_CONNECTION_SIZE] = {NULL,};   

	ret = cci_init(CCI_ABI_VERSION, 0, &caps);
	if (ret) {
		fprintf(stderr, "cci_init() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	/* create an endpoint */
	ret = cci_create_endpoint(NULL, 0, &endpoint, ep_fd);
	if (ret) {
		fprintf(stderr, "cci_create_endpoint() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}
	ret = cci_get_opt(endpoint,
			  CCI_OPT_ENDPT_URI, &uri);
	if (ret) {
		fprintf(stderr, "cci_get_opt() failed with %s\n", cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}
	printf("Opened %s\n", uri);

	while (1) { 
		int accept = 1;
		cci_event_t *event;		
		ret = cci_get_event(endpoint, &event);
		if (ret != 0) {
			if (ret != CCI_EAGAIN)
				fprintf(stderr, "cci_get_event() returned %s\n",
			cci_strerror(endpoint, ret));
			continue;
		}
		switch (event->type) {
			
		case CCI_EVENT_RECV:{
				char buf[MAX_BUFFER_SIZE];
				char *number, *data;
				char exit_msg[32];
				long read_size=0;
				int len = event->recv.len;				
				int i=0, j=0;
				int id;
				
				memset(buf, 0, MAX_BUFFER_SIZE);
				memcpy(buf, event->recv.ptr, len);	
				if(strncasecmp((char*)event->recv.ptr,"client", 6)==0){ /* 문자인 경우 */ 	
					fprintf(stderr, "%s", buf); 

					strtok(buf," :");	 /* 필요한 정보 파싱 (누구한테, 첫번째 문자)*/
					number=strtok(NULL," :");	
					data=strtok(NULL," :");	
					id=atoi(number); //누구한테서 왔는지

					memcpy(buf, event->recv.ptr, len);						
					if(event->recv.connection==connection[id-1]){
						if(strncasecmp(data,"bye\n", 4)==0){  //종료를 원할 경우 
		            		fprintf(stderr, "Client%d want to termainate this program.\n", id);
		            		client_end[id-1]=1;                      		       
			            }else if(strncasecmp(data,"file\n", 5)==0) //file 전송을 원할 경우			
							fprintf(stderr, "Client %d send a file...\n", id);		       
													
						for(j=0; j<number_of_connections; j++){
							if(j !=id-1 && !client_end[j]) //받는놈은 종료되지 않은놈이고 보내는놈이 아님
								ret = cci_send(connection[j], buf, len, SEND_CONTEXT, 0); //보냄	
							if(client_end[id-1]){  //보내는놈이 종료를 원한 경우
								sprintf(exit_msg,"client%d exits..\n", id);
								ret = cci_send(connection[j], exit_msg, strlen(exit_msg), SEND_CONTEXT, 0); //종료메세지 보냄	
							}
						} //end for j
					}
	            }else{   /* 파일인 경우 */	                 		
					for(i=0; i<number_of_connections; i++){ //어떤 connection에서 왔는지 check
						if(event->recv.connection == connection[i]){
							if(strncasecmp((char*)event->recv.ptr,"file send completed\0", 20)==0){ //file 전송종료신호면 그만				
								fprintf(stderr,"%s. file size is %ldbytes.\n",(char*)event->recv.ptr, file_size[i]);						
								memcpy(buf,event->recv.ptr, len); //종료신호 고대로 클라에게
								read_size=len;
								file_size[i]=0;
							}else{
								read_size=event->recv.len;	//읽은 크기 
								file_size[i]+=read_size; //file size check		
								fprintf(stderr, "%ld\n", file_size[i]);		
								memcpy(buf, event->recv.ptr, read_size); //읽은거 buf에 써주기			            			
							}//end file trasport
							for(j=0; j<number_of_connections; j++){ /* broadcast */
									if(j !=i && !client_end[j]){ 
										ret = cci_send(connection[j], buf, read_size, SEND_CONTEXT, 0); 
										if(ret)
											fprintf(stderr, "file send failed!\n");
									}
							}//end for j
							break;
	            		}//end if	            					
					}//end for i
	            }													
				break;
			}//end recv case

		case CCI_EVENT_SEND:

			assert(event->send.context == SEND_CONTEXT);
			assert(event->send.connection->context == ACCEPT_CONTEXT);
			fprintf(stderr, "completed send\n");
			break;

		case CCI_EVENT_CONNECT_REQUEST:
			if (accept) {
				cci_accept(event, ACCEPT_CONTEXT);
			}else {
				cci_reject(event);
			}
			break;

		case CCI_EVENT_ACCEPT:{
		    char number[MAX_CONNECTION_SIZE];

			assert(event->accept.connection != NULL);
			assert(event->accept.connection->context == ACCEPT_CONTEXT);
			
			connection[number_of_connections] = event->accept.connection;			
			fprintf(stderr, "completed accept\n");
		
            sprintf(number,"%d",number_of_connections+1);
            ret = cci_send(connection[number_of_connections], number, strlen(number), SEND_CONTEXT, 0); //몇번째 클라이언트 인지 알려줌
            number_of_connections++;
			break;
		}
		default:
			printf("event type %d\n", event->type);
			break;
		} //end switch

		cci_return_event(event);
	}

	/* clean up */
	cci_destroy_endpoint(endpoint);
	cci_finalize();
	free(uri);

	return 0;
}
