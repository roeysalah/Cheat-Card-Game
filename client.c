#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define false 0
#define true 1

int exit_prem();
void ctrl_c_handler(int);
int decodeMultiAddr();
int in_game();
int decode_startin_hand();  
int convert_char_card_to_index(char* card);
int multicast_listen();
int card_index_to_string(int card_index,char* string);

int client_soc;
int player_num = 0;
int num_of_players =0;
char MULTICAST_ADDR[64] ={'\0'};
int num_of_players;
char buffer[64];
char multi_buffer[64];
int my_hand[52] = {0};
int is_valid_move(int i,int j);
int print_hand();

struct ip_mreq mreq;
struct sockaddr_in multi_data;
int multi_sock;
pthread_t game_thread;

pthread_cond_t cv;
pthread_mutex_t lock;
int close_prog = false;
int prev_card = 0;
int next_player = 0;
int has_multicast_addr = false;

char convert_index_to_string[52][12] = {    "Heart A","Heart 2","Heart 3","Heart 4","Heart 5","Heart 6","Heart 7","Heart 8","Heart 9","Heart 10","Heart J","Heart Q","Heart K",
                                            "Clubs A","Clubs 2","Clubs 3","Clubs 4","Clubs 5","Clubs 6","Clubs 7","Clubs 8","Clubs 9","Clubs 10","Clubs J","Clubs Q","Clubs K",
                                            "Diamond A","Diamond 2","Diamond 3","Diamond 4","Diamond 5","Diamond 6","Diamond 7","Diamond 8","Diamond 9","Diamond 10","Diamond J","Diamond Q","Diamond K",
                                            "Spades A","Spades 2","Spades 3","Spades 4","Spades 5","Spades 6","Spades 7","Spades 8","Spades 9","Spades 10","Spades J","Spades Q","Spades K"};

int main(int argc, char *argv[])
{
    // 1:ip 2:port 3:name
    struct sockaddr_in client_data, server_data;
    signal(SIGINT, ctrl_c_handler);

    // create socket
    client_soc = socket(AF_INET, SOCK_STREAM, 0);
    if (client_soc == -1)
    {
        perror("error in creating socket\n");
        exit(EXIT_FAILURE);
    }
    // set  socket properties
    memset(&client_data, 0, sizeof(client_data));
    // set server socket
    memset(&server_data, 0, sizeof(server_data));
    server_data.sin_family = AF_INET;
    server_data.sin_addr.s_addr = inet_addr(argv[1]);
    server_data.sin_port = htons(atoi(argv[2]));

    // multicast socket
    multi_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&multi_data, 0, sizeof(multi_data));
    multi_data.sin_family = AF_INET;
    multi_data.sin_addr.s_addr = htonl(INADDR_ANY);
    multi_data.sin_port = htons(6000);
    u_int yes = 1;
    setsockopt(multi_sock, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof(yes));

    if  (bind(multi_sock, (struct sockaddr *) &multi_data, sizeof(multi_data))< 0)
    {
        printf("error in binding! \n");
    }

    printf("connecting to %s port: %d as: %s\n",argv[1],atoi(argv[2]),argv[3]);
    // connect
    if (connect(client_soc, (struct sockaddr *)&server_data, sizeof(server_data)) == -1)
    {
        printf("failed to connect to server\n");
        exit(EXIT_FAILURE);
    }
    //when client connect, client should send "H(len(name))name"
    // generate msg
    char* str = (char*)malloc((strlen(argv[3])+3)*sizeof(char));
    int msg_len = sprintf(str,"H%d%s",(int)strlen(argv[3]),argv[3]);
    //send
    send(client_soc,str, strlen(str), 0);
    free(str);

    //client should get either Welcome msg or Quit msg
    memset(buffer,0,64);
    int s = recv(client_soc, buffer, 64, 0);
    if (s > 0)
    {
        //got msg check result
        if (*buffer == 'W')
        {
            printf("connected\n");
            player_num = atoi(&buffer[1]);
            printf("player num = %d\n",player_num);
            
        }
        
    }
    else
    { 
        /* got error or close socket*/  
        printf("maximun number of games\n");
        return EXIT_FAILURE;
    }
    
    // now wait to receive the multicast addr of the game
    s = recv(client_soc, buffer, 64, 0);
    if (s > 0)
    {
        //got msg check result
        decodeMultiAddr();
        bzero(&mreq,sizeof(struct ip_mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ssize_t status = 0;
        if ((status = setsockopt(multi_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &mreq, sizeof(mreq))) < 0)
        {
            fprintf(stderr, "setsockopt: %s (%d)\n", strerror(errno), errno);
            char temp[] = "Q";
            send(client_soc,temp, strlen(temp), 0);
            printf("\nsent quit msg\n");
            close(client_soc);
            exit(EXIT_FAILURE);
        }
        //recvfrom(multi_sock, buffer, 64, 0, (struct sockaddr *) &multi_data, &multilen)
        has_multicast_addr = true;
        if (pthread_create(&game_thread, NULL, (void *)in_game, NULL) != 0) 
        {
            perror("error in thread creation\n");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // close socket and sent Q msg
        char temp[] = "Q";
        send(client_soc,temp, strlen(temp), 0);
        printf("\nsent quit msg\n");
        close(client_soc);
        exit(EXIT_FAILURE);
    }

    

    pthread_mutex_lock(&lock);
    while(close_prog == false)
    {
        pthread_cond_wait(&cv, &lock);
    }   
    //free resources
    char temp[] = "Q";
    send(client_soc,temp, strlen(temp), 0);
    printf("\nsent quit msg\n");
    close(client_soc);
    close(multi_sock);
    printf("\nbye\n");
    return EXIT_SUCCESS;
}

int in_game()
{
    /* we got multicast addr, we expect to recive from the tcp out starting hand*/
    memset(buffer,0,64);
    int status = recv(client_soc, buffer, 64, 0);
    if(!(decode_startin_hand() == true && status > 0))
    {
        /* got not valid hand*/
    }
    /* got multicast address -> make thread to print what is broadcasting */
    pthread_t multicast_listen_thread;
    if (pthread_create(&multicast_listen_thread, NULL, (void *)multicast_listen, NULL) != 0) 
    {
        perror("error in thread creation\n");
        exit(EXIT_FAILURE);
    }
    printf("put c for 'cheat', put t to 'taking card' or put decleration card (ie C05) followed by space and actual card :\n\n");  // C05 H01
    while(!close_prog)
    {
        printf("currently: ");
        print_hand();
        printf("\n");
        char user_input[64] = {0};
        int j;
        fgets(user_input,64,stdin);
        if (user_input[0] == 'c')
        {
            /* cheat */
            send(client_soc,user_input, 64, 0);
            memset(buffer,0,64);
            int status = recv(client_soc, buffer, 64, 0);
            if (buffer[0] == 'e')
            {
                printf("invalid cheat ! \n");
            }
            
        }
        else if ( user_input[0] == 't')
        {
            /* take a card */
            send(client_soc,user_input, 64, 0);
            // wait for card from server
            memset(buffer,0,64);
            int status = recv(client_soc, buffer, 64, 0);
            if (buffer[0] == 't')
            {
                /* took a card */
                char card[4] = {buffer[1],buffer[2],buffer[3],'\0'};
                my_hand[convert_char_card_to_index(card)] = 1 ;
                printf("got card index %d, card = %s\n",convert_char_card_to_index(card),card);
            }
            else if(buffer[0] == 'e')
            {
                /* empty deck */
                printf("empty deck try again\n");
            }
            else if(buffer[0] == 'E')
            {
                printf("not your turn try again later\n");
            }
        }
        else
        {
            char player_card[3] = {user_input[4],user_input[5],user_input[6]};
            char decleration_card[3] = {user_input[0],user_input[1],user_input[2]};
            int decleration_card_index = convert_char_card_to_index(decleration_card);
            int player_card_index = convert_char_card_to_index(player_card);
            if (my_hand[player_card_index] && is_valid_move(decleration_card_index,prev_card) && next_player == player_num)
            {
                my_hand[player_card_index] = 0 ;
                print_hand();
                send(client_soc,user_input, 64, 0);
            }
            else
            {
                printf("not valid move ! \n");
            }
        }
        
    }
    pthread_exit(NULL);
}

int multicast_listen()
{
    int multilen = sizeof(multi_data);
    int my_turn = 0;
    while(!close_prog)
    {
        memset(multi_buffer,0,64);
        recvfrom(multi_sock, multi_buffer, 64, 0, (struct sockaddr *) &multi_data, &multilen);

        if (multi_buffer[0] == 'P')
        {
            char card_char[3] ={buffer[1],buffer[2],buffer[3]};
            int card_index = convert_char_card_to_index(multi_buffer+1);
            prev_card = card_index;
            char string[12] = {0};
            card_index_to_string(card_index,string);
            printf("~~~~~the server put card : %s\n",string);
        }
        else if (multi_buffer[0] == 'T')
        {
            next_player  = atoi(&multi_buffer[1]);
            printf("next player turn %d\n",next_player);
        }
        else if (multi_buffer[0] == 'G')
        {
            char card_char[3] ={multi_buffer[3],multi_buffer[4],multi_buffer[5]};
            int server_turn = 10*atoi(&multi_buffer[1]) + atoi(&multi_buffer[2]);
            int card_index = convert_char_card_to_index(multi_buffer+3);
            prev_card = card_index;
            char c_player_played = multi_buffer[6];
            int player_played = atoi(&c_player_played);
            if (server_turn != my_turn)
            {
                /* means we maybe were passed */
            }
            char string[12] = {0};
            card_index_to_string(card_index,string);
            printf("~~~~~player %d put a card and declated %s\n",player_played,string);
        }
        else if (multi_buffer[0] == 'E')
        {
            int player_error = atoi(&multi_buffer[1]);
            printf("player %d did an incorrent move\n",player_error);
        }
        else if (multi_buffer[0] == 't')
        {
            int playr_took = atoi(&multi_buffer[1]);
            printf("player %d took a card\n",playr_took);
        }
        else if (multi_buffer[0] == 'c')
        {
            int suspected_player = multi_buffer[2]-48;
            int suspecting_player = multi_buffer[1]-48;
            printf("player %d said cheat on player %d\n",suspecting_player,suspected_player);         
        }
        else if(multi_buffer[0] == 'L')
        {
            int loser = multi_buffer[1]-48;
            if (player_num == loser)
            {
                /* start receiving from tcp */
                char stop_sign = '0';
                while (stop_sign != '$')
                {
                    memset(buffer,0,64);
                    recv(client_soc, buffer, 64, 0);
                    stop_sign = buffer[0];
                    if (stop_sign != '$')
                    {
                        char card_char[3] ={buffer[0],buffer[1],buffer[2]};
                        int card_index = convert_char_card_to_index(card_char);
                        my_hand[card_index] = 1;
                        char temp[12] = {0};
                        card_index_to_string(card_index,temp);
                        printf("i took = %s\n",temp);
                    }
                    
                }
                print_hand();
                
            }
        }
        else if(multi_buffer[0] == 'F')
        {
            int winner = multi_buffer[1]-48;
            printf("player %d won the game\n",winner);
            close_prog = true;
            pthread_cond_signal(&cv);

        }
        else
        {
            close_prog = true;
            pthread_cond_signal(&cv);
        }
    }
    pthread_exit(NULL);
}

int is_valid_move(int card1, int card2)
{
    if (card1%13 == (1 + card2%13)%13 || card1%13 == (-1 + card2%13)%13 || (card1%13 + 1)%13 == card2%13 || (card1%13 - 1)%13 == card2%13)
    {
        return 1;
    }
    return 0;
}

int card_index_to_string(int card_index,char* string)
{
    strcpy(string,convert_index_to_string[card_index]);
    return 1;
}

int decode_startin_hand()
{
    int k;
    int l;
    printf("my hand = ");
    for ( k = 0; k < 6; k++)
    {
        char temp[3] = {0};
        for ( l = k*3; l < k*3 + 3; l++)
        {
           temp[l - k*3] = buffer[l];
        }
        int index  = convert_char_card_to_index(temp);
        char string[12] = {0};
        card_index_to_string(index,string);
        printf(", %s", string);
        my_hand[index] = 1;
    }
    printf(" \n");
    return true;
}


int exit_prem()
{
    close(client_soc);
    perror("exit prem\n");
    return EXIT_SUCCESS;
}

void ctrl_c_handler(int sig)
{
    signal(sig, SIG_IGN);
    char temp[] = "Q";
    send(client_soc,temp, strlen(temp), 0);
    printf("\nsent quit msg\n");
    if (has_multicast_addr)
    {
        close(multi_sock);
    }
    
    close(client_soc);
    exit(EXIT_FAILURE);
}

int decodeMultiAddr()
{
    // check if multicast
    if (buffer[0] == 'M')
    {
        int mult_len = 0;
        int i;
        int offset = 0;
        if (buffer[2] == '$')
        {
            /* ML$ */
            mult_len = (int)buffer[1] - 48;
            offset = 3;
        }
        else if (buffer[3] == '$')
        {
            /* MLL$ */
            mult_len = ((int)buffer[1] - 48)*10;
            mult_len += (int)buffer[2] - 48;
            offset = 4;
        }

        memset(MULTICAST_ADDR,0,64);
        
        for (i = offset ; i < mult_len + offset; i++)
        {
            MULTICAST_ADDR[i-offset] = buffer[i];
        }
        num_of_players = (int)buffer[mult_len + offset] -48;
        return 1;
        
    }
    return 0;
}


int convert_char_card_to_index(char* card)
{
    char temp[2];
    temp[0] = card[1];
    temp[1] = card[2];
    int number = atoi(temp);
    switch (card[0])
    {
    case 'H':
        return number - 1;
        break;
    case 'C':
        return number + 13 - 1;
        break;
    case 'D':
        return number + 2*13 - 1;
        break;
    case 'S':
        return number + 3*13 - 1;
        break;
    }
    char temp1[] = "Q";
    send(client_soc,temp1, strlen(temp1), 0);
    printf("\nsent quit msg\n");
    if (has_multicast_addr)
    {
        close(multi_sock);
    }
    close(client_soc);
    exit(EXIT_FAILURE);
    return 0;
}

int print_hand()
{
    int i;
    printf("my hand = ");
    for ( i = 0; i < 52; i++)
        {
            if (my_hand[i])
            {
                char string[12] = {0};
                card_index_to_string(i,string);
                printf(" %s",string);
            }
        }
    printf("\n");
}
                    
