#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"

// Function prototypes
void send_message(int socket_fd, const char *message);
void receive_message(int socket_fd, char *buffer);

int main()
{
    int socket_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Create socket
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    // Main loop for communication
    while (1)
    {
        // Receive message from server
        memset(buffer, 0, BUFFER_SIZE);
        if (read(socket_fd, buffer, BUFFER_SIZE - 1) <= 0)
        {
            printf("Server disconnected\n");
            break;
        }

        // Print message from server
        printf("%s", buffer);

        // Check if server is waiting for input
        if (strstr(buffer, ": ") != NULL && buffer[strlen(buffer) - 1] != '\n')
        {
            // Get input from user
            fgets(buffer, BUFFER_SIZE, stdin);

            // Send input to server
            if (write(socket_fd, buffer, strlen(buffer)) <= 0)
            {
                perror("Failed to send message");
                break;
            }
        }
    }

    // Close socket
    close(socket_fd);

    return 0;
}

// Send message to server
void send_message(int socket_fd, const char *message)
{
    if (write(socket_fd, message, strlen(message)) == -1)
    {
        perror("Failed to send message");
    }
}

// Receive message from server
void receive_message(int socket_fd, char *buffer)
{
    int bytes_read = read(socket_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0)
    {
        if (bytes_read == 0)
        {
            // Server disconnected
            printf("Server disconnected\n");
        }
        else
        {
            perror("Failed to receive message");
        }
        buffer[0] = '\0';
        return;
    }

    // Null-terminate the buffer
    buffer[bytes_read] = '\0';
}
