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
#define LSHIFT 2
#define RSHIFT 0x20
#define HOLD_COUNT 50
#define BUFFER_SIZE 129
#define USER_ROW 18

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
int execute_key(uint8_t key, uint8_t modifiers, int position, char * message, int len);
void print_message(char * message, int start_row, int cursor_pos);

// Add caps lock tracking variable
int caps_lock_enabled = 0;  

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


static const char keycode_to_ascii_caps_lock[128] = {
    0,   0,   0,   0,  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'l',  // 0x00-0x0Fl
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2',  // 0x10-0x1F
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
  drawline(USER_ROW-1);


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

  struct usb_keyboard_packet prev = {0, 0, {0, 0, 0, 0, 0, 0}};
  int cursor_pos = 0;
  int len = 0;
  int change = 0;
  char message[BUFFER_SIZE] = {0}; // Initialize all elements to zero
  /* Look for and handle keypresses */

  
  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      //Checking for the rightmost key pressed, as that is the only one we may want to send.
      uint8_t rightmost = 0;
      for(int i = 0; i < 6; i++){
        if(packet.keycode[i] == 0){
          break;
        }
        rightmost = i+1;
      }
      //If no key is pressed, we skip the rest of the loop
      if(rightmost != 0){
        //decrementing the rightmost key to get the last key pressed
        rightmost-=1;

        //Checking if the key is new, or if it was one that was held before the most recent key was pressed
        uint8_t new = 1;
        for(int i = 0; i < 6; i++){
          if(prev.keycode[i] == 0){
            break;
          }
          if(prev.keycode[i] == packet.keycode[rightmost]){
            new = 0;
            break;
          }
        }
        
        if(new == 1){
          // //Checking for left and right arrows
          if(packet.keycode[rightmost] == 0x50){
            if(cursor_pos > 0){
              cursor_pos--;
            }
          }
          else if(packet.keycode[rightmost] == 0x4F){
            if(cursor_pos < len){
              cursor_pos++;
            }
          }
          // Check for caps lock key
          else if(packet.keycode[rightmost] == 0x39){
            caps_lock_enabled = !caps_lock_enabled;  // Toggle caps lock state
            // You could add visual indicator here if desired
          }
          else if(packet.keycode[rightmost] == 0x28 && packet.modifiers == 0){
            write(sockfd, message, len);
            for(int i = 0; i < (BUFFER_SIZE/ROW_WIDTH-1)+1; i++){
              clearline(USER_ROW + i);
            }
            message[0] = '\0';
            len = 0;
            cursor_pos = 0;
          }
          else{
            //execute the key
            change = execute_key(packet.keycode[rightmost], packet.modifiers, cursor_pos, message, len);
            cursor_pos+=change;
            len+=change;
            if(len>BUFFER_SIZE-1){
              len = BUFFER_SIZE-1;
            }
            if(cursor_pos>BUFFER_SIZE-1){
              cursor_pos = BUFFER_SIZE-1;
            }
            printf("len: %d cursor: %d\n", len, cursor_pos);
          }
        }
      }

      
      prev = packet;
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
      //printf("%s\n", keystate); //prints the keystate
      //printf("%s\n", message); //prints the message
      fbputs(keystate, 6, 0); //places the keystate onto the screen
      

      print_message(message, USER_ROW, cursor_pos);
      
      //fbputs(" ",cursor_row,cursor_col-1); //render cursor
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
int execute_key(uint8_t key, uint8_t modifiers, int position, char* message, int len){
  //Dealing with backspace
  //printf("Called Execute_Key\n");
  if(key == 0x2A){
    //printf("Key == 0x2A");
    if(position > 0){
      //printf("Position > 0");
      for(int i = position; i < len; i++){
        message[i-1] = message[i];
      }
      message[len-1] = '\0';
      position--;
      return -1;
    }
    return 0;
  }
  int top = len;
  if(len>BUFFER_SIZE - 2){
    top = BUFFER_SIZE - 2;
  }
  
  // everything else
  if(modifiers == 0){
    //printf("Modifiers == 0\n");
    //printf("Key: %d\n", key);
    
    if(keycode_to_ascii[key] != 0){
      //printf("keycode != 0  %d\n", position);
      
      //Shift everything after position down
      for(int i = top; i > position; i--){
        message[i] = message[i-1];
      }
      if(caps_lock_enabled){
        message[position] = keycode_to_ascii_caps_lock[key];
      }
      else{
        message[position] = keycode_to_ascii[key];
      }
      //printf("after shift things\n");
    }
    else{
      return 0;
    }
  }
  else if(modifiers == LSHIFT || modifiers == RSHIFT || modifiers == (LSHIFT | RSHIFT)){
    //printf("Shift\n");
    if(keycode_to_ascii_shift[key] != 0){
      //printf("keycode != 0\n");
      //Shift everything after position down
      for(int i = top; i > position; i--){
        message[i] = message[i-1];
      }
      message[position] = keycode_to_ascii_shift[key];
    }
    else{
      return 0;
    }
  }
  else{
    return 0;
  }
  message[top+1] = '\0';
  return  1;
}

void print_message(char * message, int start_row, int cursor_pos){
  int rows = (strlen(message))/ROW_WIDTH + 1;
  //Clear the input section
  for(int i = 0; i < rows; i++){
    clearline(start_row+i);
    char temp = message[(i+1)*ROW_WIDTH];
    message[(i+1)*ROW_WIDTH] = '\0';
    fbputs(&(message[i*ROW_WIDTH]), start_row + i, 0);
    message[(i+1)*ROW_WIDTH] = temp;
  }
  draw_cursor(cursor_pos);
  //
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  int recvRow = 0; // Start at row 1 to avoid top border
  
  /* Receive data */
  while ((n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0) {
    recvBuf[n] = '\0'; 
    printf("%s\n", recvBuf);
    
    // Calculate how many rows this message will need
    int msg_len = strlen(recvBuf);
    int rows_needed = (msg_len / ROW_WIDTH) + 1;
    
    // Check if we need to wrap to the top
    if (recvRow + rows_needed >= USER_ROW - 1) {
      recvRow = 1; // Reset to the top (below the border)
    }
    
    // Clear the rows we'll use
    for (int i = 0; i < rows_needed; i++) {
      clearline(recvRow + i);
    }
    
    // Display the message with wrapping
    for (int i = 0; i < rows_needed; i++) {
      // Calculate the start and end indices for this row
      int start_idx = i * ROW_WIDTH;
      if (start_idx >= msg_len) break;
      
      // Create a temporary buffer for this row
      char row_buf[ROW_WIDTH + 1];
      int chars_to_copy = (start_idx + ROW_WIDTH <= msg_len) ? 
                          ROW_WIDTH : (msg_len - start_idx);
      
      // Copy the appropriate section of the message
      strncpy(row_buf, &recvBuf[start_idx], chars_to_copy);
      row_buf[chars_to_copy] = '\0';
      
      // Display this row
      fbputs(row_buf, recvRow + i, 0);
    }
    
    // Update recvRow for the next message
    recvRow += rows_needed;
    
    // If we've reached close to the input area, wrap back to the top
    if (recvRow >= USER_ROW - 1) {
      recvRow = 1;
    }
  }

  return NULL;
}

