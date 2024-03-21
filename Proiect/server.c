#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <ctype.h>

/* portul folosit */
#define PORT 2908

/* codul de eroare returnat de anumite apeluri */
extern int errno;

typedef struct thData
{
  int idThread; // id-ul thread-ului tinut in evidenta de acest program
  int cl;       // descriptorul intors de accept
} thData;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
void raspunde(void *);
sqlite3 *db;
char *errMsg = 0;

int callback(void *foundResultPtr, int argc, char **argv, char **azColName)
{
  int *foundResult = (int *)foundResultPtr;

  *foundResult = 1;
  return 0;
}

void updateCarInParking(int numar2, const char *numarInmatriculare)
{
  char sql[100];
  sprintf(sql, "UPDATE Parcare SET Numar_Inmatriculare = '%s' WHERE Numar_Parcare = %d;", numarInmatriculare, numar2);

  int rc = sqlite3_exec(db, sql, 0, 0, 0);

  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la actualizarea masinii in parcare: %s\n", sqlite3_errmsg(db));
    exit(0);
  }
}

bool suntIndicativeJudeț(const char *nr_inmat)
{
  
  const char *indicativJud[] = {
      "AB", "AR", "AG", "BC", "BH", "BN", "BT","BR", "BV", "B", "BZ", "CS", "CL", "CJ", "CT", "CV", "DB",
      "DJ", "GL", "GR", "GJ", "HR", "HD", "IL", "IS", "IF", "MM", "MH", "MS", "NT", "OT", "PH", "SM",
      "SJ", "SB", "SV", "TR", "TM", "TL", "VS", "VL"};

  
  for (size_t i = 0; i < sizeof(indicativJud) / sizeof(indicativJud[0]); ++i)
  {
    size_t lungimeIndicativ = strlen(indicativJud[i]);

    if (strncmp(nr_inmat, indicativJud[i], lungimeIndicativ) == 0)
    {
      
      return true;
    }
  }

  
  return false;
}

bool verifica_numere(const char *numarInmatriculare)
{
  // Verificăm dacă județul este "B" (București)
  if (strncmp(numarInmatriculare, "B", 1) == 0 && isdigit(numarInmatriculare[1]))
  {
    // Dacă este București, numele de după județ poate avea 2 sau 3 cifre
    return ((isdigit(numarInmatriculare[1]) && isdigit(numarInmatriculare[2]) && isdigit(numarInmatriculare[3])) || ((isdigit(numarInmatriculare[1])) && isdigit(numarInmatriculare[2])));
  }
  else
  {
    // Pentru orice alt județ, numele de după județ trebuie să aibă doar 2 cifre
    return (isdigit(numarInmatriculare[2]) && isdigit(numarInmatriculare[3]));
  }
}

bool verifica_litere(const char *numarInmatriculare)
{
  if (strncmp(numarInmatriculare, "B", 1) == 0 && isdigit(numarInmatriculare[1]))
  {
    // Dacă este București, verificăm 3 litere după cifre
    return ((isalpha(numarInmatriculare[4]) && isupper(numarInmatriculare[4]) &&
             isalpha(numarInmatriculare[5]) && isupper(numarInmatriculare[5]) &&
             isalpha(numarInmatriculare[6]) && isupper(numarInmatriculare[6]) && numarInmatriculare[7] == '\0') ||
            (isalpha(numarInmatriculare[3]) && isupper(numarInmatriculare[3]) &&
             isalpha(numarInmatriculare[4]) && isupper(numarInmatriculare[4]) &&
             isalpha(numarInmatriculare[5]) && isupper(numarInmatriculare[5]) && numarInmatriculare[6] == '\0'));
  }
  else
  {
    // Pentru orice alt județ, verificăm 3 litere după cifre
    return (isalpha(numarInmatriculare[4]) && isupper(numarInmatriculare[4]) &&
            isalpha(numarInmatriculare[5]) && isupper(numarInmatriculare[5]) &&
            isalpha(numarInmatriculare[6]) && isupper(numarInmatriculare[6]) && numarInmatriculare[7] == '\0');
  }
}

int verificaDuplicat(sqlite3 *db, const char *numarInmatriculare)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM Parcare WHERE Numar_Inmatriculare = ?;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
    return -1; 
  }

  // Leagă valoarea la parametrul interogării
  sqlite3_bind_text(stmt, 1, numarInmatriculare, -1, SQLITE_STATIC);

  // Execută interogarea
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW)
  {
    // Obține rezultatul (numărul de rânduri găsite)
    int numarRanduri = sqlite3_column_int(stmt, 0);

    // Finalizează interogarea
    sqlite3_finalize(stmt);

    // Returnează 1 dacă există deja, altfel 0
    return (numarRanduri > 0) ? 1 : 0;
  }
  else
  {
    fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1; 
  }
}

int verificareParcarePlina(sqlite3 *db)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM Parcare WHERE Numar_Inmatriculare IS NOT NULL;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
    return -1; 
  }

  // Execută interogarea
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW)
  {
    // Obține rezultatul (numărul total de înregistrări cu Numar_Inmatriculare ne-nul)
    int numarInregistrari = sqlite3_column_int(stmt, 0);

    // Finalizează interogarea
    sqlite3_finalize(stmt);

    // Returnează 1 dacă toate locurile sunt ocupate, altfel 0
    return (numarInregistrari == 60) ? 1 : 0;
  }
  else
  {
    fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1; 
  }
}
int verificareParcarePlinaParter(sqlite3 *db)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM Parcare WHERE Numar_Inmatriculare IS NOT NULL AND Numar_Parcare>=1 AND Numar_Parcare<=20;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
    return -1; 
  }

  // Execută interogarea
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW)
  {
    // Obține rezultatul (numărul total de înregistrări cu Numar_Inmatriculare ne-nul)
    int numarInregistrari = sqlite3_column_int(stmt, 0);

    // Finalizează interogarea
    sqlite3_finalize(stmt);

    // Returnează 1 dacă toate locurile sunt ocupate, altfel 0
    return (numarInregistrari == 20) ? 1 : 0;
  }
  else
  {
    fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1; 
  }
}

int verificareParcarePlinaEtajul1(sqlite3 *db)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM Parcare WHERE Numar_Inmatriculare IS NOT NULL AND Numar_Parcare>=21 AND Numar_Parcare<=40;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
    return -1; 
  }

  // Execută interogarea
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW)
  {
    // Obține rezultatul (numărul total de înregistrări cu Numar_Inmatriculare ne-nul)
    int numarInregistrari = sqlite3_column_int(stmt, 0);

    // Finalizează interogarea
    sqlite3_finalize(stmt);

    // Returnează 1 dacă toate locurile sunt ocupate, altfel 0
    return (numarInregistrari == 20) ? 1 : 0;
  }
  else
  {
    fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1; 
  }
}
int verificareParcarePlinaEtajul2(sqlite3 *db)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM Parcare WHERE Numar_Inmatriculare IS NOT NULL AND Numar_Parcare>=41 AND Numar_Parcare<=60;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  // Execută interogarea
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW)
  {
    // Obține rezultatul (numărul total de înregistrări cu Numar_Inmatriculare ne-nul)
    int numarInregistrari = sqlite3_column_int(stmt, 0);

    // Finalizează interogarea
    sqlite3_finalize(stmt);

    // Returnează 1 dacă toate locurile sunt ocupate, altfel 0
    return (numarInregistrari == 20) ? 1 : 0;
  }
  else
  {
    fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1; 
  }
}

int main()
{
  struct sockaddr_in server; // structura folosita de server
  struct sockaddr_in from;  // informatii despre client ,ip,port
  int sd; // descriptorul de socket
  int pid;
  pthread_t th[100]; // Identificatorii thread-urilor care se vor crea
  int i = 0;

  int result;
  result = sqlite3_open("parcare.db", &db);
  if (result)
  {
    fprintf(stderr, "Eroare la deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
    exit(1);
  }

  /* crearea unui socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("[server]Eroare la socket().\n");
    return errno;
  }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  /* pregatirea structurilor de date */
  bzero(&server, sizeof(server));
  bzero(&from, sizeof(from));

  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
  server.sin_family = AF_INET;
  /* acceptam orice adresa */
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  /* utilizam un port utilizator */
  server.sin_port = htons(PORT);

  /* atasam socketul */
  if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
  {
    perror("[server]Eroare la bind().\n");
    return errno;
  }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen(sd, 5) == -1)
  {
    perror("[server]Eroare la listen().\n");
    return errno;
  }
  /* servim in mod concurent clientii...folosind thread-uri */
  while (1)
  {
    int client;
    thData *td; // parametru functia executata de thread
    int length = sizeof(from);

    printf("[server]Asteptam la portul %d...\n", PORT);
    fflush(stdout);

    // client= malloc(sizeof(int));
    /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
    if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
    {
      perror("[server]Eroare la accept().\n");
      continue;
    }

    /* s-a realizat conexiunea, se astepta mesajul */

    // int idThread; //id-ul threadului
    // int cl; //descriptorul intors de accept

    td = (struct thData *)malloc(sizeof(struct thData));
    td->idThread = i++;
    td->cl = client;

    pthread_create(&th[i], NULL, &treat, td);

  } // while
};
static void *treat(void *arg)
{
  struct thData tdL;
  tdL = *((struct thData *)arg);
  printf("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
  fflush(stdout);
  pthread_detach(pthread_self());
  raspunde((struct thData *)arg);
  /* am terminat cu acest client, inchidem conexiunea */
  close((intptr_t)arg);
  return (NULL);
};

void raspunde(void *arg)
{

  char buffer[2000] = {0};

  struct thData tdL;
  tdL = *((struct thData *)arg);

  while (1)
  {
    size_t bufferlen1 ;
    size_t bufferh1;
    if (read(tdL.cl, &bufferh1, sizeof(size_t)) <= 0)
    {

      perror("[client]Eroare la read len() dinspre client.\n");
    }

    bufferlen1 = ntohl(bufferh1);

    if (read(tdL.cl, buffer, bufferlen1) <= 0)
    {

      perror("[client]Eroare la read() dinspre client.\n");
    }

    printf("[Thread %d]Mesajul a fost receptionat\n", tdL.idThread);

    char *token;
    char *delimitator = " ";
    int numar;
    int numar2 = 0;
    char numarInmatriculare[100] = "";
    char *saveptr;

    token = strtok_r(buffer, delimitator, &saveptr);
    if (token != NULL)
    {
      numar = atoi(token);
      if (numar == 1)
      {

        token = strtok_r(NULL, delimitator, &saveptr);
        if (token != NULL)
        {
          numar2 = atoi(token);

          token = strtok_r(NULL, delimitator, &saveptr);
          if (token != NULL)
          {
            strncpy(numarInmatriculare, token, sizeof(numarInmatriculare) - 1);
            numarInmatriculare[sizeof(numarInmatriculare) - 1] = '\0';
          }
        }
      }
      else
      {
        token = strtok_r(NULL, delimitator, &saveptr);
        if (token != NULL)
        {
          strncpy(numarInmatriculare, token, sizeof(numarInmatriculare) - 1);
          numarInmatriculare[sizeof(numarInmatriculare) - 1] = '\0';
        }
      }
    }

    if (numar == 1 && numar2 == 0 && strcmp(numarInmatriculare, "") == 0)
    {
      if (verificareParcarePlina(db) == 0)
      {

        memset(buffer, 0, sizeof(buffer));
        strcat(buffer, "Locurile de parcare libere sunt: ");

        // Iterează prin baza de date pentru a găsi locurile libere

        int foundResult = 0;
        for (int i = 1; i <= 60; i++)
        {

          char sql[500];
          sprintf(sql, "SELECT * FROM Parcare WHERE Numar_Parcare=%d AND Numar_Inmatriculare IS NULL;", i);
          foundResult = 0;

          int rc = sqlite3_exec(db, sql, callback, &foundResult, 0);

          if (rc == SQLITE_OK)
          {
            // Locul de parcare este liber
            if (foundResult)
            {
              char numarStr[10];
              sprintf(numarStr, "%d", i);
              strcat(buffer, numarStr);
              strcat(buffer, " ");
            }
          }
        }
        strcat(buffer, "\nAlege unul din locurile disponibile !\n");
      }
      else
      {
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "Ne pare rau dar toate locurile de parcare sunt ocupate,poti reveni mai tarziu!\n");
      }
    }
    else if (numar == 1 && numar2 != 0 && strcmp(numarInmatriculare, "") == 0)
    {
      if (numar2 == 1)
      {
        if (verificareParcarePlinaParter(db) == 0)
        {
          memset(buffer, 0, sizeof(buffer));
          strcat(buffer, "Locurile de parcare libere de la Parter sunt: ");

          // Iterează prin baza de date pentru a găsi locurile libere

          int foundResult = 0;
          for (int i = 1; i <= 20; i++)
          {

            char sql[500];
            sprintf(sql, "SELECT * FROM Parcare WHERE Numar_Parcare=%d AND Numar_Inmatriculare IS NULL;", i);
            foundResult = 0;

            int rc = sqlite3_exec(db, sql, callback, &foundResult, 0);

            if (rc == SQLITE_OK)
            {
              // Locul de parcare este liber
              if (foundResult)
              {
                char numarStr[10];
                sprintf(numarStr, "%d", i);
                strcat(buffer, numarStr);
                strcat(buffer, " ");
              }
            }
          }
          strcat(buffer, "\nAlege unul din locurile disponibile de la Parter !\n");
        }
        else
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Ne pare rau dar toate locurile de parcare de la Parter sunt ocupate, te poti orienta pe alt nivel al parcarii sau poti reveni mai tarziu!\n");
        }
      }
      else if (numar2 == 2)
      {
        if (verificareParcarePlinaEtajul1(db) == 0)
        {

          memset(buffer, 0, sizeof(buffer));
          strcat(buffer, "Locurile de parcare libere de la Etajul 1 sunt: ");

          // Iterează prin baza de date pentru a găsi locurile libere

          int foundResult = 0;
          for (int i = 21; i <= 40; i++)
          {

            char sql[500];
            sprintf(sql, "SELECT * FROM Parcare WHERE Numar_Parcare=%d AND Numar_Inmatriculare IS NULL;", i);
            foundResult = 0;

            int rc = sqlite3_exec(db, sql, callback, &foundResult, 0);

            if (rc == SQLITE_OK)
            {
              // Locul de parcare este liber
              if (foundResult)
              {
                char numarStr[10];
                sprintf(numarStr, "%d", i);
                strcat(buffer, numarStr);
                strcat(buffer, " ");
              }
            }
          }
          strcat(buffer, "\nAlege unul din locurile disponibile de la Etajul 1!\n");
        }
        else
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Ne pare rau dar toate locurile de parcare de la Etajul 1 sunt ocupate,te poti orienta pe alt nivel al parcarii sau poti reveni mai tarziu!\n");
        }
      }
      else if (numar2 == 3)
      {
        if (verificareParcarePlinaEtajul2(db) == 0)
        {
          memset(buffer, 0, sizeof(buffer));
          strcat(buffer, "Locurile de parcare libere de la Etajul 2 sunt: ");

          // Iterează prin baza de date pentru a găsi locurile libere

          int foundResult = 0;
          for (int i = 41; i <= 60; i++)
          {

            char sql[500];
            sprintf(sql, "SELECT * FROM Parcare WHERE Numar_Parcare=%d AND Numar_Inmatriculare IS NULL;", i);
            foundResult = 0;

            int rc = sqlite3_exec(db, sql, callback, &foundResult, 0);

            if (rc == SQLITE_OK)
            {
              // Locul de parcare este liber
              if (foundResult)
              {
                char numarStr[10];
                sprintf(numarStr, "%d", i);
                strcat(buffer, numarStr);
                strcat(buffer, " ");
              }
            }
          }
          strcat(buffer, "\nAlege unul din locurile disponibile de la Etajul 2!\n");
        }
        else
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Ne pare rau dar toate locurile de parfcare de la Etajul 2 sunt ocupate,te poti orienta pe alt nivel al parcarii sau poti reveni mai tarziu!\n");
        }
      }
      else
      {
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "Nu ai introdus un numar valid pentru o zona existenta din parcare!\n");
      }
    }
    else if (numar == 1 && numar2 != 0 && strcmp(numarInmatriculare, "") != 0)
    {
      if (verificareParcarePlina(db) == 0)
      {
        if (suntIndicativeJudeț(numarInmatriculare) && verifica_numere(numarInmatriculare) && verifica_litere(numarInmatriculare) &&
            numar2 > 0 && numar2 < 61 && verificaDuplicat(db, numarInmatriculare) == 0)
        {

          int foundResult = 0;
          char sql[500];
          sprintf(sql, "SELECT * FROM Parcare WHERE Numar_Parcare=%d AND Numar_Inmatriculare IS NULL;", numar2);

          int rc = sqlite3_exec(db, sql, callback, &foundResult, 0);

          if (rc == SQLITE_OK)
          {
            if (foundResult)
            {
              // Locul de parcare este liber
              updateCarInParking(numar2, numarInmatriculare);
              memset(buffer, 0, sizeof(buffer));
              char bufi[1000];
              if (numar2 >= 1 && numar2 <= 20)
              {
                sprintf(bufi, "Ai parcat masina cu numar de inmatriculare %s pe locul %d ,care se afla la Parter!", numarInmatriculare, numar2);
              }
              else if (numar2 >= 21 && numar2 <= 40)
              {
                sprintf(bufi, "Ai parcat masina cu numar de inmatricualre %s pe locul %d ,care se afla la Etajul 1!", numarInmatriculare, numar2);
              }
              else
              {
                sprintf(bufi, "Ai parcat masina cu numar de inmatricualre %s pe locul %d ,care se afla la Etajul 2!", numarInmatriculare, numar2);
              }

              strcpy(buffer, bufi);
            }
            else
            {

              memset(buffer, 0, sizeof(buffer));
              strcpy(buffer, "Ai ales un loc de parcare ocupat!\n");
            }
          }

          else
          {
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, "EROARE LA OPERATIUNEA FACUTA DE BAZA DE DATE!\n");
            fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
          }
        }
        else if (numar2 > 0 && numar2 < 61 && verificaDuplicat(db, numarInmatriculare) == 0)
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Nu ai introdus un format valid pentru numarul de inmatriculare!\n");
        }
        else if (suntIndicativeJudeț(numarInmatriculare) && verifica_numere(numarInmatriculare) && verifica_litere(numarInmatriculare) &&
                 verificaDuplicat(db, numarInmatriculare) == 0)
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Locul de parcare ales nu exista!\n");
        }
        else if (numar2 > 0 && numar2 < 61 && suntIndicativeJudeț(numarInmatriculare) && verifica_numere(numarInmatriculare) && verifica_litere(numarInmatriculare))
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Masina cu acest numar de inmatriculare se afla deja in parcare!\n");
        }
        else if (suntIndicativeJudeț(numarInmatriculare) && verifica_numere(numarInmatriculare) && verifica_litere(numarInmatriculare))
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Locul de parcare ales nu exista si masina cu acest numar de inmatriculare se afla deja in parcare!\n");
        }
        else
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Locul de parcare ales nu exista ,nu ai introdus un format valid pentru numarul de inmatriculare!\n");
        }
      }
      else
      {
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "Ne pare rau dar toate locurile de parcare sunt ocupate,poti reveni mai tarziu!\n");
      }
    }

    else if (numar == 2 && strcmp(numarInmatriculare, "") != 0) // scos masina din parcare
    {

      char sql2[500];
      int foundResult = 0;
      sprintf(sql2, "SELECT * FROM Parcare WHERE Numar_Inmatriculare='%s';", numarInmatriculare);
      int rc = sqlite3_exec(db, sql2, callback, &foundResult, 0);
      if (rc == SQLITE_OK)
      {
        if (foundResult)
        {

          const char *sql = "UPDATE Parcare SET Numar_Inmatriculare = NULL WHERE Numar_Inmatriculare = ?;";

          // Prepară interogarea SQL
          sqlite3_stmt *stmt;
          rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

          if (rc != SQLITE_OK)
          {
            fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
          }

          // Leagă valoarea la parametrul interogării
          sqlite3_bind_text(stmt, 1, numarInmatriculare, -1, SQLITE_STATIC);

          // Execută interogarea
          rc = sqlite3_step(stmt);

          if (rc != SQLITE_DONE)
          {
            fprintf(stderr, "Eroare la executarea interogării SQL: %s\n", sqlite3_errmsg(db));
          }
          else
          {
            memset(buffer, 0, sizeof(buffer));
            char bufi[1000];
            sprintf(bufi, "Masina ta cu numarul de inamtriculare  %s a fost scoasa din parcare! ", numarInmatriculare);
            strcpy(buffer, bufi);
          }

          // Finalizează interogarea
          sqlite3_finalize(stmt);
        }

        else
        {

          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Masina ta nu se afla in parcare!\n");
        }
      }
      else
      {
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "EROARE LA OPERATIUNEA FACUTA DE BAZA DE DATE!\n");
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
      }
    }

    else if (numar == 3) // inchiderea aplicatiei
    {
      break;
    }
    else if (numar == 4) // sectiunea ajutor
    {
      char ajutor[2000] = {0};
      strcat(ajutor, "\n");
      strcat(ajutor, "Parcarea este impartita pe 3 nivele:\n");
      strcat(ajutor, "Parter(numerele parcarilor sunt cuprinse intre 1 si 20 ),aici costul este mai ridicat deoarece accesul la masina ta este mai rapid\n");
      strcat(ajutor, "Etajul 1 (numerele parcarilor sunt cuprinse intre 20 si 40) costul este putin mai scazut\n");
      strcat(ajutor, "Etajul 2( numerele parcarilor sunt cuprinse intre 40 si 60), costul parcarii este cel mai accesibil\n");
      strcat(ajutor, "Introdu 1 si zona din parcare pe care o preferi\n");
      strcat(ajutor, "(Parter identificat prin 1, etajul 1 identificata prin 2 ,etajul 2 idenficat prin 3)");
      strcat(ajutor, "pentru a vedea mai intai locurile libere din zona respectiva daca doresti!\n");
      strcat(ajutor, "Introdu 1 pentru a vedea locurile libere de parcare indiferent de zona\n");
      strcat(ajutor, "Introdu 1 ,numarul numarului de parcare dorit si numarul de inmatriculare al masinii tale pentru a finaliza parcarea!\n");
      strcat(ajutor, "Introdu 2 si numarul de inmatriculare al masinii tale  daca vreti sa-ti scoti masina din parcare,nu are rost sa introduci aceasta comanda daca masina ta nu se afla in parcare!\n");
      strcat(ajutor, "Introdu 3 daca vrei sa inchizi aplicatia!\n");
      strcat(ajutor, "Introdu 4 pentru a vedea sectiunea de ajutor!\n");
      strcat(ajutor, "Introdu 5 si numarul de inmatriculare al masinii tale pentru a vedea in ce zona si in ce loc de parcare se afla!\n");
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, ajutor);
    }
    else if (numar == 5 && strcmp(numarInmatriculare, "") != 0)
    {
      char sql2[500];
      int foundResult = 0;
      sprintf(sql2, "SELECT * FROM Parcare WHERE Numar_Inmatriculare='%s';", numarInmatriculare);
      int rc = sqlite3_exec(db, sql2, callback, &foundResult, 0);
      if (rc == SQLITE_OK)
      {
        if (foundResult)
        {

          sqlite3_stmt *stmt;
          const char *sql = "SELECT Numar_Parcare FROM Parcare WHERE Numar_Inmatriculare = ?;";

          rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
          if (rc != SQLITE_OK)
          {
            fprintf(stderr, "Eroare la pregătirea interogării SQL: %s\n", sqlite3_errmsg(db));
          }

          // Leagă valoarea la parametrul interogării
          sqlite3_bind_text(stmt, 1, numarInmatriculare, -1, SQLITE_STATIC);

          // Execută interogarea
          rc = sqlite3_step(stmt);

          if (rc == SQLITE_ROW)
          {
            // Obține rezultatul (numărul de parcare)
            int numarParcare = sqlite3_column_int(stmt, 0);
            char bufi[1000];
            if (numarParcare >= 1 && numarParcare <= 20)
            {
              sprintf(bufi, "Masina ta cu numarul de inmatriculare  %s  se afla la Parter, pe locul de parcare %d! ", numarInmatriculare, numarParcare);
            }
            else if (numarParcare >= 21 && numarParcare <= 40)
            {
              sprintf(bufi, "Masina ta cu numarul de inmatriculare %s se afla la Etajul 1 , pe locul de parcare %d! ", numarInmatriculare, numarParcare);
            }
            else
            {
              sprintf(bufi, "Masina ta cu numarul de inmatriculare %s se afla la Etajul 2, pe locul de parcare %d! ", numarInmatriculare, numarParcare);
            }
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, bufi);
          }
          sqlite3_finalize(stmt);
        }
        else
        {
          memset(buffer, 0, sizeof(buffer));
          strcpy(buffer, "Masina ta nu se afla in parcare!\n");
        }
      }
      else
      {
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "EROARE LA OPERATIUNEA FACUTA DE BAZA DE DATE!\n");
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
      }
    }
    else
    {
      memset(buffer, 0, sizeof(buffer));
      strcpy(buffer, "Nu ai introdus o comanda valida!\n");
    }

    printf("[Thread %d]Trimitem mesajul inapoi...%s\n", tdL.idThread, buffer);
    fflush(stdout);

    /* returnam mesajul clientului */
    size_t bufferlen = strlen(buffer) + 1;
    size_t bufferh = htonl(bufferlen);
    if (write(tdL.cl, &bufferh, sizeof(size_t)) <= 0)
    {
      perror("[client]Eroare la bufferlen write() spre client.\n");
    }

    if (write(tdL.cl, buffer, bufferlen) <= 0)
    {
      perror("[client]Eroare la write() spre client.\n");
    }
    else
      printf("[Thread %d]Mesajul a fostce trasmis cu sucs.\n", tdL.idThread);
  }
  sqlite3_close(db);
  close(tdL.cl);
}
