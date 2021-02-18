#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>

void *ThreadMain(void *argument);
bdaddr_t bdaddr_any = {
	0, 0, 0, 0, 0, 0
}
;
bdaddr_t bdaddr_local = {
	0, 0, 0, 0xff, 0xff, 0xff
}
;
int _str2uuid( const char *uuid_str, uuid_t *uuid ) {
	/* This is from the pybluez stack */
	uint32_t uuid_int[4];
	char *endptr;
	if( strlen( uuid_str ) == 36 ) {
		char buf[9] = {
			0
		}
		;
		if( uuid_str[8] != '-' && uuid_str[13] != '-' &&
		                                        uuid_str[18] != '-' && uuid_str[23] != '-' ) {
			return 0;
		}
		// first 8-bytes
		strncpy(buf, uuid_str, 8);
		uuid_int[0] = htonl( strtoul( buf, &endptr, 16 ) );
		if( endptr != buf + 8 ) return 0;
		// second 8-bytes
		strncpy(buf, uuid_str+9, 4);
		strncpy(buf+4, uuid_str+14, 4);
		uuid_int[1] = htonl( strtoul( buf, &endptr, 16 ) );
		if( endptr != buf + 8 ) return 0;
		// third 8-bytes
		strncpy(buf, uuid_str+19, 4);
		strncpy(buf+4, uuid_str+24, 4);
		uuid_int[2] = htonl( strtoul( buf, &endptr, 16 ) );
		if( endptr != buf + 8 ) return 0;
		// fourth 8-bytes
		strncpy(buf, uuid_str+28, 8);
		uuid_int[3] = htonl( strtoul( buf, &endptr, 16 ) );
		if( endptr != buf + 8 ) return 0;
		if( uuid != NULL ) sdp_uuid128_create( uuid, uuid_int );
	} else if ( strlen( uuid_str ) == 8 ) {
		// 32-bit reserved UUID
		uint32_t i = strtoul( uuid_str, &endptr, 16 );
		if( endptr != uuid_str + 8 ) return 0;
		if( uuid != NULL ) sdp_uuid32_create( uuid, i );
	} else if( strlen( uuid_str ) == 4 ) {
		// 16-bit reserved UUID
		int i = strtol( uuid_str, &endptr, 16 );
		if( endptr != uuid_str + 4 ) return 0;
		if( uuid != NULL ) sdp_uuid16_create( uuid, i );
	} else {
		return 0;
	}
	return 1;
}
sdp_session_t *register_service(uint8_t rfcomm_channel) {
	/* A 128-bit number used to identify this service. The words are ordered from most to least
                                                                                                                                                                                                                                                                                                * significant, but within each word, the octets are ordered from least to most significant.
                                                                                                                                                                                                                                                                                                    * For example, the UUID represneted by this array is 00001101-0000-1000-8000-00805F9B34FB. (The
                                                                                                                                                                                                                                                                                                        * hyphenation is a convention specified by the Service Discovery Protocol of the Bluetooth Core
                                                                                                                                                                                                                                                                                                            * Specification, but is not particularly important for this program.)
                                                                                                                                                                                                                                                                                                                *
                                                                                                                                                                                                                                                                                                                    * This UUID is the Bluetooth Base UUID and is commonly used for simple Bluetooth applications.
                                                                                                                                                                                                                                                                                                                        * Regardless of the UUID used, it must match the one that the Armatus Android app is searching
                                                                                                                                                                                                                                                                                                                            * for.
                                                                                                                                                                                                                                                                                                                                */
	const char *service_name = "Armatus Bluetooth server";
	const char *svc_dsc = "A HERMIT server that interfaces with the Armatus Android app";
	const char *service_prov = "Armatus";
	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid,
	                                                                                                                                                                                                                                                                                                                                                           svc_class_uuid;
	sdp_list_t *l2cap_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                               *rfcomm_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                                                *root_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                                                                  *proto_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                                                                                     *access_proto_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                                                                                                         *svc_class_list = 0,
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                              *profile_list = 0;
	sdp_data_t *channel = 0;
	sdp_profile_desc_t profile;
	sdp_record_t record = {
		0
	}
	;
	sdp_session_t *session = 0;
	// set the general service ID
	//sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
	_str2uuid("00001101-0000-1000-8000-00805F9B34FB",&svc_uuid);
	sdp_set_service_id(&record, svc_uuid);
	char str[256] = "";
	sdp_uuid2strn(&svc_uuid, str, 256);
	printf("Registering UUID %s\n", str);
	// set the service class
	sdp_uuid16_create(&svc_class_uuid, SERIAL_PORT_SVCLASS_ID);
	svc_class_list = sdp_list_append(0, &svc_class_uuid);
	sdp_set_service_classes(&record, svc_class_list);
	// set the Bluetooth profile information
	sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
	profile.version = 0x0100;
	profile_list = sdp_list_append(0, &profile);
	sdp_set_profile_descs(&record, profile_list);
	// make the service record publicly browsable
	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups(&record, root_list);
	// set l2cap information
	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);
	// register the RFCOMM channel for RFCOMM sockets
	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);
	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(&record, access_proto_list);
	// set the name, provider, and description
	sdp_set_info_attr(&record, service_name, service_prov, svc_dsc);
	// connect to the local SDP server, register the service record,
	// and disconnect
	session = sdp_connect(&bdaddr_any, &bdaddr_local, SDP_RETRY_IF_BUSY);
	sdp_record_register(session, &record, 0);
	// cleanup
	sdp_data_free(channel);
	sdp_list_free(l2cap_list, 0);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svc_class_list, 0);
	sdp_list_free(profile_list, 0);
	return session;
}
char input[1024] = {
	0
}
;
char *read_server(int client) {
	// read data from the client
	int bytes_read;
	bytes_read = read(client, input, sizeof(input));
	if (bytes_read > 0) {
		printf("received [%s]\n", input);
		return input;
	} else {
		return NULL;
	}
}
void write_server(int client, char *message) {
	// send data to the client
	char messageArr[1024] = {
		0
	}
	;
	int bytes_sent;
	strcpy(messageArr, message);
	bytes_sent = write(client, messageArr, strlen(messageArr));
	if (bytes_sent > 0) {
		printf("sent [%s] %d\n", messageArr, bytes_sent);
	}
}
int main() {
	pthread_t thread_id;
	signal( SIGPIPE, SIG_IGN );
	int port = 3, result, sock, client, bytes_read, bytes_sent;
	struct sockaddr_rc loc_addr = {
		0
	}
	, rem_addr = {
		0
	}
	;
	char buffer[1024] = {
		0
	}
	;
	socklen_t opt = sizeof(rem_addr);
	// local bluetooth adapter
	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_bdaddr = bdaddr_any;
	loc_addr.rc_channel = (uint8_t) port;
	// register service
	sdp_session_t *session = register_service(port);
	// allocate socket
	sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	printf("socket() returned %d\n", sock);
	// bind socket to port 3 of the first available
	result = bind(sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
	printf("bind() on channel %d returned %d\n", port, result);
	// put socket into listening mode
	result = listen(sock, 1);
	printf("listen() returned %d\n", result);
	//sdpRegisterL2cap(port);
	while(1) {
		// accept one connection
		printf("calling accept()\n");
		client = accept(sock, (struct sockaddr *)&rem_addr, &opt);
		printf("accept() returned %d\n", client);
		ba2str(&rem_addr.rc_bdaddr, buffer);
		fprintf(stderr, "accepted connection from %s\n", buffer);
		memset(buffer, 0, sizeof(buffer));
		pthread_create( &thread_id, NULL, ThreadMain, (void*)client);
	}
}
void *ThreadMain(void *argument) {
	char buf[1024];
	pthread_detach(pthread_self());
	int client = (int)argument;
	
	//hancheol code
	int spi_channel=0;
    int spi_speed = 1000000;
    int adc_channel = 0;
    
    wiringPiSetup();
    if(wiringPiSPISetup(spi_channel, spi_speed) == -1)
        return -1;
    
    unsigned char buffer[3];
    int v[5];
    
    
    //code finished
	while(1) {
	
	//hanchoel code2
		for(int i=0;i<5;i++){
	        buffer[0] = 1;
	        buffer[1] = (8 + adc_channel + i) << 4;
	        buffer[2] = 0;
 
	        wiringPiSPIDataRW(spi_channel,buffer,3);

	        v[i]=((buffer[1]&3)<<8)+buffer[2];
	        printf("channel : %d value :%d\n",i ,v[i]);
	        
	        
	
		}//finished
		
		
		//char data[1024];
		
		int binary_data = 0;
		static char send_data[5]="";
		static int size=0;
		
		if(v[0]<100){
			binary_data=binary_data+1;
		}
		if(v[1]<180){
			binary_data=binary_data+2;
		}
		if(v[2]<150){
			binary_data=binary_data+4;
		}
		if(v[3]<130){
			binary_data=binary_data+8;
		}
		if(v[4]<120){
			binary_data=binary_data+16;
		}
		
		switch(binary_data)
		{
			case 0:
				strcpy(send_data,"9");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 1:
				strcpy(send_data,"6");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 2:
				strcpy(send_data,"5");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 3:
				strcpy(send_data,"19");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 4:
				strcpy(send_data,"4");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 6:
				strcpy(send_data,"17");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 7:
				strcpy(send_data,"10");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 8:
				strcpy(send_data,"3");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
			
			case 9:
				strcpy(send_data,"18");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 10:
				strcpy(send_data,"11");;
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 14:
				strcpy(send_data,"13");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
			
			case 15:
				strcpy(send_data,"1");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 16:
				strcpy(send_data,"7");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 17:
				strcpy(send_data,"20");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 19:
				strcpy(send_data,"16");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 20:
				strcpy(send_data,"12");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 22:
				strcpy(send_data,"14");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 29:
				strcpy(send_data,"15");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
				
			case 31:
				strcpy(send_data,"2");
				size = strlen(send_data);
				send_data[size] = '\n';
				write_server(client, send_data);
				break;
					
			
			
			
		}
		
		
		
		/*char buf[1024];
		scanf("%s", buf);
		int size = strlen(buf);
		buf[size] = '\n';
		printf("%s\n", buf);
		write_server(client, buf);*/
		delay(1000);
	}
	printf("disconnected\n" );
	close(client);
	return 0;
}
