#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 35
#define MAX_CHUNKS 100
#define VALIDATION_MSG_SIZE 10
#define PEER_DOWNLOAD_MSG_SIZE 40
#define MAX_OUTPUT_FILENAME 30
#define MAX_NR_CHARS 5
#define MAX_DOWNLOADS_BEFORE_UPDATE 10

//Structura unui file swarm
struct FileSwarm {
    char file_name[MAX_FILENAME];
    int file_size;
    struct ClientContentInfo* seed_peer_clients;
};

//Structura de informatii continut-client (pt un fisier anume)
struct ClientContentInfo {
    int client_rank;
    char owned_segments[MAX_CHUNKS][HASH_SIZE];
};

//Strructura file
struct File {
    char name[MAX_FILENAME];
    int size;
    char chunks_hashes[MAX_CHUNKS][HASH_SIZE];
};

//Argument trimis catre thread-ul de download
struct ThreadArg {
    int rank;
    int numtasks;
    int held_files_conut;
    struct File held_files[MAX_FILES];
    int wanted_files_count;
    char wanted_files_names[MAX_FILES][MAX_FILENAME];
};

void *download_thread_func(void *arg)
{
    struct ThreadArg thr_arg = *(struct ThreadArg*) arg;
    char validation_msg[VALIDATION_MSG_SIZE];
    MPI_Status status;

    //Se trimit catre tracker fisierele pentru care clientul este seed initial
    MPI_Send(&thr_arg.held_files_conut, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
    for (int i = 0; i < thr_arg.held_files_conut; i++) {
        MPI_Send(&thr_arg.held_files[i].name, MAX_FILENAME, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        MPI_Send(&thr_arg.held_files[i].size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        for (int j = 0; j < thr_arg.held_files[i].size; j++) {
            MPI_Send(&thr_arg.held_files[i].chunks_hashes[j], HASH_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        }
    }

    //Se primeste de la tracker ca a terminat initializarea, inseamna ca putem incepe faza de download
    MPI_Recv(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

    //Download phase
    for (int i = 0; i < thr_arg.wanted_files_count; i++) {

        //Formarea mesajului de file_swarm_request
        char file_swarm_request[PEER_DOWNLOAD_MSG_SIZE];
        strcpy(file_swarm_request, "file_swarm_request ");
        strcat(file_swarm_request, thr_arg.wanted_files_names[i]);

        //Crearea si alocarea memoriei  pt swarm-ului de clienti cu hash-urile afernte pentru fisierul curent
        struct File curent_downloading_file;
        strcpy(curent_downloading_file.name, thr_arg.wanted_files_names[i]);
        for (int j = 0; j < MAX_CHUNKS; j++) {
            strcpy(curent_downloading_file.chunks_hashes[j], "EMPTY");
        }
        struct ClientContentInfo* curent_file_swarm  = (struct ClientContentInfo*)malloc((thr_arg.numtasks) * sizeof(struct ClientContentInfo));
        
        //Informatii utile download curent
        int eficient_client_idx = 1;        
        int numof_downloaded_chunks = 0;
        int curent_file_download_complete = 0;

        //Incepe download-ul pt fisierul curent
        while (curent_file_download_complete == 0) {

            //Clientul trimite un "file swarm request" trackerului pt a vedea lista cu clientii si
            //hash-urile pe care le detin din momentul actual
            MPI_Send(&file_swarm_request, PEER_DOWNLOAD_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

            //Primeste marimea acestuia
            MPI_Recv(&curent_downloading_file.size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);

            //Dupa care primeste toate informatiile despre fisierul pe care vrea sa il descarce
            //si salveaza swarm-ul actual al acestuia in "curent_file_swarm"
            for (int j = 1; j < thr_arg.numtasks; j++) {
                int peer_rank;
                MPI_Recv(&peer_rank, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
                curent_file_swarm[j].client_rank = peer_rank;
                if (curent_file_swarm[j].client_rank != 0) {
                    for (int k = 0; k < curent_downloading_file.size; k++) {
                        char hash[HASH_SIZE];
                        MPI_Recv(&hash, HASH_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);
                        strcpy(curent_file_swarm[j].owned_segments[k], hash);
                    }
                }
            }
            
            //Incepe urmatoarea sesiune de download (10 segmente de descarcat sau cate raman)
            int sesion_downloaded_chunks = 0;
            while (sesion_downloaded_chunks < MAX_DOWNLOADS_BEFORE_UPDATE && numof_downloaded_chunks < curent_downloading_file.size) {

                //Segmentele sunt descarcate in ordine, iar clientul verifica pe rand:
                //daca rankul clientului din swarmul curent este dif de 0 (inseamna ca acel client detine parti din fisier)
                if (curent_file_swarm[eficient_client_idx].client_rank != 0 &&
                    //daca rankul clientului din swarmul curent este diferit de el insusi
                    curent_file_swarm[eficient_client_idx].client_rank != thr_arg.rank &&
                    //daca clientul gasit are hash-ul de care are nevoie (slotul nu este gol)
                    strcmp(curent_file_swarm[eficient_client_idx].owned_segments[numof_downloaded_chunks], "EMPTY") != 0) {

                    //daca toate conditiile sunt confirmate trimite cerer catre clientul respectiv
                    MPI_Send(&curent_file_swarm[eficient_client_idx].owned_segments[numof_downloaded_chunks], HASH_SIZE, MPI_CHAR, eficient_client_idx, 1, MPI_COMM_WORLD);

                    //Dupa ce primeste inapoi segmentul de fisier dorit salveaza hash-ul pt fisier in "curent_downloading_file"
                    //iar apoi trece la urmatorul index de segment de care are nevoie si incepe verificarea de indexul
                    //urmatorului client (se tine cont prin "eficient_client_idx"), pentru a varia clientul de la care se cere
                    MPI_Recv(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, eficient_client_idx, 2, MPI_COMM_WORLD, &status);
                    if (strcmp(validation_msg, "OK") == 0) {
                        strcpy(curent_downloading_file.chunks_hashes[numof_downloaded_chunks], curent_file_swarm[eficient_client_idx].owned_segments[numof_downloaded_chunks]);
                        numof_downloaded_chunks++;
                        sesion_downloaded_chunks++;
                        eficient_client_idx++;
                    }
                    if (eficient_client_idx == thr_arg.numtasks) {
                        eficient_client_idx = 1;
                    }

                } else {

                    //Daca conditiile nu se indeplinesc se verifica la urmatorul client (daca se ajunge la thr_arg.numtasks
                    // se reia de la 1, astfel ciclandu-se constant prin clienti si variandu-i pe in mod egal)
                    eficient_client_idx++;
                    if (eficient_client_idx == thr_arg.numtasks) {
                        eficient_client_idx = 1;
                    }
                }
            }      

            //Update stare descarcare fisier:

            //Se creaza structura mesajului de state_update
            char update_msg[PEER_DOWNLOAD_MSG_SIZE];
            strcpy(update_msg, "state_update ");
            char numof_dowloaded_chunks_string[MAX_NR_CHARS];
            sprintf(numof_dowloaded_chunks_string, "%d", sesion_downloaded_chunks);
            strcat(update_msg, numof_dowloaded_chunks_string);
            strcat(update_msg, " ");
            strcat(update_msg, curent_downloading_file.name);

            //Se trimite mesajul catre tracker si se asteapta confiramrea primirii
            MPI_Send(&update_msg, PEER_DOWNLOAD_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            MPI_Recv(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

            //Se trimite apoi numarul de segmente downoadate ce trebuie actualizate de catre tracker
            char update_idx[MAX_NR_CHARS];
            sprintf(update_idx, "%d", numof_downloaded_chunks - sesion_downloaded_chunks);
            MPI_Send(&update_idx, MAX_NR_CHARS, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

            //Apoi se trimit hash-urile segmentelor proaspat downloadate
            for (int j = numof_downloaded_chunks - sesion_downloaded_chunks; j < numof_downloaded_chunks; j++) {
                MPI_Send(&curent_downloading_file.chunks_hashes[j], HASH_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            }

            //Se verifica daca s-a terminat de downloadat fisierul curent
            if (numof_downloaded_chunks == curent_downloading_file.size) {

                //Se marcheaza finalul descarcarii si se anunta trackerul
                curent_file_download_complete = 1;
                strcpy(update_msg, "file_download_complete");
                MPI_Send(&update_msg, PEER_DOWNLOAD_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

                //Primeste mesaj ca trackerul a luat cunostiinta, acum poate incepe scrierea output-ului pt fisier
                MPI_Recv(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

                //Construire nume fisier output
                char output_file[MAX_OUTPUT_FILENAME];
                strcpy(output_file, "client");
                char client_idx_string[MAX_NR_CHARS];
                sprintf(client_idx_string, "%d", thr_arg.rank);
                strcat(output_file, client_idx_string);
                strcat(output_file, "_");
                strcat(output_file, curent_downloading_file.name);

                //Scriere hash-uri in fisierul de output
                FILE *out_file = fopen(output_file, "w");
                for (int j = 0; j < curent_downloading_file.size; j++) {
                    fprintf(out_file, "%s\n", curent_downloading_file.chunks_hashes[j]);
                }

                fclose(out_file);
            }
        }

        free(curent_file_swarm);
    }

    //Dupa ce s-a trecut si s-au downloadat toate fisierele dorite se anunta tracker-ul
    char last_update[PEER_DOWNLOAD_MSG_SIZE];
    strcpy(last_update, "all_files_downloaded_successfully");
    MPI_Send(&last_update, PEER_DOWNLOAD_MSG_SIZE, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    
    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    MPI_Status status;
    
    //Clientul tine deschis firul de upload pana toti termina de download
    int clients_downloading_remaning = 1;
    while (clients_downloading_remaning != 0) {

        //La primirea unui hash de la un client, acesta trimite inapoi sender-ului "segmentul virtual" adica "OK"
        //daca vine de la un alt client, iar daca vine mesajul de la tracker acesta inchide executia
        char message_recv[HASH_SIZE];
        MPI_Recv(&message_recv, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;
        if (sender_rank == 0) {
            clients_downloading_remaning = 0;
        } else {
            char uploaded_content[VALIDATION_MSG_SIZE];
            strcpy(uploaded_content, "OK");
            MPI_Send(&uploaded_content, VALIDATION_MSG_SIZE, MPI_CHAR, sender_rank, 2, MPI_COMM_WORLD);
        }
    }

    return NULL;
}

void tracker(int numtasks, int rank) {
    MPI_Status status;
    char validation_msg[VALIDATION_MSG_SIZE];

    //"files_swarms" o sa fie baza de date a trackerului, "files_swarms_idx", indexul fisierului cu care lucram
    struct FileSwarm files_swarms[MAX_FILES];
    int files_swarms_size = 0;
    int files_swarms_idx = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        files_swarms[i].seed_peer_clients = (struct ClientContentInfo*)malloc(numtasks * sizeof(struct ClientContentInfo));
        //Initializam file.size-urile cu 0 = nu avem fisierul respectiv
        files_swarms[i].file_size = 0;

        for (int j = 0; j < numtasks; j++) {
            //Initializam la fiecare client din fiecare fisier, rankul cu 0 = acel client nu detine
            //nici macar 1 segment din acel fisier
            files_swarms[i].seed_peer_clients[j].client_rank = 0;
            for (int k = 0; k < MAX_CHUNKS; k++) {
                //Initializam toate hash-urile cu "EMPTY" = clientul nu detine acel hash
                strcpy(files_swarms[i].seed_peer_clients[j].owned_segments[k], "EMPTY");
            }
        }
    }

    //Primim informatiile de input de la fiecare fisier si le stocam in baza de date (files_swarms)
    for (int i = 1; i < numtasks; i++) {
        //Primim nr de fisiere dorite de client
        int owned_files_conut;
        MPI_Recv(&owned_files_conut, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

        //Pt fiecare fisier primim:
        for (int j = 0; j < owned_files_conut; j++) {

            //Numele acestuia, verificam daca e un fisier nou sau nu sa stim daca
            //stocam noile info in swarm-ul unui fisier despre care stim deja sau creem un swarm nou
            char curent_file_name[MAX_FILENAME];
            MPI_Recv(&curent_file_name, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
            int file_already_exists = 0;
            for (int k = 0; k < files_swarms_size; k++) {
                if ((strcmp(curent_file_name, files_swarms[k].file_name) == 0)) {
                    file_already_exists = 1;
                    files_swarms_idx = k;
                }
            }
            if (file_already_exists == 0) {
                strcpy(files_swarms[files_swarms_size].file_name, curent_file_name);
                files_swarms_idx = files_swarms_size;
                files_swarms_size++;
            } 
            //Marcam ca informatiile au venit de la un anume client si ca el le detine
            //schimband 0 cu rankul acestuia
            files_swarms[files_swarms_idx].seed_peer_clients[i].client_rank = i;

            //Primim marimea si hash-urile fisierului in discutie se la sender si le stocam la clientul sender in swarm
            MPI_Recv(&files_swarms[files_swarms_idx].file_size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
            for (int k = 0; k < files_swarms[files_swarms_idx].file_size; k++) {
                char current_hash[HASH_SIZE];
                MPI_Recv(&current_hash, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
                strcpy(files_swarms[files_swarms_idx].seed_peer_clients[i].owned_segments[k], current_hash);
            }
        }
    }

    for (int i = 1; i < numtasks; i++) {
        //Trimitem mesaj catre fiecare client ca am terminat initializarea si ca poate incepe download-ul
        strcpy(validation_msg, "init_done");
        MPI_Send(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }


    //Cat timp avem clienti ce inca downloadeaza primim mesaje cu cereri
    int clients_downloading_remaning = numtasks - 1;
    while (clients_downloading_remaning > 0) {

        //Primim mesajul, identificam senderul si spargem mesajul in bucati pt a-l rezolva
        char peer_message[PEER_DOWNLOAD_MSG_SIZE];
        MPI_Recv(&peer_message, PEER_DOWNLOAD_MSG_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;
        char delimiter = ' ';
        char *token = strtok(peer_message, &delimiter);

        //Daca este o cerere de swarm:
        if (strcmp(token, "file_swarm_request") == 0) {

            //Urmatorul cuvant din cerere este numele fisierului cerut,
            // il identificam pt a stabili indexul fisierului din swarm
            token = strtok(NULL, &delimiter);
            for (int i = 0; i < files_swarms_size; i++) {
                if ((strcmp(token, files_swarms[i].file_name) == 0)) {
                    files_swarms_idx = i;
                    break;
                }
            }

            //Trimitem size-ul fisierului catre sender
            MPI_Send(&files_swarms[files_swarms_idx].file_size, 1, MPI_INT, sender_rank, 0, MPI_COMM_WORLD);

            //Trimitem swarm-ul fisierului, astfel il instiintam de ce clienti
            // detin fisierul si ce segmente au in momentul asta
            for (int i = 1; i < numtasks; i++) {
                MPI_Send(&files_swarms[files_swarms_idx].seed_peer_clients[i].client_rank, 1, MPI_INT, sender_rank, 0, MPI_COMM_WORLD);
                if (files_swarms[files_swarms_idx].seed_peer_clients[i].client_rank != 0) {
                    for (int j = 0; j < files_swarms[files_swarms_idx].file_size; j++) {
                        MPI_Send(&files_swarms[files_swarms_idx].seed_peer_clients[i].owned_segments[j], HASH_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);
                    }
                }
            }

        //Daca este un update de stare:
        } else if (strcmp(token, "state_update") == 0) {

            //Trimitem mesaj de validare ca am primit cererea de update
            strcpy(validation_msg, "ok");
            MPI_Send(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

            //Al doilea cuvant din cerere este numerul de segmente descarcate
            token = strtok(NULL, &delimiter);
            int numof_updated_chunks = atoi(token);

            //Al treilea cuvant din cerere este numele fisierului pe care il descarca
            token = strtok(NULL, &delimiter);
            char updated_file_name[MAX_FILENAME];
            strcpy(updated_file_name, token);

            //Se identifica fisierul si se marcheaza ca senderul detine parti din fisier in baza de date 
            for (int j = 0; j < files_swarms_size; j++) {
                if ((strcmp(updated_file_name, files_swarms[j].file_name) == 0)) {
                    files_swarms_idx = j;
                    if (files_swarms[files_swarms_idx].seed_peer_clients[sender_rank].client_rank == 0) {
                        files_swarms[files_swarms_idx].seed_peer_clients[sender_rank].client_rank = sender_rank;
                    }
                }
            }

            //Se primeste indexul primului hash descarcat de sender in sesiunea despre care da state_update
            int first_update_idx = 0;
            char first_update_idx_str[MAX_NR_CHARS];
    	    MPI_Recv(&first_update_idx_str, MAX_NR_CHARS, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);
            first_update_idx = atoi(first_update_idx_str);

            //Se actualizeaza hash-urile detinute de clienul sender pt fisierul respectiv
            for (int j = first_update_idx; j < first_update_idx + numof_updated_chunks; j++) {
                char hash[HASH_SIZE];
                MPI_Recv(&hash, HASH_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);
                strcpy(files_swarms[files_swarms_idx].seed_peer_clients[sender_rank].owned_segments[j], hash);
            }

        //Daca este o instiintare ca un client a terminat de downloadat un fisier
        } else if (strcmp(token, "file_download_complete") == 0) {
            //Trackerul trimite senderul permisiunea sa scrie outputul si sa treaca la urmatorul fisier
            MPI_Send(&validation_msg, VALIDATION_MSG_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

        //Daca este o instiintare ca un client a terminat toate fisierele de download marcheaza acest lucru
        } else if (strcmp(token, "all_files_downloaded_successfully") == 0) {
            clients_downloading_remaning--;
        } else {
            printf("TOKEN INVALID AJUNS LA TRACKER: %s\n", token);
        }
    }

    //Atunci cand toti clientii au terminat de downloadat trimite catre 
    // toti clientii mesaje sa inchida firele de upload
    for (int i = 1; i < numtasks; i++) {
        char end_upload_hash[HASH_SIZE];
        strcpy(end_upload_hash, "&THANK_YOU_FOR_YOUR_SERVICE&");
        MPI_Send(&end_upload_hash, HASH_SIZE, MPI_CHAR, i, 1, MPI_COMM_WORLD);
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        free(files_swarms[i].seed_peer_clients);
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    //Deschide fisier input destinat lui
    FILE *input_file;
    char input_file_idx = rank + '0';
    char input_file_name[MAX_FILENAME] = "in";
    input_file_name[2] = input_file_idx;
    input_file_name[3] = '\0';
    strcat(input_file_name, ".txt");
    input_file = fopen(input_file_name, "r");

    //Stocheaza toate informatiie in input pt a le trimite firului de download
    struct ThreadArg thr_arg;
    thr_arg.rank = rank;
    thr_arg.numtasks = numtasks;
    fscanf(input_file, "%d", &thr_arg.held_files_conut);
    for (int i = 0; i < thr_arg.held_files_conut; i++) {
        fscanf(input_file, "%s", thr_arg.held_files[i].name);
        fscanf(input_file, "%d", &thr_arg.held_files[i].size);
        for (int j = 0; j < thr_arg.held_files[i].size; j++) {
            fscanf(input_file, "%s", thr_arg.held_files[i].chunks_hashes[j]);
        }
    }
    fscanf(input_file, "%d", &thr_arg.wanted_files_count);
    for (int i = 0; i < thr_arg.wanted_files_count; i++) {
        fscanf(input_file, "%s", thr_arg.wanted_files_names[i]);
    }
    

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &thr_arg);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }

    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
