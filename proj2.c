#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/mman.h>


int *dlzka_fronty = 0; // oznacuje pocet ludi stojacich v rade (po vykonaní "enter")
int *i_log = 0; // cislo vypisu v log_file
int *pocet_odvezenych_pasazierov = 0; // pocet

int i_log_id;
int dlzka_fronty_id;
int pocet_odvezenych_pasazierov_id;

sem_t *pristup_k_rade;
sem_t *pristup_k_autobusu;
sem_t *pristup_k_exitu;
sem_t *pristup_k_i_log;
sem_t *pristup_k_vystupovaniu;
sem_t *pristup_k_e_vystupovaniu;

FILE *f;

void sem_err();
void shm_err(int x);
void fork_err();
void inicializuj_semafory();
void odstran_semafory();
void inicializuj_pamat();
void odstran_pamat();
void write_log(char* NAME, int I, char* STATE, int CR);
int vypocet_kapacity(int dlzka_rady, int kapacita);
void jazda_autobusu(int r, int kapacita, int abt);
void cestujuci_pasazier(int moje_id);
void generator_pasazierov(int r, int art);
void invalid_input();
int over_cislo(char *cislo);
void nastupovanie(int pocet_nastupujucich);
void vystupovanie(int pocet_pasazierov);


void sem_err() {
    fprintf(stderr, "Error: Chyba vytvorenia semaforov.\n");
    exit(1);
}

void shm_err(int x) {
    fprintf(stderr, "Error: Chyba vytvorenia zdielanej pamate. %d\n", x);
    exit(1);
}

void fork_err() {
    fprintf(stderr, "Error: Nepodarilo sa vytvorit proces\n");
    odstran_semafory();
    odstran_pamat();
    exit(1);
}

void inicializuj_semafory() {
    // semafor: PRISTUP K RADE
    // pouzity na zabezpecenie len jedneho procesu pri zapisovani sa do rady (RID : ENTER)
    if ((pristup_k_rade = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_rade, 1, 1) == -1)
        sem_err();
    
    // semafor: PRISTUP K AUTOBUSU
    // p. k autobusu a k exitu sluzia na spravny chod nastupovania
    if ((pristup_k_autobusu = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_autobusu, 1, 0) == -1)
        sem_err();
    
    //semafor: PRISTUP K EXITU
    if ((pristup_k_exitu = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_exitu, 1, 0) == -1)
        sem_err();

    // semafor: PRISTUP K VYSTUPOVANIU
    // p. k vystupovaniu a k e_vystupovaniu sluzia na spravny chod "vystupovania" (RID : FINISH)
    if ((pristup_k_vystupovaniu = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_vystupovaniu, 1, 0) == -1)
        sem_err();
    
    // semafor: PRISTUP K EXIT_VYSTUPOVANIU
    if ((pristup_k_e_vystupovaniu = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_e_vystupovaniu, 1, 0) == -1)
        sem_err();

    // semafor: PRISTUP K I_LOGU
    // sluzi na spravny pristup k zdielanej pamati I_LOG (v zadani "A")
    if ((pristup_k_i_log = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED)
            sem_err();
    
    if (sem_init(pristup_k_i_log, 1, 1) == -1)
        sem_err();
}

void odstran_semafory() {
    if (sem_destroy(pristup_k_rade) == -1)
        sem_err();
    
    if (sem_destroy(pristup_k_autobusu) == -1)
        sem_err();

    if (sem_destroy(pristup_k_exitu) == -1)
        sem_err();

    if (sem_destroy(pristup_k_vystupovaniu) == -1)
        sem_err();

    if (sem_destroy(pristup_k_e_vystupovaniu) == -1)
        sem_err();

    if (sem_destroy(pristup_k_i_log) == -1)
        sem_err();
}

void inicializuj_pamat() {
    // I_LOG - inkrementuje sa po kazdom zapise do suboru log
    if ((i_log_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1)
        shm_err(3);
    if ((i_log = (int *)shmat(i_log_id, NULL, 0)) == (void *) -1)
        shm_err(4);

    // DLZKA FRONTY - urcuje, kolko riderov stoji na zastavke a caka na bus
    if ((dlzka_fronty_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1)
        shm_err(7);
    if ((dlzka_fronty = (int *)shmat(dlzka_fronty_id, NULL, 0)) == (void *) -1)
        shm_err(8);

    // POCET ODVEZENYCH PASAZIEROV - inkrementuje sa vzdy po odvezeni ridera
    // zavisi od neho, ci bus pojde dalsi jazdu alebo skonci
    if ((pocet_odvezenych_pasazierov_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1)
        shm_err(9);
    if ((pocet_odvezenych_pasazierov = (int *)shmat(pocet_odvezenych_pasazierov_id, NULL, 0)) == (void *) -1)
        shm_err(10);
}

void odstran_pamat() {
    shmdt(i_log);
    shmdt(dlzka_fronty);
    shmdt(pocet_odvezenych_pasazierov);

    if (shmctl(i_log_id, IPC_RMID, NULL) == -1)
        shm_err(12);
    if (shmctl(dlzka_fronty_id, IPC_RMID, NULL) == -1)
        shm_err(14);
    if (shmctl(pocet_odvezenych_pasazierov_id, IPC_RMID, NULL) == -1)
        shm_err(15);
}


void write_log(char* NAME, int I, char* STATE, int CR) {
	sem_wait(pristup_k_i_log);

	if (I == -1 && CR == -1)
		fprintf(f, "%d : %s : %s\n", ++(*i_log), NAME, STATE);
	else if (I == -1)
		fprintf(f, "%d : %s : %s: %d\n", ++(*i_log), NAME, STATE, CR);
	else if (CR == -1)
		fprintf(f, "%d : %s %d : %s\n", ++(*i_log), NAME, I, STATE);
	else 
		fprintf(f, "%d : %s %d : %s: %d\n", ++(*i_log), NAME, I, STATE, CR);

    fflush(NULL);
	sem_post(pristup_k_i_log);
}


int vypocet_kapacity(int dlzka_rady, int kapacita) {
	if (dlzka_rady > kapacita)
		return(kapacita);
	else
		return dlzka_rady;
}

void nastupovanie(int pocet_nastupujucich) {
    int i;
    for (i = 0; i < pocet_nastupujucich; i++) {
            sem_post(pristup_k_autobusu);
            
            sem_wait(pristup_k_exitu);
        }
}

void vystupovanie(int pocet_pasazierov) {
    int i;
    for (i = 0; i < pocet_pasazierov; i++) {
            sem_post(pristup_k_vystupovaniu);
            
            sem_wait(pristup_k_e_vystupovaniu);
        }
}

void jazda_autobusu(int r, int kapacita, int abt) {
    int i;
    int pocet_pasazierov = 0;
 	int pocet_nastupujucich = 0;
    int wait_time = 0;

    //spusteni procesu
    write_log("BUS",-1,"start",-1);

    //cyklus cinnosti autobusu:
    //opakuje sa pokym nie su odvezeni vsetci pasazieri
    while(*pocet_odvezenych_pasazierov != r) {
	    
	    sem_wait(pristup_k_rade);
	    //ARRIVAL
	    write_log("BUS",-1,"arrival",-1);
    	
	    //pocet ludi, ktori mozu nastupit
    	pocet_nastupujucich = vypocet_kapacity(*dlzka_fronty, kapacita);
    	
        //*********************************
        //BOARDING
    	if (pocet_nastupujucich != 0) {
    		write_log("BUS",-1,"start boarding",*dlzka_fronty);
    		
            // cyklus na ukoncenie procesov riderov, ktori "vystupuju"
            if (pocet_pasazierov != 0) 
                vystupovanie(pocet_pasazierov);
            
            // cyklus na nastupovanie pasazierov, ktori stoja v rade
            nastupovanie(pocet_nastupujucich);

            // dlzka fronty zaisti, aby zostal na zastavke pocet ridero, ktori stali vo fronte ale nenastupili
            *dlzka_fronty -= pocet_nastupujucich;
            write_log("BUS",-1,"end boarding",*dlzka_fronty);
        
        } else { 
            // v pripade, ze nikto nenastupuje, tak pasazieri len vystupia
            if (pocet_pasazierov != 0) 
                vystupovanie(pocet_pasazierov);
    	}
        

	    pocet_pasazierov = pocet_nastupujucich;

	    //DEPART
	    write_log("BUS",-1,"depart",-1);

	    sem_post(pristup_k_rade);
        
	    if (abt != 0) {
		    wait_time = rand() % abt;
	        usleep(wait_time);
    	}

        // ukoncenie jazdy a ak este neboli odvezeni vsetci rideri, opakuje sa
        write_log("BUS",-1,"end",-1);
    }

    // vystupovanie poslednych pasazierov po ukoncení jazdy busu
    if (pocet_pasazierov != 0) {
        for (i = 0; i < pocet_pasazierov; i++){
            sem_post(pristup_k_vystupovaniu);

            sem_wait(pristup_k_e_vystupovaniu);
        }
    }
    //ukoncenie procesu bus
    write_log("BUS",-1,"finish",-1);
    exit(0);
}


void cestujuci_pasazier(int moje_id) {
    //zacatie procesu rider
    write_log("RID",moje_id,"start",-1);

    //vstupenie do rady
    sem_wait(pristup_k_rade);

        write_log("RID",moje_id,"enter",++(*dlzka_fronty));

    sem_post(pristup_k_rade);
    

 	//BOARDING
    sem_wait(pristup_k_autobusu);
    	
        write_log("RID",moje_id,"boarding",-1);
        // pasazier po nastupeni navysi "pocet_odvezenych_pasazierov", ktory je pouzity v 
        // podmienke ukoncenia procesu bus
        *pocet_odvezenych_pasazierov += 1;

    sem_post(pristup_k_exitu);


    //UNBOARDING
    sem_wait(pristup_k_vystupovaniu);

        write_log("RID",moje_id,"finish",-1);

    sem_post(pristup_k_e_vystupovaniu);

    exit(0);
}


void generator_pasazierov(int r, int art) {
    int i;
    int wait_time = 0;
    pid_t pid_pasazier[r];
    pid_t wpid;

    for (i=0; i<r; i++) {
        pid_pasazier[i]=fork();
        if (pid_pasazier[i] ==0) {
            // kod pro proces potomka
            cestujuci_pasazier(i+1);
        } else if (pid_pasazier[i] ==-1) {
            // kod pro rodice, nastala chyba pri fork()
            fork_err();
        } else {
            
        }

        // cakanie na cas zadany argumentom ART
        if (art != 0) {
            wait_time = rand() % art;
            usleep(wait_time);
        }
    }

    // cakanie na ukoncenie vsetkych riderov
    for (i = 0; i < r; i++) {
    	while ((wpid = wait(&pid_pasazier[i])) > 0);
    }

    exit(0);
}


void invalid_input() {
    fprintf(stderr, "Error: Invalid argument.\n");
    exit(1);
}


int over_cislo(char *cislo) {
    char *ostatne = {'\0'};
    int vysledok = 0;
    vysledok = (int) strtol(cislo, &ostatne, 10);

    if (*ostatne != '\0'){
        invalid_input();
    }  
    return(vysledok);
}


//********************************
//  ||\\//||   //\\   || ||\\  ||
//  || \/ ||  //==\\  || || \\ ||
//  ||    || //    \\ || ||  \\||
//********************************

int main(int argc, char *argv[]) {
    time_t t;
    pid_t wpid;

    srand((unsigned) time(&t));

    //**********************
    // overenie argumentov
    if (argc != 5)
        invalid_input();

    int r = over_cislo(argv[1]);
    int c = over_cislo(argv[2]);
    //pouzivame uspanie v mikrosekundach, tak vynasobime hodnotu v milisekundach *1000
    int art = (over_cislo(argv[3]))*1000;
    int abt = (over_cislo(argv[4]))*1000;

    //kontrola rozsahu hodnot tiez musi byt vynasobena *1000, pretoze pocitame v mikrosekundach
    if  (!(r > 0) || !(c > 0) || !(art >= 0) || !(art <= 1000000)
    || !(abt >= 0) || !(abt <= 1000000))
    	invalid_input();
    
    // otvorenie suboru na zapis LOGU
    f = fopen("proj2.out", "w");
    if (f == NULL) {
        fprintf(stderr, "Error: Nepodarilo sa otvorit subor\n");
        exit(1);
    }

    //inicializacie:**************
    inicializuj_semafory();
    inicializuj_pamat();
    
        
    //****************************
    // Vytvorenie procesu BUS
	int bus=fork();
    if (bus==0) {
        //proces potomka
        jazda_autobusu(r,c,abt);
    } else if (bus==-1) {
        //nastala chyba pri fork()
        fork_err();
    } else {
        //kod pre rodica
    }

    //**************************
    // Vytvorenie procesu Rideov
    int rider_generator=fork();
    if (rider_generator==0) {
        //proces potomka
        generator_pasazierov(r,art);
    } else if (rider_generator==-1) {
        //nastala chyba pri fork()
        fork_err();
    } else {
        //kod pre rodica
    }
    
    // cakanie na dokoncenie procesov BUS a GENERATOR PROCESOV
    while ((wpid = wait(&bus)) > 0);
    while ((wpid = wait(&rider_generator)) > 0);

    //mazanie:************************
    odstran_semafory();
    odstran_pamat();

    fclose(f);

    return 0;
}