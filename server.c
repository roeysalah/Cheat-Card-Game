#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#define true 1
#define false 0
#define max_clients 6



int force_exit();
int listen_socket_handler();
int game_handler();
int encodeHello(char* buffer, int index);
int sendMultiAddr(char* buffer);
int game_timer();
int convert_char_card_to_index(char* card);
int is_valid_move(int i,int j);
void broadcast_turn(int *turn);
int give_to_loser(int loser);
int card_index_to_string(int card_index,char* string);
int declare_winner(int player);
int check_posb_winer();

int close_prog = false;
char MULTICAST_ADDR1[] = "239.1.5.1"; 
char MULTICAST_ADDR2[] = "239.1.5.2"; 
char MULTICAST_ADDR3[] = "239.1.5.3"; 
char* MULTICAST_ADDR;
int num_of_players = 0;
int server_soc, multi_sock;
struct sockaddr_in server_data, client_data[6], mulit_data;
int client_socket[6] = {0};
int player_connected[6] = {false};      // each elem rep if player is on or off
char names[6][64];
char buffer[64] = {'\0'};
int game = false;
int client_hand[6][52] = {0};
char deck[52][4] = {    "H01","H02","H03","H04","H05","H06","H07","H08","H09","H10","H11","H12","H13",
                        "C01","C02","C03","C04","C05","C06","C07","C08","C09","C10","C11","C12","C13",
                        "D01","D02","D03","D04","D05","D06","D07","D08","D09","D10","D11","D12","D13",
                        "S01","S02","S03","S04","S05","S06","S07","S08","S09","S10","S11","S12","S13"};
int pile[52] = {0};
int pointer_to_deck = 0;


pthread_cond_t cv;
pthread_mutex_t lock;

pthread_t listen_thread;
pthread_t game_thread;

char ip[20] = {'0'};
char port[8] = {'0'};
int which_multi = 1;
int to_fork = 1;

int main(int argc, char *argv[])
{
	// 1:ip 2:port
     // create socket

    if (argc == 1)
    {
        printf("  run ./servergame 'IP' 'PORT'\n");
        return EXIT_FAILURE;
    }
    strcpy(ip,argv[1]);
    strcpy(port,argv[2]); 
    if (argc != 3)
    {
        which_multi = atoi(argv[3]);
        to_fork = atoi(argv[4]);
    }
    switch (which_multi)
    {
    case 1:
        MULTICAST_ADDR = MULTICAST_ADDR1;
        break;
    case 2:
        MULTICAST_ADDR = MULTICAST_ADDR2;
        break;
    case 3:
        MULTICAST_ADDR = MULTICAST_ADDR3;
        break;
    }
    if (which_multi < 3 && to_fork == 1)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            char number[2] = {'\0'};
            sprintf(number,"%d",which_multi+1);
            char char_next_port[8] = {'0'};
            sprintf(char_next_port,"%d",atoi(port) + 1);
            char keep_forking[2] = {'\0'};
            sprintf(keep_forking,"%d",1);
            char* args[] = { "./servergame", ip, char_next_port , number, keep_forking ,NULL};
            execv(args[0],args);
        }
    }
    printf("#%d : using multicast = %s\n",which_multi,MULTICAST_ADDR);
    server_soc = socket(AF_INET, SOCK_STREAM, 0);
    if (server_soc < 0)
    {
        perror("error in creating socket\n");
        exit(EXIT_FAILURE);
    }
    int enable = 1;
    if (setsockopt(server_soc, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
    }
    
    // set listening port socket properties
    memset(&server_data, 0, sizeof(server_data));
    server_data.sin_family = AF_INET;
    server_data.sin_addr.s_addr = inet_addr(argv[1]);
    server_data.sin_port = htons(atoi(argv[2]));
    printf("#%d : opening on port %d\n",which_multi,ntohs(server_data.sin_port));

    //MULTICAST 
    multi_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&mulit_data, 0, sizeof(mulit_data));
    mulit_data.sin_family = AF_INET;
    mulit_data.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
    mulit_data.sin_port = htons(6000);
    int k = 64;
    u_char ttl = k;
    setsockopt(multi_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,sizeof(ttl));


    // bind
    if (bind(server_soc, (struct sockaddr *)&server_data, sizeof(server_data)) != 0)
    {
        perror("error in binding\n");
        exit(EXIT_FAILURE);
    }

    // make listen thread
    if (pthread_create(&listen_thread, NULL, (void *)listen_socket_handler, NULL) != 0) 
    {
            perror("error in thread creation\n");
            exit(EXIT_FAILURE);
    }
    // make game thread
    if (pthread_create(&game_thread, NULL, (void *)game_handler, NULL) != 0) 
    {
            perror("error in thread creation\n");
            exit(EXIT_FAILURE);
    } 

    // block til' change

    pthread_mutex_lock(&lock);
    while(close_prog == false)
    {
        pthread_cond_wait(&cv, &lock);
    }   
    //free resources
    int r;
    for ( r = 0; r < max_clients; r++)
    {
        if (player_connected[r])
        {
            memset(buffer,0,64);
            recv(client_socket[r],buffer,64,0);
            if (!(buffer[0] == 'Q'))
            {
                /* error or  nonsense */
                printf("error in getting user quit msg, closing the socket myself");
                close(client_socket[r]);
            }
            else
            {
                printf("#%d : the client closed his socket\n",which_multi);
                close(client_socket[r]);
            }
        }
    }
    printf("#%d : closing listening socket\n",which_multi);
    close(server_soc);
    printf("#%d : closing multicast socket\n",which_multi);
    close(multi_sock);
    sleep(1);
    char number[2] = {'\0'};
    sprintf(number,"%d",which_multi);
    char keep_forking[2] = {'\0'};
    sprintf(keep_forking,"%d",0);
    char* args[] = { "./servergame", argv[1], argv[2] , number , keep_forking ,NULL};
    printf("#%d : restarting game\n",which_multi);
    execv(args[0],args);
    return EXIT_SUCCESS;
}


int force_exit()
{
    close_prog = true;
    close(server_soc);
    pthread_cond_signal(&cv);
}

int listen_socket_handler()
{
    listen(server_soc, max_clients);
    // till two players are in keep listening
    fd_set readfds;
    int max_socket = 0;
    int inputfd = 0;
    int i = 0;
    while (num_of_players < 7 && close_prog == false)     
    {
        FD_ZERO(&readfds);              // clear
        FD_SET(server_soc, &readfds);   // set server socket
        FD_SET(fileno(stdin),&readfds); // set keyboard

        // add clients if any, and max
        max_socket = server_soc + 1;
        if(!game)
        {
            for ( i = 0 ; i < max_clients ; i++)  
            {  
                if(client_socket[i] > 0){ FD_SET( client_socket[i] , &readfds);}
                if(client_socket[i] > max_socket){ max_socket = client_socket[i]+1;}
            }
        }
        // set timeout
        struct timeval timeout;
        if (num_of_players >= 2) { timeout.tv_sec = 7 ;}
        else { timeout.tv_sec = 86400 ;}

        
        inputfd = select(max_socket,&readfds, NULL , NULL, &timeout);
        if (inputfd == 0)                           //timeout
        {
            if (num_of_players >= 2 && !game)
            {
                /* start game */
                /* send multicast address and raise flag afterwards*/
                /* make big timer*/
                sendMultiAddr(buffer);
                game = true;
                /*open game thread*/
            }
            else if(!game)
            {
                perror("timeout\n");
                force_exit();
                pthread_exit(NULL);
            }
        }
        else if(FD_ISSET(fileno(stdin),&readfds))   // keyboard
        {
            /*
            perror("user interrupt\n");
            force_exit();
            pthread_exit(NULL);
            */
        }
        else if (FD_ISSET(server_soc, &readfds))    // listening socket
        {
            // new connection -> try to give it an open socket
            // if game not already started do this
            if(!game)
            {
                for ( i = 0; i < max_clients; i++)
                {
                    if (player_connected[i] == false)
                    {
                        // can accept him to this open socket
                        int addrlen = sizeof(client_data[i]);
                        memset(&client_data[i], 0, addrlen);
                        if ((client_socket[i] = accept(server_soc, (struct sockaddr*) &client_data[i], (socklen_t*)&addrlen)) < 0)
                        {
                            perror("error in accepting");
                            force_exit();
                            pthread_exit(NULL);
                        }
                        printf("#%d : new connection\n",which_multi);
                        printf("#%d : from : %s\n",which_multi,(char*)inet_ntoa((struct in_addr)client_data[i].sin_addr));

                        memset(buffer,0,64);
                        recv(client_socket[i],buffer,64,0); // client sends his name
                        if(!encodeHello(buffer,i))
                        {
                            /* close the socket ? */
                            close(client_socket[i]);
                            perror("player sent not in hello format\n");
                        }
                        else
                        {
                            player_connected[i] = true;
                            num_of_players += 1;
                            memset(buffer,0,64);
                            int buffer_len = sprintf(buffer,"W%d",i);
                            send(client_socket[i],buffer,buffer_len,0);
                        }
                        i = max_clients;

                        if (num_of_players == 6)
                        {
                            /* start game */
                            // make big timer for game
                            sendMultiAddr(buffer);
                            game = true;
                        }
                    }
                }
            }
            else
            {
                // if game already in progress -> reject them
                printf("#%d : new connection but game is in progress\n",which_multi);
                int addrlen = sizeof(client_data[i]);
                int temp_client, temp_socket;
                memset(&temp_client, 0, addrlen);
                if ((temp_socket = accept(server_soc, (struct sockaddr*) &temp_client, (socklen_t*)&addrlen)) < 0)
                {
                    perror("error in accepting");
                    force_exit();
                    pthread_exit(NULL);
                }
                close(temp_socket);
            } 
        }
        else if(num_of_players >= 1)
        {
            /*
            do a check to see if the game is not in progress
            when game starts the server needs to inform the other player that this player quit
            */
           if(!game)
           {
                for ( i = 0; i < 6; i++)
                {
                    if (player_connected[i])
                    {
                        if (FD_ISSET(client_socket[i],&readfds))
                        {
                            /*somebody in lobby sent a msg*/
                            /*assume terminated socket*/
                            memset(buffer,0,64);
                            int status = recv(client_socket[i],buffer,64,0);
                            if (status < 1 || buffer[0] == 'Q')
                            {
                                /* something wrong happends */
                                printf("#%d : %s closed connection\n",which_multi,names[i]);
                                player_connected[i] = false;
                                num_of_players -= 1;
                                i = 6;
                            }
                        }
                    }
                }
           }
        }
    }
    /*close this thread*/
    pthread_exit(NULL);
}

int game_handler()
{
    while(!game){};
    // now the game started
    printf("#%d : game started!\n",which_multi); 
    fd_set readfds2;
    int i;

    // make game timer
    pthread_t game_time;
    if (pthread_create(&game_time, NULL, (void *)game_timer, NULL) != 0) 
    {
            perror("error in thread creation\n");
            exit(EXIT_FAILURE);
    } 
    // start game
    /*send starting hand to all players*/
    int j;
    for ( i = 0; i < 52; i++)
    {
        j = (rand()) % 52;
        if (j != i)
        {
            char temp[3] = {0};
            memcpy(temp,deck[i],3);
            memset(deck[i],0,3);
            memcpy(deck[i],deck[j],3);
            memcpy(deck[j],temp,3);
        }
    }
    j = 0;
    for ( i = 0 ;i < 6; i++)
    {
        if (player_connected[i])
        {
            memset(buffer,0,64);
            sprintf(buffer,"%s%s%s%s%s%s",deck[j],deck[j+1],deck[j+2],deck[j+3],deck[j+4],deck[j+5]);
            j += 6;
            pointer_to_deck = j;
            send(client_socket[i],buffer,64,0);
            /* update client deck*/
            int k;
            int l;
            for ( k = 0; k < 6; k++)
            {
                char temp[3] = {0};
                for ( l = k*3; l < k*3 + 3; l++)
                {
                    temp[l - k*3] = buffer[l];
                }
                int index  = convert_char_card_to_index(temp);
                client_hand[i][index] = 1;
            }
        }
    }
    int prev_card = convert_char_card_to_index(deck[pointer_to_deck]);
    pile[prev_card] = 1;
    /* put intial card from deck to pile and multicast it*/
    sleep(1);
    memset(buffer,0,64);
    sprintf(buffer,"P%s",deck[pointer_to_deck]);
    pointer_to_deck ++;
    sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));


    /* now wait for their turn*/
    int turn = 0;       // who's turn it is
    int turn_counter = 0;
    int can_cheat = false;
    int prev_player_cheated = false;
    int prev_player = 0;
    broadcast_turn(&turn);
    int maybe_winner = false;
    while (game && close_prog == false)
    {
        FD_ZERO(&readfds2);              // clear

        // add clients if any, and max
        for ( i = 0 ; i < max_clients ; i++)
        {  
            if(player_connected[i] > 0){ FD_SET( client_socket[i] , &readfds2);}
        }
        // set timeout
        struct timeval timeout;
        timeout.tv_sec = 45;

        sleep(1);

        printf("#%d : turn = %d\n",which_multi,turn);
        int inputfd = select(FD_SETSIZE,&readfds2, NULL , NULL, &timeout);
        if (inputfd == 0)
        {
            /* player timeout  need to do something */
            turn = (turn +1) % 6;
            turn_counter ++;
            printf("#%d : player timeout\n",which_multi);
            broadcast_turn(&turn);
        }
        else
        {
            for ( i = 0; i < 6; i++)
            {
                if (player_connected[i] && FD_ISSET(client_socket[i],&readfds2))
                {
                    
                    if (turn == i)
                    {
                        printf("#%d : player %d played\n",which_multi,i);
                        /* correct turn check if decleration or cheat*/
                        memset(buffer,0,64);
                        int status = recv(client_socket[i],buffer,64,0);
                        if (status < 1 || buffer[0] == 'Q')
                        {
                            /* something wrong happends */
                            printf("#%d : %s closed connection\n",which_multi,names[i]);
                            player_connected[i] = false;
                            num_of_players -= 1;
                            i = 6;
                            if (num_of_players == 1)
                            {
                                /* close the game declare the remaning player the winner*/
                                /* find who is still connected and declare him the winner*/
                                int r;
                                for ( r = 0; r < max_clients; r++)
                                {
                                    if (player_connected[r])
                                    {
                                        declare_winner(r);
                                    }
                                    
                                }
                            }
                            
                        }
                        else if (buffer[0] == 'c')
                        {
                            /* cheat */
                            /* check if not cheat on yourself*/
                            if (i == prev_player || !can_cheat)
                            {
                                /* invalid cheat */
                                memset(buffer,0,64);
                                sprintf(buffer,"e");
                                send(client_socket[i],buffer,64,0);

                            }
                            else
                            {
                                /* valid cheat tell the player it was ok*/
                                sprintf(buffer,"L");
                                send(client_socket[i],buffer,64,0);
                                /* valid cheat-> tell who said cheat on who
                                so they can start listening on tcp */
                                /* c(who said cheat)(cheat on who)*/
                                memset(buffer,0,64);
                                sprintf(buffer,"c%d%d",i,prev_player);
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                /* now the two canidates are ready */
                                if (prev_player_cheated)
                                {
                                    maybe_winner = false;
                                    printf("#%d : previous player cheated !\n",which_multi);
                                    /* send on multicast who lost ->
                                    the who who lost need to start recv from tcp
                                    send on multicast L(player_num)
                                    */
                                    memset(buffer,0,64);
                                    sprintf(buffer,"L%d",prev_player);
                                    sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                    give_to_loser(prev_player);
                                }
                                else
                                {
                                    memset(buffer,0,64);
                                    sprintf(buffer,"L%d",i);
                                    sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                    give_to_loser(i);
                                    if (maybe_winner)
                                    {
                                        /* we have winner */
                                        declare_winner(prev_player);
                                    }
                                }

                                prev_player_cheated = false;
                                prev_card = convert_char_card_to_index(deck[pointer_to_deck]);
                                pile[prev_card] = 1;

                                sleep(1);
                                memset(buffer,0,64);
                                sprintf(buffer,"P%s",deck[pointer_to_deck]);
                                pointer_to_deck ++;
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                /*
                                    maybe send to other player who lost and who won
                                */
                                prev_player = i;
                                turn = (turn +1) % 6;
                                turn_counter ++;
                                i=6;
                                can_cheat = false;
                                broadcast_turn(&turn);

                            }
                            
                        }
                        else if (buffer[0] == 't')
                        {
                            /* taking a card */

                            if((pointer_to_deck < 52))
                            {
                                if (maybe_winner)
                                {
                                    /* we have winner */
                                    declare_winner(prev_player);
                                }
                                memset(buffer,0,64);
                                sprintf(buffer,"t%s",deck[pointer_to_deck]);
                                send(client_socket[i],buffer,64,0);
                                pointer_to_deck++;
                                memset(buffer,0,64);
                                sprintf(buffer,"t%d",i);
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                prev_player = i;
                                turn = (turn +1) % 6;
                                turn_counter ++;
                                i=6;
                                broadcast_turn(&turn);
                                can_cheat = false;
                            }
                            else
                            {
                                /* empty deck */
                                memset(buffer,0,64);
                                sprintf(buffer,"e");
                                send(client_socket[i],buffer,64,0);
                                check_posb_winer();
                            }
                            
                        }
                        else
                        {
                            // decleration
                            if (maybe_winner)
                            {
                                /* we have winner */
                                declare_winner(prev_player);
                            }
                            
                            char player_decleration[4] = {buffer[0],buffer[1],buffer[2],'\0'};
                            int player_decleration_index = convert_char_card_to_index(player_decleration);
                            char player_actual_card[3] = {buffer[4],buffer[5],buffer[6]};
                            int player_actual_card_index = convert_char_card_to_index(player_actual_card);
                            if (client_hand[i][player_actual_card_index] && is_valid_move(player_decleration_index,prev_card))
                            {
                                /* valid move, has card + correct move*/
                                prev_card = player_decleration_index;
                                client_hand[i][player_actual_card_index] = 0;
                                memset(buffer,0,64);
                                sprintf(buffer,"G%02d%s%d",turn_counter,player_decleration,i);
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                pile[player_actual_card_index] = 1;
                                prev_player_cheated = false;
                                if (player_actual_card_index != player_decleration_index)
                                {
                                    prev_player_cheated = true;
                                }
                                can_cheat = true;
                                prev_player = i;
                                int r;
                                maybe_winner = true;
                                for ( r = 0; r < 52; r++)
                                {
                                    if(client_hand[i][r] == 1)
                                    {
                                        maybe_winner = false;
                                        break;
                                    }
                                }
                                turn = (turn +1) % 6;
                                turn_counter ++;
                                i=6;
                                broadcast_turn(&turn);

                            }
                            else
                            {
                                /* he doesnt have this card*/
                                printf("#%d : not valid move\n",which_multi);
                            }
                        }
                    }
                    else
                    {
                        /* not his turn -> he cant play at all ! */
                        /* maybe disconnected*/
                        memset(buffer,0,64);
                        int status = recv(client_socket[i],buffer,64,0);
                        if (status < 1 || buffer[0] == 'Q')
                        {
                            /* something wrong happends */
                            printf("#%d : %s closed connection\n",which_multi,names[i]);
                            player_connected[i] = false;
                            num_of_players -= 1;
                            i = 6;
                            if (num_of_players == 1)
                            {
                                /* close the game declare the remaning player the winner*/
                                /* find who is still connected and declare him the winner*/
                                int r;
                                for ( r = 0; r < max_clients; r++)
                                {
                                    if (player_connected[r])
                                    {
                                        declare_winner(r);
                                    }
                                    
                                }
                                
                            }
                            
                        }
                        /* not disconnected check if cheat or wrong msg*/
                        if (can_cheat && buffer[0] == 'c' && (prev_player != i))
                        {
                            /* valid cheat */
                            sprintf(buffer,"L");
                            send(client_socket[i],buffer,64,0);

                            memset(buffer,0,64);
                            sprintf(buffer,"c%d%d",i,prev_player);
                            sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                            if (prev_player_cheated)
                            {
                                maybe_winner = false;
                                printf("#%d : previous player cheated !\n",which_multi);
                                /* send on multicast who lost ->
                                the who who lost need to start recv from tcp
                                send on multicast L(player_num)
                                */
                                memset(buffer,0,64);
                                sprintf(buffer,"L%d",prev_player);
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                give_to_loser(prev_player);
                            }
                            else
                            {
                                memset(buffer,0,64);
                                sprintf(buffer,"L%d",i);
                                sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                give_to_loser(i);
                                if (maybe_winner)
                                {
                                    /* we have winner */
                                    declare_winner(prev_player);
                                }
                            }

                            prev_player_cheated = false;
                            prev_card = convert_char_card_to_index(deck[pointer_to_deck]);
                            pile[prev_card] = 1;

                            sleep(1);
                            memset(buffer,0,64);
                            sprintf(buffer,"P%s",deck[pointer_to_deck]);
                            pointer_to_deck ++;
                            sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
                                /*
                                    maybe send to other player who lost and who won
                                */
                            prev_player = i;
                            turn = (turn +1) % 6;
                            turn_counter ++;
                            i=6;
                            can_cheat = false;
                            broadcast_turn(&turn);
                        }
                        else if (buffer[0] == 't')
                        {
                            /* discard */
                            memset(buffer,0,64);
                            sprintf(buffer,"E");
                            send(client_socket[i],buffer,64,0);
                        }
                        else
                        {
                            /* discard */
                            memset(buffer,0,64);
                            sprintf(buffer,"e");
                            send(client_socket[i],buffer,64,0);
                        }
                        

                    }  
                }
            }
        }    
    }
    if (!game)
    {
        /* game timeout, declare winner */
        check_posb_winer();
    }
         

    /*close this trhead*/
    pthread_exit(NULL);
    return EXIT_SUCCESS;
}

int is_valid_move(int card1, int card2)
{
    if (card1%13 == (1 + card2%13)%13 || card1%13 == (-1 + card2%13)%13 || (card1%13 + 1)%13 == card2%13 || (card1%13 - 1)%13 == card2%13)
    {
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
    return 0;
}

int game_timer()
{
    int done = false;
    fd_set readfds;
    struct timeval game_time;
    game_time.tv_sec = 360;
    while(!done && close_prog == false)
    {
        FD_ZERO(&readfds);
        int inputfd = select(0,&readfds, NULL , NULL, &game_time);
        if (inputfd == 0)
        {
            /* game timeout */
            printf("#%d : game timeout!\n",which_multi);
            done = true;
        }
    }
    /* close the game */
    game = false;
    pthread_exit(NULL);
}


int encodeHello(char* buffer, int index)
{
    // check if hello
    if (buffer[0] == 'H')
    {
        int name_len = (int)buffer[1] - 48;
        memset(names[index],0,64);
        sprintf(names[index],"%s",buffer+2);
        int i;
        for (i = name_len; i < 64; i++)
        {
            names[index][i] = 0;
        }
        return 1;
        
    }
    return 0;
}

int sendMultiAddr(char* buffer)
{
    //Mlen(multicast_addr)$(multicast)addr)$(number of players)
    memset(buffer,0,64);
    int msg_len = sprintf(buffer,"M%d$%s%d",(int)strlen(MULTICAST_ADDR),MULTICAST_ADDR,num_of_players);
    int i;
    for (i = 0; i < max_clients; i++)
    {
        if (player_connected[i])
        {
            send(client_socket[i],buffer,64,0);
        }
    }
}

void broadcast_turn(int* turn)
{
    while (!player_connected[*turn])
    {
        *turn = (*turn +1)%6;
    }
    memset(buffer,0,64);
    sprintf(buffer,"T%d",*turn);
    sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
    printf("#%d : turn = %d\n",which_multi,*turn);
}

int give_to_loser(int loser)
{
    int k;
    // (S12)(C05)(D10)S
    for ( k = 0; k < 52; k++)
    {
        if (pile[k])
        {
            char string[12] = {0};
            memset(buffer,0,64);
            card_index_to_string(k,string);
            sprintf(buffer,"%s",string);
            send(client_socket[loser],buffer,64,0);
            pile[k] = 0;
        }
    }
    memset(buffer,0,64);
    sprintf(buffer,"$");
    send(client_socket[loser],buffer,64,0); 
}

int card_index_to_string(int card_index,char* string)
{
    char fun_deck[52][4] = {    "H01","H02","H03","H04","H05","H06","H07","H08","H09","H10","H11","H12","H13",
                        "C01","C02","C03","C04","C05","C06","C07","C08","C09","C10","C11","C12","C13",
                        "D01","D02","D03","D04","D05","D06","D07","D08","D09","D10","D11","D12","D13",
                        "S01","S02","S03","S04","S05","S06","S07","S08","S09","S10","S11","S12","S13"};
    strcpy(string,fun_deck[card_index]);
    return 1;
}

int declare_winner(int player)
{
    memset(buffer,0,64);
    sprintf(buffer,"F%d",player);
    sendto(multi_sock, buffer, 64, 0, (struct sockaddr *) &mulit_data,sizeof(mulit_data));
    close_prog = true;
    pthread_cond_signal(&cv);
}


int check_posb_winer()
{
    int min_cards = 52;
    int posb_player = 0;
    int i,j;
    int counter;
    for ( i = 0; i < max_clients; i++)
    {
        if (player_connected[i])
        {
            counter = 0;
            for (j = 0; j < 52; j++)
            {
                if (client_hand[i][j])
                {
                    counter++;
                }
                
            }
            if (counter < min_cards)
            {
                min_cards = counter;
                posb_player = i;
                printf("#%d : posb player = %d, he has %d cards\n",which_multi,i,counter);
            }
        }   
    }
    declare_winner(posb_player);
}
