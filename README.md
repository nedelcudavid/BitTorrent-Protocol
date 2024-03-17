Nedelcu Andrei David 334CA <nedelcudavid11@gmail.com>

Tema 3 - APD

Am creat 4 structuri:
- File Swarm e pentru un swarm de informatii cu ce detin clientii, acesta retine
numele fisierului, marimea si lista de clienti cu ce segmente detin fiecare
- ClientContentInfo este reprezentarea informatiilor despre ce detine un client
din swarm, retine rankul si ce segmente detine acesta
- File este structura unui fisier, retine nume, marime si lista de hash-uri
- ThreadArg este structura cu ajutoru careia trimit toate informatiile luate din
input catre firul de executie download al fiecarui client

---Logica client(peer):

#(download & comunicare tracker)

Se extrag toate informatiile de input si se trimit catre tracker, apoi se
asteapta mesajul de confirmare ca tracker-ul a terminat initializarea cu toti
clientii, deci poate incepe faza de download.

Faza de download functionaza iterand prin for fisierele dorite, iar in fiecare
iteratie exista un loop de descarcare a fisierului respectiv care se termina
doar atunci cand fisierul este descarcat complet. Astfel, la inceputul fiecarei
iteratiei se formeaza mesajul de file_swarm_request ("file_swarm_request
<nume_fisier>") si se creaza variabila curent_download_file, unde vom stoca
evolutia descarcarii fisierului curent (aici adaugam hash-uri pe masura ce le
downloadam segmentele).

In loop-ul descarcarii unui fisier de repeta urmatoarele actiuni:
* Se trimite un file_swarm_request catre tracker, apoi primim marimea
fisierului, apoi toate informatiile cerute in file_swarm_request si le stocam
in curent_file_swarm pentru a le folosi la sesiunea curenta de download
(o sesiune descarcat 10 segmente sau cate au mai ramas)

* Descrcarea propriuzisa se face dupa logica eficientei explicata mai jos
(Logica Eficientei): avem variabila eficient_client_idx care incepe de la 1 si
cand se verifica in swarmul fisierului daca clientul <eficient_client_idx>
are hash-ul dorit, daca nu il are eficient_client_idx creste cu 1, daca il are
atunci il descarca, iar dupa descarcare tot il creste pe eficient_client_idx
cu 1, dupa fiecare crestere verificam daca eficient_client_idx = numtasks,
daca da il facem inapoi la 1, astfel se tot cicleaza prin clienti cu scopul de
a descarca de la clienti diferiti intr-un mod echilibrat.

* Urmeaza sa facem update ,se trimite un mesaj de forma "state_update
<numar_segmente_downlodate_in_ultima_sesiune> <nume_fisier>", se asteapta
confirmarea primirii update-ului, apoi se trimite indexul primului hash
descarcat in ultima sesiune, ca sa stie de unde sa scrie trackerul, apoi
hash-urile segmentelor pe rand

* Se verifica daca s-a terminat de descarcat fisierul, daca da se trimite un
update catre tracker ca s-a terminat de descarcat fisierul actual, asteapta
confirmarea primirii si dupa scrie fisierul de output, daca nu s-a terminat de
descarcat, loop-ul de download se ia de la capat, dupa ce se termina iteratia
fisierelor dorite trimitem mesaj catre tracker ca am terminat descarcarea

#(upload)

Firul de upload are un loop care primeste mesaje incotinuu, daca mesajul vinede
de la un client, ii trimite inapoi segementul dorit ("ok"), iar daca vine de la
tracker atunci o sa inchida firul (deoarece singurul tip de mesaj primit de
catre firul de upload este cel de finalizare download pt toate fisierele).
Diferentierea de unde ajunge mesajul (pe ce fir) se face prin tag-uri.

---Logica tracker:

Trackerul are un fel de baza de date numita files_swarms unde dunt toate
swarm-urile a tuturor fisierelor. La initializare primeste de la toti clientii
informatiile din input, astfel construind baza de date. Dupa ce a terminat de
primit de la toti, da mesaje tuturor sa le spuna ca pot incepe download-ul.

Cat timp clients_downloading_remaning e mai mare de 0, trackerul va primi
cereri incontinuu si va actiona in consecint:
* file_swarm_request: verifica baza de date actuala, gaseste fisierul dorit
si ii trimite swarm-ul acestuia senderu-ului

* state_update: actualizeaza baza de date dupa indicatiile primite (cauta
fisierul, la sectiunea clientului sender din swarm si de la indicele specificat
in mesaj si completeaza cu hash-urile a cate segmente a descarcat acel client

* file_download_complete: trimite un mesaj ca a primit instiintarea

* all_files_downloaded_successfully: scade clients_downloading_remaning cu 1

Cand iese din acest loop inseamna ca toti au terminat de descarcat, deci
trimite catre toti clientii mesaj sa inchida firul de upload