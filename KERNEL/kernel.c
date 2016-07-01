/* Kernel.c by pacevedo */
/* Compilame asi:
gcc -I/usr/include/parser -I/usr/include/commons -I/usr/include/commons/collections -o kernel socketCommons.c libs/stack.c libs/pcb.c libs/serialize.c kernel.c -L/usr/lib -lcommons -lparser-ansisop -pthread
 * El CPU compilalo asi:
gcc -I/usr/include/parser -I/usr/include/commons -I/usr/include/commons/collections -o cpu implementation_ansisop.c libs/socketCommons.c libs/stack.c libs/pcb.c libs/serialize.c cpu.c -L/usr/lib -lcommons -lparser-ansisop -lm
 * La UMC compilala asi:
gcc -I/usr/include/commons -I/usr/include/commons/collections -o umc umc.c -L/usr/lib -pthread -lcommons
 * Y la consola asi:
gcc -o test_pablo_console socketCommons/socketCommons.c test_pablo_console.c
*/
#include <libs/pcb_tests.h>
#include "kernel.h"

/* BEGIN OF GLOBAL STUFF I NEED EVERYWHERE */
int consoleServer, cpuServer, clientUMC, configFileFD;
int configFileWatcher;
int maxSocket=0;
char* configFileName;
char* configFilePath;
t_log   *kernel_log;
t_list  *PCB_READY, *PCB_BLOCKED, *PCB_EXIT;
t_list  *consolas_conectadas, *cpus_conectadas, *cpus_executing;
t_list **solicitudes_io;
fd_set 	 allSockets;
/* END OF GLOBAL STUFF I NEED EVERYWHERE */

int main (int argc, char **argv){
	kernel_log = log_create("kernel.log", "Elestac-KERNEL", true, LOG_LEVEL_TRACE);
	PCB_READY = list_create();
	PCB_BLOCKED = list_create();
	PCB_EXIT = list_create();
	cpus_conectadas = list_create();
	cpus_executing = list_create();
	consolas_conectadas = list_create();
	if (start_kernel(argc, argv[1])<0) return 0; //load settings
	clientUMC=connect2UMC();
	//clientUMC=100;setup.PAGE_SIZE=1024; //TODO Delete -> lo hace connect2UMC()
	if (clientUMC<0){
		log_error(kernel_log, "Could not connect to the UMC. Please, try again.");
		return 0;
	}
	if(setServerSocket(&consoleServer, setup.KERNEL_IP, setup.PUERTO_PROG)<0){
		log_error(kernel_log,"Error while creating the CONSOLE server.");
		return 0;
	}
	if(setServerSocket(&cpuServer, setup.KERNEL_IP, setup.PUERTO_CPU)<0){
		log_error(kernel_log,"Error while creating the CPU server.");
		return 0;
	}
	maxSocket=cpuServer;
	log_info(kernel_log,"Servers to CPUs and consoles up and running. Waiting for incoming connections.");
	while (control_clients());
	log_error(kernel_log, "Closing kernel.");
	inotify_rm_watch(configFileFD, configFileWatcher);
	close(configFileFD);
	close(consoleServer);
	close(cpuServer);
	close(clientUMC); // TODO un-comment when real UMC is present
	log_destroy(kernel_log);
	return 0;
}

int start_kernel(int argc, char* configFile){
	printf("\n\t===========================================\n");
	printf("\t.:: Vamo a calmarno que viene el Kernel ::.");
	printf("\n\t===========================================\n\n");
	if (argc==2){
		if (loadConfig(configFile)<0) {
			log_error(kernel_log, "Config file can not be loaded. Please, try again.");
    		return -1;
    	}
//		else{
//			configFileName = realloc(configFileName, sizeof(configFile));
//			strcpy(configFileName, configFile);
//			char cwd[1024];
//			getcwd(cwd, sizeof(cwd));
//			configFilePath = realloc(configFilePath, sizeof(cwd));
//			strcpy(configFilePath, cwd);
//		}
//		configFileFD = inotify_init();
//		configFilePath = "/home/alan/repos/tp-2016-1c-Vamo-a-calmarno/KERNEL"; // TODO borrar fuera de testing
//		configFileWatcher = inotify_add_watch(configFileFD, configFilePath, IN_MODIFY | IN_CREATE);
	} else {
		int i = 0;
		for (i = 0; i < 10001; i++){
			usleep(400);
			printf("\r\e[?25l Loading... %d", i/100);
		}
    	printf("\r Usage: ./kernel setup.data \n");
		log_error(kernel_log, "Config file was not provided.");
    	return -1;
	}
	signal (SIGINT, tratarSeniales);
	signal (SIGPIPE, tratarSeniales);
	return 0;
}

void* do_work(void *p) {
	int miID = *((int *) p);
	t_io *io_op;
	log_info(kernel_log, "do_work: Starting the device %s with a sleep of %s milliseconds.", setup.IO_ID[miID], setup.IO_SLEEP[miID]);
	while(1){
		sem_wait(&semaforo_io[miID]);
		log_info(kernel_log, "A new I/O request arrived at %s", setup.IO_ID[miID]);
		pthread_mutex_lock(&mut_io_list);
		io_op = list_remove(solicitudes_io[miID], 0);
		pthread_mutex_unlock(&mut_io_list);
		if (io_op != NULL){
			log_info(kernel_log,"%s will perform %s operations.", setup.IO_ID[miID], io_op->io_units);
			int processing_io = atoi(setup.IO_SLEEP[miID]) * atoi(io_op->io_units) * 1000;
			// TODO Carefull here !! usleep() may fail when processing_io is > 1 sec.
			// TODO Example provided in setup.data has delays bigger than 1 sec.
			usleep((useconds_t) processing_io);
			bool match_PCB(void *pcb){
				t_pcb *unPCB = pcb;
				bool matchea = (io_op->pid==unPCB->pid);
				return matchea;
			}
			t_pcb *elPCB;
			elPCB = list_remove_by_condition(PCB_BLOCKED, match_PCB);
			if (elPCB != NULL ){
				elPCB->status = READY;
				list_add(PCB_READY, elPCB);
			} // Else -> The PCB was killed by end_program while performing I/O
			log_info(kernel_log, "do_work: Finished an io operation on device %s requested by PID %d.", setup.IO_ID[miID], io_op->pid);
			free(io_op);
		}
	}
}

int loadConfig(char* configFile){
	int counter = 0, i = 0;
	pthread_t *io_device;
	if (configFile == NULL)	return -1;
	t_config *config = config_create(configFile);
	log_info(kernel_log, "Loading settings.");
	if (config != NULL){
		setup.PUERTO_PROG = config_get_int_value(config,"PUERTO_PROG");
		setup.PUERTO_CPU = config_get_int_value(config,"PUERTO_CPU");
		setup.QUANTUM = config_get_int_value(config,"QUANTUM");
		setup.QUANTUM_SLEEP = config_get_int_value(config,"QUANTUM_SLEEP");
		setup.IO_ID = config_get_array_value(config,"IO_ID");
		setup.IO_SLEEP = config_get_array_value(config,"IO_SLEEP");
		while(setup.IO_ID[counter])
			counter++;
		setup.IO_COUNT = counter;
		solicitudes_io = realloc(solicitudes_io, counter * sizeof(t_list));
		semaforo_io = realloc(semaforo_io, counter * sizeof(sem_t));
		io_device = malloc(sizeof(pthread_t) * counter);
		for (i = 0; i < counter; i++) {
			solicitudes_io[i] = list_create();
			sem_init(&semaforo_io[i], 0, 0);
			int *thread_id = malloc(sizeof(int));
			*thread_id=i;
			pthread_create(&io_device[i],NULL,do_work, thread_id);
		}
		setup.SEM_ID=config_get_array_value(config,"SEM_ID");
		setup.SEM_INIT=config_get_array_value(config,"SEM_INIT");
		counter=0;
		setup.SHARED_VARS=config_get_array_value(config,"SHARED_VARS");
		while(setup.SHARED_VARS[counter])
			counter++;
		setup.SHARED_VALUES = realloc(setup.SHARED_VALUES, counter * sizeof(int));
		for (i = 0; i < counter; i++) {
			setup.SHARED_VALUES[counter]=0;
		}
		setup.STACK_SIZE=config_get_int_value(config,"STACK_SIZE");
		setup.PUERTO_UMC=config_get_int_value(config,"PUERTO_UMC");
		setup.IP_UMC=config_get_string_value(config,"IP_UMC");
		setup.KERNEL_IP=config_get_string_value(config,"KERNEL_IP");
	}
	return 0;
}

int connect2UMC(){
	int clientUMC;
	char* buffer;
	char* buffer_4=malloc(4);
	log_info(kernel_log, "Connecting to UMC on %s:%d.", setup.IP_UMC, setup.PUERTO_UMC);
	if (getClientSocket(&clientUMC, setup.IP_UMC, setup.PUERTO_UMC)) return (-1);
	sprintf(buffer_4, "%04d", setup.STACK_SIZE);
	asprintf(&buffer, "%s%s", "0", buffer_4);
	send(clientUMC, buffer, 5 , 0);
	log_info(kernel_log, "Stack size (sent to UMC): %s.",buffer_4);
	if (recv(clientUMC, buffer_4, 4, 0) < 0) return (-1);
	setup.PAGE_SIZE=atoi(buffer_4);
	log_info(kernel_log, "Page size: (received from UMC): %d.",setup.PAGE_SIZE);
	free(buffer_4);
	return clientUMC;
}

void *requestPages2UMC(void* request_buffer){
	int deserialize_index = 0;
	char PID[4];
	int ansisopLen=0;
	char * code = NULL;
	int clientUMC = 0;
	deserialize_data(PID, sizeof(int), request_buffer, &deserialize_index);
	deserialize_data(&ansisopLen, sizeof(int), request_buffer, &deserialize_index);
	code = malloc((size_t) ansisopLen);
	deserialize_data(code, ansisopLen, request_buffer, &deserialize_index);
	deserialize_data(&clientUMC, sizeof(int), request_buffer, &deserialize_index);
	char* buffer;
	char buffer_4[4];
	int bufferLen=1+4+4+4+ansisopLen; //1+PID+req_pages+size+code
	sprintf(buffer_4, "%04d", (ansisopLen/setup.PAGE_SIZE)+1);
	log_info(kernel_log, "Requesting %s pages to UMC.", buffer_4);
	asprintf(&buffer, "%d%04d%s%04d%s", 1,*(int*)PID,buffer_4, ansisopLen,code);
	send(clientUMC, buffer, (size_t) bufferLen, 0);
	recv(clientUMC, buffer_4, 4, 0);
	log_info(kernel_log, "UMC replied.");
	int code_pages = atoi(buffer_4);
	free(buffer);
	createNewPCB(*(int*) (PID), code_pages, code);
}
void tratarSeniales(int senial){
	printf("\n\t=============================================\n");
	printf("\t\tSystem received the signal: %d",senial);
	printf("\n\t=============================================\n");
	switch (senial){
	case SIGINT:
		// Detecta Ctrl+C y evita el cierre.
		printf("Esto acabará con el sistema. Presione Ctrl+C una vez más para confirmar.\n\n");
		signal (SIGINT, SIG_DFL); // solo controlo una vez.
		break;
	case SIGPIPE:
		// Trato de escribir en un socket que cerro la conexion.
		printf("La consola o el CPU con el que estabas hablando se murió. Llamá al 911.\n\n");
		break;
	default:
		printf("Otra senial\n");
		break;
	}
}

void add2FD_SET(void *client){
	t_Client *cliente=client;
	FD_SET(cliente->clientID, &allSockets);
}

t_pcb * recvPCB(int cpuID){
	t_pcb *incomingPCB = NULL;
	int pcb_size;
	char *tmp_buff = malloc(sizeof(int));
	recv(cpuID, tmp_buff, sizeof(int), 0);
	pcb_size = *(int*) tmp_buff;
	void *pcb_serializado = malloc((size_t) pcb_size);
	recv(cpuID, pcb_serializado, (size_t) pcb_size, 0);
//    printSerializedPcb(pcb_serializado);
	incomingPCB = (t_pcb *)calloc(1,sizeof(t_pcb));
	int pcb_serializado_cursor = 0;
	deserialize_pcb(&incomingPCB, pcb_serializado, &pcb_serializado_cursor);
	testSerializedPCB(incomingPCB, pcb_serializado);
	free(tmp_buff);
	free(pcb_serializado);
	return incomingPCB;
}

void restoreCPU(t_Client *laCPU){
	bool getCPUIndex(void *nbr){
		t_Client *unaCPU = nbr;
		bool matchea = (laCPU->clientID == unaCPU->clientID);
		return matchea;
	}
	list_remove_by_condition(cpus_executing, getCPUIndex);
	laCPU->status = READY;
	laCPU->pid = 0;
	list_add(cpus_conectadas, laCPU); /* return the CPU to the queue */
}

void check_CPU_FD_ISSET(void *cpu){
	char *cpu_protocol = malloc(1);
	int setValue = 0;
	t_Client *laCPU = (t_Client*) cpu;
	char *tmp_buff = malloc(4);
	if (FD_ISSET(laCPU->clientID, &allSockets)) {
		log_debug(kernel_log,"CPU %d has something to say.", laCPU->clientID);
		if (recv(laCPU->clientID, cpu_protocol, 1, 0) > 0){
			log_info(kernel_log,"CPU sent protocol ID: %s.",cpu_protocol);
			t_io *io_op = malloc(sizeof(t_io)); // TODO Free esto sin matar lo que puse en la lista
			t_pcb *incomingPCB = recvPCB(laCPU->clientID); // TODO Free esto sin romper nada
			switch (atoi(cpu_protocol)) {
			case 1:// Quantum end
			case 2:// Program END
				if (laCPU->status == EXIT || incomingPCB->status==EXIT){
					list_add(PCB_EXIT, incomingPCB);
				} else {
					list_add(PCB_READY, incomingPCB);
				}
				restoreCPU(laCPU);
				break;
			case 3:// IO
				io_op->pid = laCPU->pid;
				recv(laCPU->clientID, tmp_buff, 4, 0); // size of the io_name
				recv(laCPU->clientID, io_op->io_name, (size_t) atoi(tmp_buff), 0);
				recv(laCPU->clientID, io_op->io_units, 4, 0);
				io_op->io_index = getIOindex(io_op->io_name);
				t_pcb *blockedPCB = recvPCB(laCPU->clientID);
				if (io_op->io_index < 0 || laCPU->status == EXIT) {
					log_error(kernel_log, "AnSisOp program request an unplugged device or the console has been closed. #VamoACalmarno");
					list_add(PCB_EXIT,blockedPCB);
				} else {
					pthread_mutex_lock(&mut_io_list);
					list_add(solicitudes_io[io_op->io_index], io_op);
					pthread_mutex_unlock(&mut_io_list);
					list_add(PCB_BLOCKED,incomingPCB);
				}
				restoreCPU(laCPU);
				break;
			case 4:// semaforo
				//wait   [identificador de semáforo]
				//signal [identificador de semáforo]
				break;
			case 5:// var compartida
				recv(laCPU->clientID, tmp_buff, 1, 0);
				if (strncmp(tmp_buff, "1",1) == 0) setValue = 1;
				recv(laCPU->clientID, tmp_buff, 4, 0);
				size_t varNameSize = (size_t) atoi(tmp_buff);
				char *theShared = malloc(varNameSize);
				recv(laCPU->clientID, theShared, varNameSize, 0);
				int sharedIndex = getSharedIndex(theShared);
				if (setValue == 1) {
					recv(laCPU->clientID, tmp_buff, 4, 0);//recv & set the value
					int theVal = atoi(tmp_buff);
					setup.SHARED_VALUES[sharedIndex] = theVal;
				} else {
					char *sharedValue = malloc(4);
					sprintf(sharedValue, "%04d", setup.SHARED_VALUES[sharedIndex]);
					send(laCPU->clientID, sharedValue, 4, 0); // send the value to the CPU
					free(sharedValue);
				}
				free(theShared);
				break;
			case 6:// imprimirValor
				recv(laCPU->clientID, tmp_buff, 4, 0);
				size_t nameSize = (size_t) atoi(tmp_buff);
				char *theName = malloc(nameSize);
				recv(laCPU->clientID, theName, nameSize, 0);
				recv(laCPU->clientID, tmp_buff, 4, 0);
				char *value2console = malloc(1+4+nameSize+4);
				asprintf(&value2console, "%d%04d%s%04d", 1, nameSize, theName, tmp_buff);//1+nameSize+name+value
				send(laCPU->pid, value2console, (9+nameSize), 0); // send the value to the console
				free(theName);
				free(value2console);
				break;
			case 7:// imprimirTexto
				recv(laCPU->clientID, tmp_buff, 4, 0);
				size_t txtSize = (size_t) atoi(tmp_buff);
				char *theTXT = malloc(txtSize);
				recv(laCPU->clientID, theTXT, txtSize, 0);
				char *txt2console = malloc(1+4+txtSize);
				asprintf(&txt2console, "%d%04d%s", 2, txtSize, theTXT);
				send(laCPU->pid, txt2console, (5+txtSize), 0); // send the text to the console
				free(theTXT);
				free(txt2console);
				break;
			default:
				log_error(kernel_log,"Caso no contemplado. CPU dijo: %s",cpu_protocol);
			}
			call_handlers();
		} else {
			log_info(kernel_log,"CPU %d has closed the connection.", laCPU->clientID);
			close(laCPU->clientID);
			bool getCPUIndex(void *nbr){
				t_Client *unCliente = nbr;
				bool matchea = (laCPU->clientID == unCliente->clientID);
				if (matchea){
					end_program(laCPU->pid, true, false);
				}
				return matchea;
			}
			if (list_size(cpus_conectadas) > 0)
				list_remove_by_condition(cpus_conectadas, getCPUIndex);
			if (list_size(cpus_executing) > 0)
				list_remove_by_condition(cpus_executing, getCPUIndex);
		}
	}
	free(cpu_protocol);
	free(tmp_buff);
}

void destroy_PCB(void *pcb){
	t_pcb *unPCB = pcb;
	free(unPCB);
}

void check_CONSOLE_FD_ISSET(void *console){
	char *buffer_4=malloc(4);
	t_Client *cliente = console;
	if (FD_ISSET(cliente->clientID, &allSockets)) {
		if (recv(cliente->clientID, buffer_4, 1, 0) == 0){
			log_info(kernel_log,"A console has closed the connection, the associated PID %04d will be terminated.", cliente->clientID);
			end_program(cliente->clientID, false, true);
		}
	}
	free(buffer_4);
}

int control_clients(){
	int newConsole,newCPU;
	struct timeval timeout = {.tv_sec = 1};
	FD_ZERO(&allSockets);
	FD_SET(cpuServer, &allSockets);
	FD_SET(consoleServer, &allSockets);
	FD_SET(configFileFD, &allSockets);
	list_iterate(consolas_conectadas,add2FD_SET);
	list_iterate(cpus_conectadas,add2FD_SET);
	list_iterate(cpus_executing,add2FD_SET);
	int retval=select(maxSocket+1, &allSockets, NULL, NULL, &timeout); // (retval < 0) <=> signal
	if (retval>0) {
		if (FD_ISSET(configFileFD, &allSockets)) {
			char configFileBuff[EVENT_BUF_LEN];
			ssize_t limit = read(configFileFD, configFileBuff, EVENT_BUF_LEN);
			if (limit > 0 ){
				int base = 0;
				while (base < limit ) {
					struct inotify_event *event = ( struct inotify_event * ) &configFileBuff[base];
					if ((strcmp(event->name, configFileName)==0) & ((event->mask & IN_CREATE) || (event->mask & IN_MODIFY))){
						t_config *config = config_create(configFileName);
						if (config != NULL && config_has_property(config,"QUANTUM") && config_has_property(config,"QUANTUM_SLEEP")){
							setup.QUANTUM = config_get_int_value(config,"QUANTUM");
							setup.QUANTUM_SLEEP = config_get_int_value(config,"QUANTUM_SLEEP");
							config_destroy(config);
							log_info(kernel_log, "New config file loaded. QUANTUM=%d & QUANTUM_SLEEP=%d.", setup.QUANTUM, setup.QUANTUM_SLEEP);
						}
					}
					base += EVENT_SIZE + event->len;
				}
			}
		}
		list_iterate(consolas_conectadas,check_CONSOLE_FD_ISSET);
		list_iterate(cpus_conectadas,check_CPU_FD_ISSET);
		list_iterate(cpus_executing,check_CPU_FD_ISSET);
		if ((newConsole=accept_new_client("console", &consoleServer, &allSockets, consolas_conectadas)) > 1){
			accept_new_PCB(newConsole);
		}
		newCPU=accept_new_client("CPU", &cpuServer, &allSockets, cpus_conectadas);
		if(newCPU>0) log_info(kernel_log,"New CPU accepted with ID %d",newCPU);
	}
	call_handlers();
	return 1;
}

int accept_new_client(char* what,int *server, fd_set *sockets,t_list *lista){
	int aceptado=0;
	char buffer_4[4];
	if (FD_ISSET(*server, &*sockets)){
		if ((aceptado=acceptConnection(*server)) < 1){
			log_error(kernel_log,"Error while trying to Accept() a new %s.",what);
		} else {
			maxSocket=aceptado;
			if (recv(aceptado, buffer_4, 1, 0) > 0){
				if (strncmp(buffer_4, "0",1) == 0){
					t_Client *cliente=malloc(sizeof(t_Client));
					cliente->clientID=aceptado;
					cliente->pid=0;
					cliente->status = 0;
					list_add(lista, cliente);
					log_info(kernel_log, "New %s arriving (%d).", what, list_size(lista));
				}
			} else {
				log_error(kernel_log,"Error while trying to read from a newly accepted %s.",what);
				close(aceptado);
				return -1;
			}
		}
	}
	return aceptado;
}

void accept_new_PCB(int newConsole){
	char buffer_4[4];
	log_info(kernel_log, "NEW (0) program with PID=%04d arriving.", newConsole);
	recv(newConsole, buffer_4, 4, 0);
	int ansisopLen = atoi(buffer_4);
	char *code = malloc((size_t) ansisopLen);
	recv(newConsole, code, (size_t) ansisopLen, 0);
	/* Recv ansisop from console */
	void * request_buffer = NULL;
	int request_buffer_index = 0;
	serialize_data(&newConsole, sizeof(int), &request_buffer, &request_buffer_index);
	serialize_data(&ansisopLen, sizeof(int), &request_buffer, &request_buffer_index);
	serialize_data(code, (size_t) ansisopLen, &request_buffer, &request_buffer_index);
	serialize_data(&clientUMC, sizeof(int), &request_buffer, &request_buffer_index);
	pthread_t newPCB_thread;
	pthread_create(&newPCB_thread, NULL, requestPages2UMC, request_buffer);
	free(code); // let it free
}

void createNewPCB(int newConsole, int code_pages, char* code){
	char PID[4];
	sprintf(PID,"%04d",newConsole);
	if (code_pages>0){
		log_info(kernel_log, "Pages of code + stack = %d.", code_pages);
		send(newConsole,PID,4,0);
		t_metadata_program* metadata = metadata_desde_literal(code);
		t_pcb *newPCB=malloc(sizeof(t_pcb));
		newPCB->pid=newConsole;
		newPCB->program_counter=metadata->instruccion_inicio;
		newPCB->stack_pointer=code_pages;
		newPCB->stack_index=queue_create();
		newPCB->status=READY;
		newPCB->instrucciones_size= metadata->instrucciones_size;
		newPCB->instrucciones_serializado = metadata->instrucciones_serializado;
		newPCB->etiquetas_size = metadata->etiquetas_size;
		newPCB->etiquetas = metadata->etiquetas;
		list_add(PCB_READY,newPCB);
		log_info(kernel_log, "The program with PID=%04d is now READY (%d).", newPCB->pid, newPCB->status);
		log_info(kernel_log, "Consoles after accepting: %d.", list_size(PCB_READY));
	} else {
		send(newConsole,"0000",4,0);
		log_error(kernel_log, "The program with PID=%04d could not be started. System run out of memory.", newConsole);
		close(newConsole);
	}
}

void round_robin(){
	int       tmp_buffer_size = 1+sizeof(int)*3; /* 1+QUANTUM+QUANTUM_SLEEP+PCB_SIZE */
	int       pcb_buffer_size = 0;
	void     *pcb_buffer = NULL;
	void     *tmp_buffer = NULL;
	t_Client *laCPU = list_remove(cpus_conectadas,0);
	t_pcb    *tuPCB = list_remove(PCB_READY,0);
	tuPCB->status = EXECUTING;
	laCPU->status = EXECUTING;
	laCPU->pid = tuPCB->pid;
	serialize_pcb(tuPCB, &pcb_buffer, &pcb_buffer_size);
	tmp_buffer = malloc((size_t) tmp_buffer_size+pcb_buffer_size);
	asprintf(&tmp_buffer, "%d%04d%04d%04d", 1, setup.QUANTUM, setup.QUANTUM_SLEEP, pcb_buffer_size);
    tmp_buffer = realloc(tmp_buffer , (size_t) tmp_buffer_size + pcb_buffer_size);
	serialize_data(pcb_buffer, (size_t ) pcb_buffer_size, &tmp_buffer, &tmp_buffer_size );
	log_info(kernel_log,"Submitting to CPU %d the PID %d.", laCPU->clientID, tuPCB->pid);
	send(laCPU->clientID, tmp_buffer, tmp_buffer_size, 0);
	free(tmp_buffer);
    free(pcb_buffer);
	list_add(cpus_executing,laCPU);
}

void end_program(int pid, bool consoleStillOpen, bool cpuStillOpen) { /* Search everywhere for the PID and kill it ! */
/*
 * CASOS
 *
 * 1) CPU cierra la conexion -> no tengo el PCB en ningun lado, tengo el PID
 *      a) Avisar a UMC
 *      b) Avisar a consola
 * 2) Consola cierra la conexion -> tengo el PID
 *      a) Avisar a UMC
 *      b) Buscar PCB y borrarlo
 *          i) puede estar ready, blocked o exit (casos lindos)
 *          ii) puede estar executing -> cambiar status de CPU
 *          iii) la CPU puede haber muerto -> matar socket de consola y listo
 * 3) Programa termina, hay error en ejecución de CPU o problema en UMC -> esta en PCB_EXIT
 *      a) Avisar a UMC
 *      b) Buscar PCB y borrarlo
 *          i) seguro está en exit (pero no pierdo nada con buscar en los otros)
 *      c) Avisar a consola
 * 4) It's new... (probably waiting for UMC to reply with requested pages)
 *      a) Puedo ignorar este caso si no cierro el socket con consola... en la próxima vuelta el PCB va a estar en PCB_READY.
 */
	char* buffer=malloc(5);
	bool pcb_found = false;
	bool match_PCB(void *pcb){
		t_pcb *unPCB = pcb;
		bool matchea = (pid==unPCB->pid);
		if (matchea)
			pcb_found = true;
		return matchea;
	}
	if (list_size(PCB_READY) > 0)
		list_remove_and_destroy_by_condition(PCB_READY,match_PCB,destroy_PCB);
	if (list_size(PCB_BLOCKED) > 0)
		list_remove_and_destroy_by_condition(PCB_BLOCKED,match_PCB,destroy_PCB);
	if (list_size(PCB_EXIT) > 0)
		list_remove_and_destroy_by_condition(PCB_EXIT,match_PCB,destroy_PCB);

	if (cpuStillOpen){
		if (list_size(cpus_executing) > 0){
			bool getCPUIndex(void *nbr) {
				t_Client *aCPU = nbr;
				return (pid == aCPU->pid);
			}
			t_Client *theCPU;
			theCPU = list_find(cpus_executing, getCPUIndex);
			theCPU->status = EXIT;
		}
	} else {
		pcb_found = true;
	}

	if (pcb_found == true) {
		sprintf(buffer, "%d%04d", 2, pid);
		send(clientUMC, buffer, 5, 0);
	}
	if (consoleStillOpen) send(pid, "0", 1, 0); // send exit code to console
	close(pid); // close console socket
	bool getConsoleIndex(void *nbr) {
		t_Client *unCliente = nbr;
		return (pid == unCliente->clientID);
	}
	list_remove_by_condition(consolas_conectadas, getConsoleIndex);
	free(buffer);
}

void process_io() {
	int i;
	for (i = 0; i < setup.IO_COUNT; i++) {
		if (list_size(solicitudes_io[i]) > 0) {
			sem_post(&semaforo_io[i]);
		}
	}
}

int getIOindex(char *io_name) {
	int i;
	for (i = 0; i < setup.IO_COUNT; i++) {
		if (strcmp(setup.IO_ID[i],io_name) == 0) {
			return i;
		}
	}
	return -1;
}

int getSharedIndex(char *shared_name) {
	int i = 0;
	while (setup.SHARED_VARS[i]){
		if (strcmp(setup.SHARED_VARS[i],shared_name) == 0) {
			return i;
		}
		i++;
	}
	return -1;
}

void call_handlers() {
	while (list_size(PCB_EXIT) > 0) {
		t_pcb *elPCB;
		elPCB = list_get(PCB_EXIT, 0);
		end_program(elPCB->pid, true, false);
	}
	if (list_size(PCB_BLOCKED) > 0) process_io();
	while (list_size(cpus_conectadas) > 0 && list_size(PCB_READY) > 0 ) round_robin();
}
