/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Robert Pendergrast, Moises Mata, Isaac Trost
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define HOLD_COUNT 50
#define BUFFER_SIZE 128

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);
void execute_key(uint8_t key, uint8_t modifiers, int position, char * message);
// USB HID Keyboard scancode to ASCII mapping
static const char keycode_to_ascii[128] = {
    0,   0,   0,   0,  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',  // 0x00-0x0F
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',  // 0x10-0x1F
    '3', '4', '5', '6', '7', '8', '9', '0', '\n', 0,   0,   0,   ' ', '-', '=', '[',  // 0x20-0x2F
    ']', '\\', 0,  ';', '\'', '`', ',', '.', '/',  0,   0,   0,   0,   0,   0,   0,   // 0x30-0x3F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x40-0x4F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x50-0x5F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x60-0x6F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0    // 0x70-0x7F
};

// Shift key modifier for special characters
static const char keycode_to_ascii_shift[128] = {
    0,   0,   0,   0,  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',  // 0x00-0x0F
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',  // 0x10-0x1F
    '#', '$', '%', '^', '&', '*', '(', ')', '\n', 0,   0,   0,   ' ', '_', '+', '{',  // 0x20-0x2F
    '}', '|', 0,  ':', '"', '~', '<', '>', '?',  0,   0,   0,   0,   0,   0,   0,   // 0x30-0x3F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x40-0x4F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x50-0x5F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 0x60-0x6F
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0    // 0x70-0x7F
};

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  /* Draw rows of asterisks across the top and bottom of the screen */

  //Turn this into the draw line function?
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col,255,0,255);
    fbputchar('*', 23, col,255,0,255);
  }

  fbputs("Hello CSEE 4840 World!", 4, 10);
  
  //Testing the drawline and clear screen functions
  clearscreen();
  drawline(17);


  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  int cursor_row = 18;
  int cursor_col = 0;
  uint8_t held_char = 0;
  uint8_t held_mod = 0;
  struct usb_keyboard_packet prev = {0, 0, {0, 0, 0, 0, 0, 0}};
  uint8_t held_count = 0; 
  char message[BUFFER_SIZE];
  /* Look for and handle keypresses */
  for (;;) {

    fbputs("_",cursor_row,cursor_col);
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      printf("pressed\n");
      uint8_t rightmost = 0;
      for(int i = 0; i < 6; i++){
        if(packet.keycode[i] == 0){
          printf("break 1\n");
          break;
        }
        rightmost = i+1;
      }
      
      if(rightmost == 0){
        held_char = 0;
        held_mod = 0;
        printf("continue 1\n");
        continue;
      }
      rightmost-=1;

      if(rightmost == held_char && packet.modifiers == held_mod){
        if(held_count < HOLD_COUNT){
          held_count++;                       
          
          printf("continue 2\n");
          continue;
        }
        execute_key(rightmost, packet.modifiers, 0, message);
        cursor_col++;
        printf("continue 3\n");
        continue;
      }
      uint8_t new = 1;
      for(int i = 0; i < 6; i++){
        if(packet.keycode[i] == 0){
          printf("break 2\n");
          break;
        }
        if(prev.keycode[i] == rightmost){
          new = 0;
          printf("break 3\n");
          break;
        }
      }
      if(new){
        execute_key(rightmost, packet.modifiers, 0, message);
        cursor_col++;
        held_char = rightmost;
        held_mod = packet.modifiers;
      }
      prev = packet;
      fbputs(" ", cursor_row, cursor_col);
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
      printf("%s\n", keystate); //prints the keystate
      fbputs(keystate, 6, 0); //places the keystate onto the screen
      fbputs("",cursor_row,cursor_col-1);
      if (packet.keycode[0] == 0x29) { /* ESC pressed? */
	      break;
      }
    }
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}
void execute_key(uint8_t key, uint8_t modifiers, int position, char* message){
  return;
}
void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, 0, 0); //changed the row to below the line 
  }

  return NULL;
}

