
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>

/* codul de eroare returnat de anumite apeluri */
extern int errno;

/* portul de conectare la server*/
int port;

int main(int argc, char *argv[])
{
  int sd;                    // descriptorul de socket
  struct sockaddr_in server; // structura folosita pentru conectare
  char buffer[2000];

  /* exista toate argumentele in linia de comanda? */
  if (argc != 3)
  {
    printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
    return -1;
  }

  /* stabilim portul */
  port = atoi(argv[2]);

  /* cream socketul */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Eroare la socket().\n");
    return errno;
  }

  /* umplem structura folosita pentru realizarea conexiunii cu serverul */
  /* familia socket-ului */
  server.sin_family = AF_INET;
  /* adresa IP a serverului */
  server.sin_addr.s_addr = inet_addr(argv[1]);
  /* portul de conectare */
  server.sin_port = htons(port);

  /* ne conectam la server */
  if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[client]Eroare la connect().\n");
    return errno;
  }
  printf("Bun venit la Smart Parking Assistant,introdu unul din codurile de mai jos pentru a profita de serviciile aplicatiei: \n");
  printf("Parcarea este impartita pe 3 nivele:\n");
  printf("Parter(numerele parcarilor sunt cuprinse intre 1 si 20 ),aici costul este mai ridicat deoarece accesul la masina ta este mai rapid\n");
  printf("Etajul 1 (numerele parcarilor sunt cuprinse intre 20 si 40) costul este putin mai scazut\n");
  printf("Etajul 2( numerele parcarilor sunt cuprinse intre 40 si 60), costul parcarii este cel mai accesibil\n");
  printf("Introdu 1 si zona din parcare pe care o preferi\n");
  printf("(Parter identificat prin 1, etajul 1 identificata prin 2 ,etajul 2 idenficat prin 3)");
  printf("pentru a vedea mai intai locurile libere din zona respectiva daca doresti!\n");
  printf("Introdu 1 pentru a vedea locurile libere de parcare indiferent de zona\n");
  printf("Introdu 1 ,numarul numarului de parcare dorit si numarul de inmatriculare al masinii tale pentru a finaliza parcarea!\n");
  printf("Introdu 2 si numarul de inmatriculare al masinii tale  daca vreti sa-ti scoti masina din parcare!\n");
  printf("Introdu 3 daca vrei sa inchizi aplicatia!\n");
  printf("Introdu 4 pentru a vedea sectiunea de ajutor!\n");
  printf("Introdu 5 si numarul de inmatriculare al masinii tale pentru a vedea in ce zona si in ce loc de parcare se afla!\n");
  while (1)
  {

    /* citirea mesajului */

    fgets(buffer, sizeof(buffer), stdin);
    buffer[strlen(buffer) - 1] = '\0';

    /* trimiterea mesajului la server */

    size_t bufferlen1 = strlen(buffer) + 1;
    size_t bufferh1 = htonl(bufferlen1);
    if (write(sd, &bufferh1, sizeof(size_t)) <= 0)
    {
      perror("[client]Eroare la bufferlen write() spre server.\n");
    }

    if (write(sd, buffer, bufferlen1) <= 0)
    {
      perror("[client]Eroare la write() spre server.\n");
    }
    char *token;
    char *delimitator = " ";
    int numar;
    char *saveptr;
    token = strtok_r(buffer, delimitator, &saveptr);
    if (token != NULL)
    {
      numar = atoi(token);
    }

    if (numar == 3)
    {
      break;
    }

    /* citirea raspunsului dat de server
       (apel blocant pina cind serverul raspunde) */
    size_t bufferlen;
    size_t bufferh;
    if (read(sd, &bufferh, sizeof(size_t)) <= 0)
    {

      perror("[client]Eroare la read len() dinspre server.\n");
      return errno;
    }

    bufferlen = ntohl(bufferh);
    /* afisam mesajul primit */
    if (read(sd, buffer, bufferlen) <= 0)
    {

      perror("[client]Eroare la read() dinspre server.\n");
      return errno;
    }

    printf("%s\n", buffer);

    /* inchidem conexiunea, am terminat */
  }
  close(sd);
}
