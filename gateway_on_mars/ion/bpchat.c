/*
 * bpchat.c
 * Andrew Jenkins <andrew.jenkins@colorado.edu>
 * Reads lines from stdin and sends those in bundles.
 * Receives bundles and writes them to stdout.
 */

#include <stdlib.h>
#include <stdio.h>
#include <bp.h>

#include <sys/un.h>  
#include <unistd.h>

static BpSAP                sap;
static Sdr                  sdr;
static pthread_mutex_t      sdrmutex = PTHREAD_MUTEX_INITIALIZER;
static char                 *destEid = NULL;
static char                 *ownEid = NULL;
static BpCustodySwitch      custodySwitch = NoCustodyRequested;
static int                  running = 1;


int sockfd,ret;  
 
struct sockaddr_un server_addr; //this is for bpchat self
struct sockaddr_un ccn_addr; //this is for socksaddr in sendto

#define BUFFER_SIZE 1000 

/* 包头 */
typedef struct 
{ 
  long int id; 
  int buf_size; 
 // unsigned int  crc32val;   //每一个buffer的crc32值
  int errorflag;
}PackInfo; 
  
/* 接收包 */
struct RecvPack 
{ 
  PackInfo head; 
  char buf[BUFFER_SIZE]; 
} data; 
  

const char usage[] =
"Usage: bpchat.c <source EID> <dest EID> [ct]\n\n"
"Reads lines from stdin and sends these lines in bundles.\n"
"Receives bundles and writes them to stdout.\n"
"If \"ct\" is specified, sent bundles have the custody transfer flag set\n";

static pthread_t    sendLinesThread;
static void *       sendLines(void *args)
{
	Object          bundleZco, bundlePayload;
	Object          newBundle;   /* We never use but bp_send requires it. */
	int             lineLength = 0;
	char            lineBuffer[10*1024*1024];
	//int             recv_num=0;
	int             file_id;
	
	while(running) {
	  
	  int len = 0; 
	    
	  
	    if((len = recvfrom(sockfd, (char*)&data, sizeof(data), 0, NULL,NULL)) < 0) 
	    { 
	    printf("recvfrom接受失败！\n"); 
			fprintf(stderr, "EOF\n");
			running = 0;
			bp_interrupt(sap);
			break; 
	 
	    } 
	    else    //接收完毕退出
	    { 
		printf("recv file from CCN\n");
	     
	    }
	    
	  
	  
	  printf("Receive File:\t%s From CCN Successful!\n", "/home/istin/ccn_tmp_file"); 
	  


/*
		recv_num=recvfrom(sockfd,lineBuffer,sizeof(lineBuffer),0, NULL, NULL);
		if(recv_num<0)
		{
			printf("调用recvfrom接受失败！\n"); 
			fprintf(stderr, "EOF\n");
			running = 0;
			bp_interrupt(sap);
			break; 
		} 
		printf("recv from ccn, length:%d\n",recv_num );



		lineLength = recv_num;
		printf("lineLength:%d\n",lineLength );
*/

	file_id = open("/home/istin/ccn_tmp_file", O_RDONLY);
	lineLength = read(file_id, lineBuffer, sizeof(lineBuffer));
	close(file_id);
	printf("lineLength:%d\n",lineLength );

		/* Wrap the linebuffer in a bundle payload. */
		if(pthread_mutex_lock(&sdrmutex) != 0)
		{
			putErrmsg("Couldn't take sdr mutex.", NULL);
			break;
		}

		oK(sdr_begin_xn(sdr));
		bundlePayload = sdr_malloc(sdr, lineLength);
		if(bundlePayload) {
			sdr_write(sdr, bundlePayload, lineBuffer, lineLength);
		}

		if(sdr_end_xn(sdr) < 0) {
			pthread_mutex_unlock(&sdrmutex);
			bp_close(sap);
			putErrmsg("No space for bpchat payload.", NULL);
			break;
		}

		bundleZco = ionCreateZco(ZcoSdrSource, bundlePayload, 0, 
			lineLength, BP_STD_PRIORITY, 0, ZcoOutbound, NULL);
		if(bundleZco == 0 || bundleZco == (Object) ERROR) {
			pthread_mutex_unlock(&sdrmutex);
			bp_close(sap);
			putErrmsg("bpchat can't create bundle ZCO.", NULL);
			break;
		}
		pthread_mutex_unlock(&sdrmutex);

		/* Send the bundle payload. */
		if(bp_send(sap, destEid, NULL, 86400, BP_STD_PRIORITY,
				custodySwitch, 0, 0, NULL, bundleZco,
				&newBundle) <= 0)
		{
			putErrmsg("bpchat can't send bundle.", NULL);
			break;
		}

	}
	return NULL;
}

static pthread_t    recvBundlesThread;
static void *       recvBundles(void *args)
{
	BpDelivery      dlv;
	ZcoReader       reader;
	char            buffer[1024];
	int             bundleLenRemaining;
	int             rc;
	int             bytesToRead;
	int             sendto_num=0;

	while(running)
	{
		if(bp_receive(sap, &dlv, BP_BLOCKING) < 0)
		{
			putErrmsg("bpchat bundle reception failed.", NULL);
			break;
		}

		if(dlv.result == BpReceptionInterrupted || dlv.adu == 0)
		{
			bp_release_delivery(&dlv, 1);
			continue;
		}

		if(dlv.result == BpEndpointStopped)
		{
			bp_release_delivery(&dlv, 1);
			break;
		}

		if(pthread_mutex_lock(&sdrmutex) != 0)
		{
			putErrmsg("Couldn't take sdr mutex.", NULL);
			break;
		}

		oK(sdr_begin_xn(sdr));
		bundleLenRemaining = zco_source_data_length(sdr, dlv.adu);
		zco_start_receiving(dlv.adu, &reader);
		while(bundleLenRemaining > 0)
		{
			bytesToRead = MIN(bundleLenRemaining, sizeof(buffer)-1);
			rc = zco_receive_source(sdr, &reader, bytesToRead,
					buffer);
			if(rc < 0) break;
			bundleLenRemaining -= rc;
			printf("recv from dtn length:%d\n",rc );
			printf("%.*s\n", rc, buffer);
			fflush(stdout);

			sendto_num=sendto(sockfd,buffer,strlen(buffer),0, (struct sockaddr *)(&ccn_addr), sizeof(ccn_addr));  
        if(sendto_num<0)  
            printf("sendto failed\n");  
        else  
        {  
            printf("sendto ccn success,length:%d bytes\n",sendto_num);  
        } 
        
		}

		if (sdr_end_xn(sdr) < 0)
		{
			running = 0;
		}

		pthread_mutex_unlock(&sdrmutex);
		bp_release_delivery(&dlv, 1);
	}        
	return NULL;
}

void handleQuit(int sig)
{
	running = 0;
	pthread_end(sendLinesThread);
	bp_interrupt(sap);
}

int main(int argc, char **argv)
{
	ownEid      = (argc > 1 ? argv[1] : NULL);
	destEid     = (argc > 2 ? argv[2] : NULL);
	char    *ctArg = (argc > 3 ? argv[3] : NULL);

	if(argc < 2 || (argv[1][0] == '-')) {
		fprintf(stderr, usage);
		exit(1);
	}

	if(ctArg && strncmp(ctArg, "ct", 3) == 0) {
		custodySwitch = SourceCustodyRequired;
	}

	if(bp_attach() < 0) {
		putErrmsg("Can't bp_attach()", NULL);
		exit(1);
	}

	if(bp_open(ownEid, &sap) < 0) 
	{
		putErrmsg("Can't open own endpoint.", ownEid);
		exit(1);
	}

	sdr = bp_get_sdr();

	signal(SIGINT, handleQuit);

	
   unlink("/tmp/my.sock");
    memset(&server_addr,0,sizeof(server_addr));  
    server_addr.sun_family=AF_UNIX;  
    strcpy(server_addr.sun_path,"/tmp/my.sock");  
    sockfd=socket(AF_UNIX,SOCK_DGRAM,0);  
    if (sockfd<0)  
    {  
        printf("调用socket函数建立socket描述符出错！1\n");  
         exit(1);  
    }  
    printf("调用socket函数建立socket描述符成功！\n");  
    ret=bind(sockfd,(struct sockaddr *)(&server_addr),sizeof(server_addr));  
    if (ret<0)  
    {  
        printf("调用bind函数绑定套接字与地址出错！\n");  
         exit(2);  
    }  
    printf("调用bind函数绑定套接字与地址成功！\n"); 

    memset(&ccn_addr,0,sizeof(ccn_addr)); 
    ccn_addr.sun_family=AF_UNIX;
    strcpy(ccn_addr.sun_path,"/tmp/ccn.sock"); //this path is as same as the ccn-relay
   

	/* Start receiver thread and sender thread. */
	if(pthread_begin(&sendLinesThread, NULL, sendLines, NULL) < 0) {
		putErrmsg("Can't make sendLines thread.", NULL);
		bp_interrupt(sap);
		exit(1);
	}

	if(pthread_begin(&recvBundlesThread, NULL, recvBundles, NULL) < 0) {
		putErrmsg("Can't make recvBundles thread.", NULL);
		bp_interrupt(sap);
		exit(1);
	}

	pthread_join(sendLinesThread, NULL);
	pthread_join(recvBundlesThread, NULL);

	bp_close(sap);
	bp_detach();
	return 0;
}
