#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>
#include <sys/stat.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_USERNAME 50
#define MAX_PASSWORD 50

// User roles
#define ADMIN 1
#define FACULTY 2
#define STUDENT 3

// File paths
#define STUDENT_FILE "data/students.dat"
#define FACULTY_FILE "data/faculty.dat"
#define COURSE_FILE "data/courses.dat"
#define USER_FILE "data/users.dat"

// Structures
typedef struct
{
    int id;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int role;
    int active; // 1 for active, 0 for inactive
} User;

typedef struct
{
    int id;
    char name[100];
    char department[50];
    int semester;
    int active;
} Student;

typedef struct
{
    int id;
    char name[100];
    char department[50];
    int active;
} Faculty;

typedef struct
{
    int id;
    char name[100];
    char faculty_username[MAX_USERNAME];
    int max_seats;
    int available_seats;
    int active;
} Course;

typedef struct
{
    int student_id;
    int course_id;
} Enrollment;

// Function prototypes
void handle_client(int client_socket);
int authenticate_user(char *username, char *password, User *user);
void admin_menu(int client_socket, User *user);
void faculty_menu(int client_socket, User *user);
void student_menu(int client_socket, User *user);
void add_student(int client_socket);
void add_faculty(int client_socket);
void activate_deactivate_student(int client_socket);
void update_details(int client_socket);
void add_course(int client_socket, User *user);
void remove_course(int client_socket, User *user);
void view_enrollments(int client_socket, User *user);
void change_password(int client_socket, User *user);
void enroll_course(int client_socket, User *user);
void unenroll_course(int client_socket, User *user);
void view_enrolled_courses(int client_socket, User *user);
int get_user_by_username(char *username, User *user);
int get_student_by_username(char *username, Student *student);
int get_faculty_by_username(char *username, Faculty *faculty);
void create_data_directory();
void initialize_files();
void send_message(int client_socket, const char *message);
void receive_message(int client_socket, char *buffer);
void create_account(int client_socket);

// Global variables
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Main function
int main()
{
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;

    // Create data directory and initialize files
    create_data_directory();
    initialize_files();

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d\n", PORT);

    // Accept connections and create threads to handle clients
    while (1)
    {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("New client connected: %d\n", client_socket);

        // Create a new thread to handle the client
        if (pthread_create(&thread_id, NULL, (void *)handle_client, (void *)(intptr_t)client_socket) != 0)
        {
            perror("Thread creation failed");
            close(client_socket);
            continue;
        }

        // Detach the thread to automatically clean up when it exits
        pthread_detach(thread_id);
    }

    // Close the server socket
    close(server_fd);

    return 0;
}

// Create data directory if it doesn't exist
void create_data_directory()
{
    // Using mkdir system call to create directory
    if (mkdir("data", 0777) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Failed to create data directory");
            exit(EXIT_FAILURE);
        }
    }
}

// Initialize files with default admin user if they don't exist
void initialize_files()
{
    int fd;

    // Create users file if it doesn't exist
    if ((fd = open(USER_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open users file");
        exit(EXIT_FAILURE);
    }

    // Check if the file is empty
    if (lseek(fd, 0, SEEK_END) == 0)
    {
        // File is empty, add default admin user
        User admin;
        admin.id = 1;
        strcpy(admin.username, "admin");
        strcpy(admin.password, "admin123");
        admin.role = ADMIN;
        admin.active = 1;

        // Write admin user to file
        if (write(fd, &admin, sizeof(User)) == -1)
        {
            perror("Failed to write admin user");
            close(fd);
            exit(EXIT_FAILURE);
        }

        printf("Default admin user created\n");
    }

    close(fd);

    // Create other files if they don't exist
    if ((fd = open(STUDENT_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open students file");
        exit(EXIT_FAILURE);
    }
    close(fd);

    if ((fd = open(FACULTY_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open faculty file");
        exit(EXIT_FAILURE);
    }
    close(fd);

    if ((fd = open(COURSE_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open courses file");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

// Handle client connection
void handle_client(int client_socket)
{
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    User user;
    char buffer[BUFFER_SIZE];
    char choice[BUFFER_SIZE];

    // Send welcome message
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "            Welcome to Academia Portal\n");
    send_message(client_socket, "==========================================================\n\n");

    // Ask if user wants to create an account
    send_message(client_socket, "+---------------------------+\n");
    send_message(client_socket, "| 1. Login                  |\n");
    send_message(client_socket, "| 2. Create Account         |\n");
    send_message(client_socket, "+---------------------------+\n");
    send_message(client_socket, "Enter your choice: ");
    receive_message(client_socket, choice);

    if (atoi(choice) == 2)
    {
        create_account(client_socket);
        close(client_socket);
        printf("Client disconnected\n");
        return;
    }

    // Get username
    send_message(client_socket, "Username: ");
    receive_message(client_socket, username);

    // Get password
    send_message(client_socket, "Password: ");
    receive_message(client_socket, password);

    // Authenticate user
    if (authenticate_user(username, password, &user))
    {
        // Check if user is active
        if (!user.active)
        {
            send_message(client_socket, "Your account is inactive. Please contact the administrator.\n");
            close(client_socket);
            return;
        }

        // Send welcome message based on role
        sprintf(buffer, "Welcome, %s!\n", username);
        send_message(client_socket, buffer);

        // Show menu based on role
        switch (user.role)
        {
        case ADMIN:
            admin_menu(client_socket, &user);
            break;
        case FACULTY:
            faculty_menu(client_socket, &user);
            break;
        case STUDENT:
            student_menu(client_socket, &user);
            break;
        default:
            send_message(client_socket, "Invalid role. Disconnecting.\n");
            break;
        }
    }
    else
    {
        send_message(client_socket, "Invalid username or password. Disconnecting.\n");
    }

    // Close client socket
    close(client_socket);
    printf("Client disconnected\n");
}

// Authenticate user
int authenticate_user(char *username, char *password, User *user)
{
    int fd;
    User temp;

    // Open users file
    if ((fd = open(USER_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open users file");
        return 0;
    }

    // Read users from file and check credentials
    while (read(fd, &temp, sizeof(User)) > 0)
    {
        if (strcmp(temp.username, username) == 0 && strcmp(temp.password, password) == 0)
        {
            *user = temp;
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

// Admin menu
void admin_menu(int client_socket, User *user)
{
    char choice[BUFFER_SIZE];
    int running = 1;

    while (running)
    {
        // Display menu
        send_message(client_socket, "\n==========================================================\n");
        send_message(client_socket, "                     ADMIN MENU\n");
        send_message(client_socket, "==========================================================\n\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "| 1. Add Student                           |\n");
        send_message(client_socket, "| 2. Add Faculty                           |\n");
        send_message(client_socket, "| 3. Activate/Deactivate Student           |\n");
        send_message(client_socket, "| 4. Update Student/Faculty details        |\n");
        send_message(client_socket, "| 5. Exit                                  |\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "Enter your choice: ");

        // Get choice
        receive_message(client_socket, choice);

        // Process choice
        switch (atoi(choice))
        {
        case 1:
            add_student(client_socket);
            break;
        case 2:
            add_faculty(client_socket);
            break;
        case 3:
            activate_deactivate_student(client_socket);
            break;
        case 4:
            update_details(client_socket);
            break;
        case 5:
            send_message(client_socket, "Logging out...\n");
            running = 0;
            break;
        default:
            send_message(client_socket, "Invalid choice. Please try again.\n");
            break;
        }
    }
}

// Faculty menu
void faculty_menu(int client_socket, User *user)
{
    char choice[BUFFER_SIZE];
    int running = 1;

    while (running)
    {
        // Display menu
        send_message(client_socket, "\n==========================================================\n");
        send_message(client_socket, "                    FACULTY MENU\n");
        send_message(client_socket, "==========================================================\n\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "| 1. Add new Course                        |\n");
        send_message(client_socket, "| 2. Remove offered Course                 |\n");
        send_message(client_socket, "| 3. View enrollments in Courses           |\n");
        send_message(client_socket, "| 4. Password Change                       |\n");
        send_message(client_socket, "| 5. Exit                                  |\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "Enter your choice: ");

        // Get choice
        receive_message(client_socket, choice);

        // Process choice
        switch (atoi(choice))
        {
        case 1:
            add_course(client_socket, user);
            break;
        case 2:
            remove_course(client_socket, user);
            break;
        case 3:
            view_enrollments(client_socket, user);
            break;
        case 4:
            change_password(client_socket, user);
            break;
        case 5:
            send_message(client_socket, "Logging out...\n");
            running = 0;
            break;
        default:
            send_message(client_socket, "Invalid choice. Please try again.\n");
            break;
        }
    }
}

// Student menu
void student_menu(int client_socket, User *user)
{
    char choice[BUFFER_SIZE];
    int running = 1;

    while (running)
    {
        // Display menu
        send_message(client_socket, "\n==========================================================\n");
        send_message(client_socket, "                    STUDENT MENU\n");
        send_message(client_socket, "==========================================================\n\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "| 1. Enroll to new Courses                 |\n");
        send_message(client_socket, "| 2. Unenroll from already enrolled Courses|\n");
        send_message(client_socket, "| 3. View enrolled Courses                 |\n");
        send_message(client_socket, "| 4. Password Change                       |\n");
        send_message(client_socket, "| 5. Exit                                  |\n");
        send_message(client_socket, "+------------------------------------------+\n");
        send_message(client_socket, "Enter your choice: ");

        // Get choice
        receive_message(client_socket, choice);

        // Process choice
        switch (atoi(choice))
        {
        case 1:
            enroll_course(client_socket, user);
            break;
        case 2:
            unenroll_course(client_socket, user);
            break;
        case 3:
            view_enrolled_courses(client_socket, user);
            break;
        case 4:
            change_password(client_socket, user);
            break;
        case 5:
            send_message(client_socket, "Logging out...\n");
            running = 0;
            break;
        default:
            send_message(client_socket, "Invalid choice. Please try again.\n");
            break;
        }
    }
}

// Add student
void add_student(int client_socket)
{
    char buffer[BUFFER_SIZE];
    Student student;
    User user;
    int fd, user_fd;

    // Get student details
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                     ADD STUDENT\n");
    send_message(client_socket, "==========================================================\n\n");

    // Get username
    send_message(client_socket, "Username: ");
    receive_message(client_socket, buffer);

    // Check if username already exists
    if (get_user_by_username(buffer, &user))
    {
        send_message(client_socket, "Username already exists. Please try again.\n");
        return;
    }

    strcpy(user.username, buffer);

    // Get password
    send_message(client_socket, "Password: ");
    receive_message(client_socket, user.password);

    // Get name
    send_message(client_socket, "Name: ");
    receive_message(client_socket, student.name);

    // Get department
    send_message(client_socket, "Department: ");
    receive_message(client_socket, student.department);

    // Get semester
    send_message(client_socket, "Semester: ");
    receive_message(client_socket, buffer);
    student.semester = atoi(buffer);

    // Set student as active
    student.active = 1;

    // Lock files for writing
    pthread_mutex_lock(&file_mutex);

    // Open student file
    if ((fd = open(STUDENT_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open students file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add student. Please try again.\n");
        return;
    }

    // Get next student ID
    student.id = lseek(fd, 0, SEEK_END) / sizeof(Student) + 1;

    // Write student to file
    if (write(fd, &student, sizeof(Student)) == -1)
    {
        perror("Failed to write student");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add student. Please try again.\n");
        return;
    }

    close(fd);

    // Open user file
    if ((user_fd = open(USER_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open users file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add student. Please try again.\n");
        return;
    }

    // Get next user ID
    user.id = lseek(user_fd, 0, SEEK_END) / sizeof(User) + 1;
    user.role = STUDENT;
    user.active = 1;

    // Write user to file
    if (write(user_fd, &user, sizeof(User)) == -1)
    {
        perror("Failed to write user");
        close(user_fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add student. Please try again.\n");
        return;
    }

    close(user_fd);

    // Unlock files
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Student added successfully.\n");
}

// Add faculty
void add_faculty(int client_socket)
{
    char buffer[BUFFER_SIZE];
    Faculty faculty;
    User user;
    int fd, user_fd;

    // Get faculty details
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                     ADD FACULTY\n");
    send_message(client_socket, "==========================================================\n\n");

    // Get username
    send_message(client_socket, "Username: ");
    receive_message(client_socket, buffer);

    // Check if username already exists
    if (get_user_by_username(buffer, &user))
    {
        send_message(client_socket, "Username already exists. Please try again.\n");
        return;
    }

    strcpy(user.username, buffer);

    // Get password
    send_message(client_socket, "Password: ");
    receive_message(client_socket, user.password);

    // Get name
    send_message(client_socket, "Name: ");
    receive_message(client_socket, faculty.name);

    // Get department
    send_message(client_socket, "Department: ");
    receive_message(client_socket, faculty.department);

    // Set faculty as active
    faculty.active = 1;

    // Lock files for writing
    pthread_mutex_lock(&file_mutex);

    // Open faculty file
    if ((fd = open(FACULTY_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open faculty file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add faculty. Please try again.\n");
        return;
    }

    // Get next faculty ID
    faculty.id = lseek(fd, 0, SEEK_END) / sizeof(Faculty) + 1;

    // Write faculty to file
    if (write(fd, &faculty, sizeof(Faculty)) == -1)
    {
        perror("Failed to write faculty");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add faculty. Please try again.\n");
        return;
    }

    close(fd);

    // Open user file
    if ((user_fd = open(USER_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open users file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add faculty. Please try again.\n");
        return;
    }

    // Get next user ID
    user.id = lseek(user_fd, 0, SEEK_END) / sizeof(User) + 1;
    user.role = FACULTY;
    user.active = 1;

    // Write user to file
    if (write(user_fd, &user, sizeof(User)) == -1)
    {
        perror("Failed to write user");
        close(user_fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add faculty. Please try again.\n");
        return;
    }

    close(user_fd);

    // Unlock files
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Faculty added successfully.\n");
}

// Activate/deactivate student
void activate_deactivate_student(int client_socket)
{
    char buffer[BUFFER_SIZE];
    User user;
    Student student;
    int fd, user_fd;

    // Get student username
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "              ACTIVATE/DEACTIVATE STUDENT\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "Enter student username: ");
    receive_message(client_socket, buffer);

    // Check if username exists and is a student
    if (!get_user_by_username(buffer, &user) || user.role != STUDENT)
    {
        send_message(client_socket, "Student not found. Please try again.\n");
        return;
    }

    // Get student details
    if (!get_student_by_username(buffer, &student))
    {
        send_message(client_socket, "Student details not found. Please try again.\n");
        return;
    }

    // Toggle active status
    user.active = !user.active;
    student.active = !student.active;

    // Lock files for writing
    pthread_mutex_lock(&file_mutex);

    // Update user file
    if ((user_fd = open(USER_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open users file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    // Seek to user position
    if (lseek(user_fd, (user.id - 1) * sizeof(User), SEEK_SET) == -1)
    {
        perror("Failed to seek in users file");
        close(user_fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    // Write updated user
    if (write(user_fd, &user, sizeof(User)) == -1)
    {
        perror("Failed to write user");
        close(user_fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    close(user_fd);

    // Update student file
    if ((fd = open(STUDENT_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open students file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    // Seek to student position
    if (lseek(fd, (student.id - 1) * sizeof(Student), SEEK_SET) == -1)
    {
        perror("Failed to seek in students file");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    // Write updated student
    if (write(fd, &student, sizeof(Student)) == -1)
    {
        perror("Failed to write student");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update student status. Please try again.\n");
        return;
    }

    close(fd);

    // Unlock files
    pthread_mutex_unlock(&file_mutex);

    // Send success message
    if (student.active)
    {
        send_message(client_socket, "Student activated successfully.\n");
    }
    else
    {
        send_message(client_socket, "Student deactivated successfully.\n");
    }
}

// Update student/faculty details
void update_details(int client_socket)
{
    char buffer[BUFFER_SIZE];
    User user;
    Student student;
    Faculty faculty;
    int fd;

    // Get username
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "            UPDATE STUDENT/FACULTY DETAILS\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "Enter username: ");
    receive_message(client_socket, buffer);

    // Check if username exists
    if (!get_user_by_username(buffer, &user))
    {
        send_message(client_socket, "User not found. Please try again.\n");
        return;
    }

    // Update based on role
    if (user.role == STUDENT)
    {
        // Get student details
        if (!get_student_by_username(buffer, &student))
        {
            send_message(client_socket, "Student details not found. Please try again.\n");
            return;
        }

        // Get updated details
        send_message(client_socket, "Enter new name (leave empty to keep current): ");
        receive_message(client_socket, buffer);
        if (strlen(buffer) > 0)
        {
            strcpy(student.name, buffer);
        }

        send_message(client_socket, "Enter new department (leave empty to keep current): ");
        receive_message(client_socket, buffer);
        if (strlen(buffer) > 0)
        {
            strcpy(student.department, buffer);
        }

        send_message(client_socket, "Enter new semester (leave empty to keep current): ");
        receive_message(client_socket, buffer);
        if (strlen(buffer) > 0)
        {
            student.semester = atoi(buffer);
        }

        // Lock file for writing
        pthread_mutex_lock(&file_mutex);

        // Open student file
        if ((fd = open(STUDENT_FILE, O_RDWR)) == -1)
        {
            perror("Failed to open students file");
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update student details. Please try again.\n");
            return;
        }

        // Seek to student position
        if (lseek(fd, (student.id - 1) * sizeof(Student), SEEK_SET) == -1)
        {
            perror("Failed to seek in students file");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update student details. Please try again.\n");
            return;
        }

        // Write updated student
        if (write(fd, &student, sizeof(Student)) == -1)
        {
            perror("Failed to write student");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update student details. Please try again.\n");
            return;
        }

        close(fd);

        // Unlock file
        pthread_mutex_unlock(&file_mutex);

        send_message(client_socket, "Student details updated successfully.\n");
    }
    else if (user.role == FACULTY)
    {
        // Get faculty details
        if (!get_faculty_by_username(buffer, &faculty))
        {
            send_message(client_socket, "Faculty details not found. Please try again.\n");
            return;
        }

        // Get updated details
        send_message(client_socket, "Enter new name (leave empty to keep current): ");
        receive_message(client_socket, buffer);
        if (strlen(buffer) > 0)
        {
            strcpy(faculty.name, buffer);
        }

        send_message(client_socket, "Enter new department (leave empty to keep current): ");
        receive_message(client_socket, buffer);
        if (strlen(buffer) > 0)
        {
            strcpy(faculty.department, buffer);
        }

        // Lock file for writing
        pthread_mutex_lock(&file_mutex);

        // Open faculty file
        if ((fd = open(FACULTY_FILE, O_RDWR)) == -1)
        {
            perror("Failed to open faculty file");
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update faculty details. Please try again.\n");
            return;
        }

        // Seek to faculty position
        if (lseek(fd, (faculty.id - 1) * sizeof(Faculty), SEEK_SET) == -1)
        {
            perror("Failed to seek in faculty file");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update faculty details. Please try again.\n");
            return;
        }

        // Write updated faculty
        if (write(fd, &faculty, sizeof(Faculty)) == -1)
        {
            perror("Failed to write faculty");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to update faculty details. Please try again.\n");
            return;
        }

        close(fd);

        // Unlock file
        pthread_mutex_unlock(&file_mutex);

        send_message(client_socket, "Faculty details updated successfully.\n");
    }
    else
    {
        send_message(client_socket, "Cannot update admin details.\n");
    }
}

// Add course
void add_course(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd;
    Faculty faculty;

    // Get faculty details
    if (!get_faculty_by_username(user->username, &faculty))
    {
        send_message(client_socket, "Faculty details not found. Please contact the administrator.\n");
        return;
    }

    // Get course details
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                     ADD COURSE\n");
    send_message(client_socket, "==========================================================\n\n");

    // Get course name
    send_message(client_socket, "Course Name: ");
    receive_message(client_socket, course.name);

    // Get max seats
    send_message(client_socket, "Maximum Seats: ");
    receive_message(client_socket, buffer);
    course.max_seats = atoi(buffer);
    course.available_seats = course.max_seats;

    // Set course as active and assign faculty
    course.active = 1;
    strcpy(course.faculty_username, user->username);

    // Lock file for writing
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add course. Please try again.\n");
        return;
    }

    // Get next course ID
    course.id = lseek(fd, 0, SEEK_END) / sizeof(Course) + 1;

    // Write course to file
    if (write(fd, &course, sizeof(Course)) == -1)
    {
        perror("Failed to write course");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to add course. Please try again.\n");
        return;
    }

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Course added successfully.\n");
}

// Remove course
void remove_course(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd, found = 0;

    // Get course ID
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                    REMOVE COURSE\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "Enter Course ID: ");
    receive_message(client_socket, buffer);
    int course_id = atoi(buffer);

    // Lock file for reading and writing
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to remove course. Please try again.\n");
        return;
    }

    // Seek to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Read course
    if (read(fd, &course, sizeof(Course)) != sizeof(Course))
    {
        perror("Failed to read course");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Check if course belongs to faculty
    if (strcmp(course.faculty_username, user->username) != 0)
    {
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "You can only remove courses that you have created.\n");
        return;
    }

    // Deactivate course
    course.active = 0;

    // Seek back to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to remove course. Please try again.\n");
        return;
    }

    // Write updated course
    if (write(fd, &course, sizeof(Course)) == -1)
    {
        perror("Failed to write course");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to remove course. Please try again.\n");
        return;
    }

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Course removed successfully.\n");
}

// View enrollments
void view_enrollments(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd, enrollment_fd;
    Enrollment enrollment;
    Student student;

    // Display faculty's courses
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                     YOUR COURSES\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "+-------+--------------------+----------+---------------+\n");
    send_message(client_socket, "| ID    | Name               | Max Seats| Available     |\n");
    send_message(client_socket, "+-------+--------------------+----------+---------------+\n");

    // Lock file for reading
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view courses. Please try again.\n");
        return;
    }

    // Read courses
    while (read(fd, &course, sizeof(Course)) == sizeof(Course))
    {
        if (course.active && strcmp(course.faculty_username, user->username) == 0)
        {
            sprintf(buffer, "| %-5d | %-18s | %-8d | %-13d |\n", course.id, course.name, course.max_seats, course.available_seats);
            send_message(client_socket, buffer);
        }
    }
    send_message(client_socket, "+-------+--------------------+----------+---------------+\n");

    close(fd);

    // Get course ID
    send_message(client_socket, "\nEnter Course ID to view enrollments (0 to cancel): ");
    receive_message(client_socket, buffer);
    int course_id = atoi(buffer);

    if (course_id == 0)
    {
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view enrollments. Please try again.\n");
        return;
    }

    // Seek to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Read course
    if (read(fd, &course, sizeof(Course)) != sizeof(Course))
    {
        perror("Failed to read course");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    close(fd);

    // Check if course belongs to faculty
    if (strcmp(course.faculty_username, user->username) != 0)
    {
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "You can only view enrollments for courses that you have created.\n");
        return;
    }

    // Create enrollment file path
    char enrollment_file[100];
    sprintf(enrollment_file, "data/enrollments_%d.dat", course_id);

    // Open enrollment file
    if ((enrollment_fd = open(enrollment_file, O_RDONLY)) == -1)
    {
        if (errno == ENOENT)
        {
            // File doesn't exist, no enrollments
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "No enrollments for this course.\n");
            return;
        }

        perror("Failed to open enrollments file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view enrollments. Please try again.\n");
        return;
    }

    // Display enrollments
    send_message(client_socket, "\n==========================================================\n");
    sprintf(buffer, "          ENROLLMENTS FOR COURSE: %s\n", course.name);
    send_message(client_socket, buffer);
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "+------------+------------------------------------+\n");
    send_message(client_socket, "| Student ID | Student Name                       |\n");
    send_message(client_socket, "+------------+------------------------------------+\n");

    // Read enrollments
    while (read(enrollment_fd, &enrollment, sizeof(Enrollment)) == sizeof(Enrollment))
    {
        // Get student details
        int student_fd;
        if ((student_fd = open(STUDENT_FILE, O_RDONLY)) == -1)
        {
            perror("Failed to open students file");
            continue;
        }

        // Seek to student position
        if (lseek(student_fd, (enrollment.student_id - 1) * sizeof(Student), SEEK_SET) == -1)
        {
            perror("Failed to seek in students file");
            close(student_fd);
            continue;
        }

        // Read student
        if (read(student_fd, &student, sizeof(Student)) != sizeof(Student))
        {
            perror("Failed to read student");
            close(student_fd);
            continue;
        }

        close(student_fd);

        // Display student details
        sprintf(buffer, "| %-10d | %-34s |\n", student.id, student.name);
        send_message(client_socket, buffer);
    }
    send_message(client_socket, "+------------+------------------------------------+\n");

    close(enrollment_fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);
}

// Change password
void change_password(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    int fd;

    // Get new password
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                   CHANGE PASSWORD\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "Enter new password: ");
    receive_message(client_socket, buffer);

    // Update password
    strcpy(user->password, buffer);

    // Lock file for writing
    pthread_mutex_lock(&file_mutex);

    // Open user file
    if ((fd = open(USER_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open users file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to change password. Please try again.\n");
        return;
    }

    // Seek to user position
    if (lseek(fd, (user->id - 1) * sizeof(User), SEEK_SET) == -1)
    {
        perror("Failed to seek in users file");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to change password. Please try again.\n");
        return;
    }

    // Write updated user
    if (write(fd, user, sizeof(User)) == -1)
    {
        perror("Failed to write user");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to change password. Please try again.\n");
        return;
    }

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Password changed successfully.\n");
}

// Enroll course
void enroll_course(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd, enrollment_fd;
    Enrollment enrollment;
    Student student;

    // Get student details
    if (!get_student_by_username(user->username, &student))
    {
        send_message(client_socket, "Student details not found. Please contact the administrator.\n");
        return;
    }

    // Display available courses
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                  AVAILABLE COURSES\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "+-------+--------------------+--------------------+---------------+\n");
    send_message(client_socket, "| ID    | Name               | Faculty            | Available     |\n");
    send_message(client_socket, "+-------+--------------------+--------------------+---------------+\n");

    // Lock file for reading
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view courses. Please try again.\n");
        return;
    }

    // Read courses
    while (read(fd, &course, sizeof(Course)) == sizeof(Course))
    {
        if (course.active && course.available_seats > 0)
        {
            // Get faculty name
            Faculty faculty;
            if (get_faculty_by_username(course.faculty_username, &faculty))
            {
                sprintf(buffer, "| %-5d | %-18s | %-18s | %-13d |\n", course.id, course.name, faculty.name, course.available_seats);
                send_message(client_socket, buffer);
            }
        }
    }
    send_message(client_socket, "+-------+--------------------+--------------------+---------------+\n");

    close(fd);

    // Get course ID
    send_message(client_socket, "\nEnter Course ID to enroll (0 to cancel): ");
    receive_message(client_socket, buffer);
    int course_id = atoi(buffer);

    if (course_id == 0)
    {
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    // Open course file with write lock
    if ((fd = open(COURSE_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to enroll in course. Please try again.\n");
        return;
    }

    // Lock the specific course record
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (course_id - 1) * sizeof(Course);
    lock.l_len = sizeof(Course);

    // Apply the lock
    if (fcntl(fd, F_SETLKW, &lock) == -1)
    {
        perror("Failed to lock course record");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to enroll in course. Please try again.\n");
        return;
    }

    // Seek to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Read course
    if (read(fd, &course, sizeof(Course)) != sizeof(Course))
    {
        perror("Failed to read course");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Check if course is active and has available seats
    if (!course.active)
    {
        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "This course is not available for enrollment.\n");
        return;
    }

    if (course.available_seats <= 0)
    {
        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "This course has no available seats.\n");
        return;
    }

    // Create enrollment file path
    char enrollment_file[100];
    sprintf(enrollment_file, "data/enrollments_%d.dat", course_id);

    // Check if student is already enrolled
    if ((enrollment_fd = open(enrollment_file, O_RDONLY)) != -1)
    {
        while (read(enrollment_fd, &enrollment, sizeof(Enrollment)) == sizeof(Enrollment))
        {
            if (enrollment.student_id == student.id)
            {
                close(enrollment_fd);

                // Release the lock
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);

                close(fd);
                pthread_mutex_unlock(&file_mutex);
                send_message(client_socket, "You are already enrolled in this course.\n");
                return;
            }
        }
        close(enrollment_fd);
    }

    // Open enrollment file for writing
    if ((enrollment_fd = open(enrollment_file, O_WRONLY | O_CREAT | O_APPEND, 0666)) == -1)
    {
        perror("Failed to open enrollments file");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to enroll in course. Please try again.\n");
        return;
    }

    // Create enrollment record
    enrollment.student_id = student.id;
    enrollment.course_id = course_id;

    // Write enrollment record
    if (write(enrollment_fd, &enrollment, sizeof(Enrollment)) == -1)
    {
        perror("Failed to write enrollment");
        close(enrollment_fd);

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to enroll in course. Please try again.\n");
        return;
    }

    close(enrollment_fd);

    // Update available seats
    course.available_seats--;

    // Seek back to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update course. Please try again.\n");
        return;
    }

    // Write updated course
    if (write(fd, &course, sizeof(Course)) == -1)
    {
        perror("Failed to write course");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update course. Please try again.\n");
        return;
    }

    // Release the lock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Enrolled in course successfully.\n");
}

// Unenroll course
void unenroll_course(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd, enrollment_fd, temp_fd;
    Enrollment enrollment;
    Student student;

    // Get student details
    if (!get_student_by_username(user->username, &student))
    {
        send_message(client_socket, "Student details not found. Please contact the administrator.\n");
        return;
    }

    // Display enrolled courses
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                   ENROLLED COURSES\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "+-------+--------------------+--------------------+\n");
    send_message(client_socket, "| ID    | Name               | Faculty            |\n");
    send_message(client_socket, "+-------+--------------------+--------------------+\n");

    // Lock file for reading
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view courses. Please try again.\n");
        return;
    }

    // Read courses
    int found = 0;
    while (read(fd, &course, sizeof(Course)) == sizeof(Course))
    {
        if (course.active)
        {
            // Create enrollment file path
            char enrollment_file[100];
            sprintf(enrollment_file, "data/enrollments_%d.dat", course.id);

            // Check if student is enrolled
            if ((enrollment_fd = open(enrollment_file, O_RDONLY)) != -1)
            {
                int enrolled = 0;

                while (read(enrollment_fd, &enrollment, sizeof(Enrollment)) == sizeof(Enrollment))
                {
                    if (enrollment.student_id == student.id)
                    {
                        enrolled = 1;
                        break;
                    }
                }

                close(enrollment_fd);

                if (enrolled)
                {
                    // Get faculty name
                    Faculty faculty;
                    if (get_faculty_by_username(course.faculty_username, &faculty))
                    {
                        sprintf(buffer, "| %-5d | %-18s | %-18s |\n", course.id, course.name, faculty.name);
                        send_message(client_socket, buffer);
                        found = 1;
                    }
                }
            }
        }
    }
    send_message(client_socket, "+-------+--------------------+--------------------+\n");

    close(fd);

    if (!found)
    {
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "You are not enrolled in any courses.\n");
        return;
    }

    // Get course ID
    send_message(client_socket, "\nEnter Course ID to unenroll (0 to cancel): ");
    receive_message(client_socket, buffer);
    int course_id = atoi(buffer);

    if (course_id == 0)
    {
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    // Open course file with write lock
    if ((fd = open(COURSE_FILE, O_RDWR)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to unenroll from course. Please try again.\n");
        return;
    }

    // Lock the specific course record
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (course_id - 1) * sizeof(Course);
    lock.l_len = sizeof(Course);

    // Apply the lock
    if (fcntl(fd, F_SETLKW, &lock) == -1)
    {
        perror("Failed to lock course record");
        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to unenroll from course. Please try again.\n");
        return;
    }

    // Seek to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Read course
    if (read(fd, &course, sizeof(Course)) != sizeof(Course))
    {
        perror("Failed to read course");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Course not found. Please try again.\n");
        return;
    }

    // Create enrollment file path
    char enrollment_file[100];
    sprintf(enrollment_file, "data/enrollments_%d.dat", course_id);

    // Create temporary file path
    char temp_file[100];
    sprintf(temp_file, "data/temp_enrollments_%d.dat", course_id);

    // Open enrollment file for reading
    if ((enrollment_fd = open(enrollment_file, O_RDONLY)) == -1)
    {
        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "You are not enrolled in this course.\n");
        return;
    }

    // Open temporary file for writing
    if ((temp_fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
    {
        perror("Failed to open temporary file");
        close(enrollment_fd);

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to unenroll from course. Please try again.\n");
        return;
    }

    // Copy enrollments except the one to remove
    int found_enrollment = 0;
    while (read(enrollment_fd, &enrollment, sizeof(Enrollment)) == sizeof(Enrollment))
    {
        if (enrollment.student_id == student.id)
        {
            found_enrollment = 1;
        }
        else
        {
            if (write(temp_fd, &enrollment, sizeof(Enrollment)) == -1)
            {
                perror("Failed to write enrollment");
                close(enrollment_fd);
                close(temp_fd);
                unlink(temp_file);

                // Release the lock
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);

                close(fd);
                pthread_mutex_unlock(&file_mutex);
                send_message(client_socket, "Failed to unenroll from course. Please try again.\n");
                return;
            }
        }
    }

    close(enrollment_fd);
    close(temp_fd);

    if (!found_enrollment)
    {
        unlink(temp_file);

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "You are not enrolled in this course.\n");
        return;
    }

    // Replace enrollment file with temporary file
    if (rename(temp_file, enrollment_file) == -1)
    {
        perror("Failed to rename file");
        unlink(temp_file);

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to unenroll from course. Please try again.\n");
        return;
    }

    // Update available seats
    course.available_seats++;

    // Seek back to course position
    if (lseek(fd, (course_id - 1) * sizeof(Course), SEEK_SET) == -1)
    {
        perror("Failed to seek in courses file");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update course. Please try again.\n");
        return;
    }

    // Write updated course
    if (write(fd, &course, sizeof(Course)) == -1)
    {
        perror("Failed to write course");

        // Release the lock
        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);

        close(fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to update course. Please try again.\n");
        return;
    }

    // Release the lock
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Unenrolled from course successfully.\n");
}

// View enrolled courses
void view_enrolled_courses(int client_socket, User *user)
{
    char buffer[BUFFER_SIZE];
    Course course;
    int fd, enrollment_fd;
    Enrollment enrollment;
    Student student;

    // Get student details
    if (!get_student_by_username(user->username, &student))
    {
        send_message(client_socket, "Student details not found. Please contact the administrator.\n");
        return;
    }

    // Display enrolled courses
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                   ENROLLED COURSES\n");
    send_message(client_socket, "==========================================================\n\n");
    send_message(client_socket, "+-------+--------------------+--------------------+\n");
    send_message(client_socket, "| ID    | Name               | Faculty            |\n");
    send_message(client_socket, "+-------+--------------------+--------------------+\n");

    // Lock file for reading
    pthread_mutex_lock(&file_mutex);

    // Open course file
    if ((fd = open(COURSE_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open courses file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to view courses. Please try again.\n");
        return;
    }

    // Read courses
    int found = 0;
    while (read(fd, &course, sizeof(Course)) == sizeof(Course))
    {
        if (course.active)
        {
            // Create enrollment file path
            char enrollment_file[100];
            sprintf(enrollment_file, "data/enrollments_%d.dat", course.id);

            // Check if student is enrolled
            if ((enrollment_fd = open(enrollment_file, O_RDONLY)) != -1)
            {
                int enrolled = 0;

                // Set read lock
                struct flock lock;
                lock.l_type = F_RDLCK;
                lock.l_whence = SEEK_SET;
                lock.l_start = 0;
                lock.l_len = 0; // Lock the entire file

                // Apply the lock
                if (fcntl(enrollment_fd, F_SETLKW, &lock) == -1)
                {
                    perror("Failed to lock enrollment file");
                    close(enrollment_fd);
                    continue;
                }

                while (read(enrollment_fd, &enrollment, sizeof(Enrollment)) == sizeof(Enrollment))
                {
                    if (enrollment.student_id == student.id)
                    {
                        enrolled = 1;
                        break;
                    }
                }

                // Release the lock
                lock.l_type = F_UNLCK;
                fcntl(enrollment_fd, F_SETLK, &lock);

                close(enrollment_fd);

                if (enrolled)
                {
                    // Get faculty name
                    Faculty faculty;
                    if (get_faculty_by_username(course.faculty_username, &faculty))
                    {
                        sprintf(buffer, "| %-5d | %-18s | %-18s |\n", course.id, course.name, faculty.name);
                        send_message(client_socket, buffer);
                        found = 1;
                    }
                }
            }
        }
    }

    close(fd);

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    if (!found)
    {
        send_message(client_socket, "You are not enrolled in any courses.\n");
    }
    send_message(client_socket, "+-------+--------------------+--------------------+\n");
}

// Get user by username
int get_user_by_username(char *username, User *user)
{
    int fd;
    User temp;

    // Open users file
    if ((fd = open(USER_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open users file");
        return 0;
    }

    // Read users from file
    while (read(fd, &temp, sizeof(User)) > 0)
    {
        if (strcmp(temp.username, username) == 0)
        {
            *user = temp;
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

// Get student by username
int get_student_by_username(char *username, Student *student)
{
    int fd, user_fd;
    User user;
    Student temp;

    // Get user by username
    if (!get_user_by_username(username, &user) || user.role != STUDENT)
    {
        return 0;
    }

    // Open students file
    if ((fd = open(STUDENT_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open students file");
        return 0;
    }

    // Read students from file
    while (read(fd, &temp, sizeof(Student)) > 0)
    {
        if (temp.id == user.id)
        {
            *student = temp;
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

// Get faculty by username
int get_faculty_by_username(char *username, Faculty *faculty)
{
    int fd, user_fd;
    User user;
    Faculty temp;

    // Get user by username
    if (!get_user_by_username(username, &user) || user.role != FACULTY)
    {
        return 0;
    }

    // Open faculty file
    if ((fd = open(FACULTY_FILE, O_RDONLY)) == -1)
    {
        perror("Failed to open faculty file");
        return 0;
    }

    // Read faculty from file
    while (read(fd, &temp, sizeof(Faculty)) > 0)
    {
        if (temp.id == user.id)
        {
            *faculty = temp;
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

// Send message to client
void send_message(int client_socket, const char *message)
{
    if (write(client_socket, message, strlen(message)) == -1)
    {
        perror("Failed to send message");
    }
}

// Receive message from client
void receive_message(int client_socket, char *buffer)
{
    int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0)
    {
        if (bytes_read == 0)
        {
            // Client disconnected
            printf("Client disconnected\n");
        }
        else
        {
            perror("Failed to receive message");
        }
        buffer[0] = '\0';
        return;
    }

    // Remove newline character
    if (buffer[bytes_read - 1] == '\n')
    {
        buffer[bytes_read - 1] = '\0';
    }
    else
    {
        buffer[bytes_read] = '\0';
    }
}

// Create a new account
void create_account(int client_socket)
{
    char buffer[BUFFER_SIZE];
    User user;
    int user_fd;

    // Get account details
    send_message(client_socket, "\n==========================================================\n");
    send_message(client_socket, "                  CREATE NEW ACCOUNT\n");
    send_message(client_socket, "==========================================================\n\n");

    // Get username
    send_message(client_socket, "Username: ");
    receive_message(client_socket, buffer);

    // Check if username already exists
    if (get_user_by_username(buffer, &user))
    {
        send_message(client_socket, "Username already exists. Please try again.\n");
        return;
    }

    strcpy(user.username, buffer);

    // Get password
    send_message(client_socket, "Password: ");
    receive_message(client_socket, user.password);

    // Get role
    send_message(client_socket, "Role (1 for Admin, 2 for Faculty, 3 for Student): ");
    receive_message(client_socket, buffer);
    user.role = atoi(buffer);

    if (user.role < 1 || user.role > 3)
    {
        send_message(client_socket, "Invalid role. Please try again.\n");
        return;
    }

    // Set user as active
    user.active = 1;

    // Lock file for writing
    pthread_mutex_lock(&file_mutex);

    // Open user file
    if ((user_fd = open(USER_FILE, O_RDWR | O_CREAT, 0666)) == -1)
    {
        perror("Failed to open users file");
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to create account. Please try again.\n");
        return;
    }

    // Get next user ID
    user.id = lseek(user_fd, 0, SEEK_END) / sizeof(User) + 1;

    // Write user to file
    if (write(user_fd, &user, sizeof(User)) == -1)
    {
        perror("Failed to write user");
        close(user_fd);
        pthread_mutex_unlock(&file_mutex);
        send_message(client_socket, "Failed to create account. Please try again.\n");
        return;
    }

    close(user_fd);

    // If role is student or faculty, create corresponding record
    if (user.role == STUDENT)
    {
        Student student;
        int fd;

        // Get student details
        send_message(client_socket, "Name: ");
        receive_message(client_socket, student.name);

        send_message(client_socket, "Department: ");
        receive_message(client_socket, student.department);

        send_message(client_socket, "Semester: ");
        receive_message(client_socket, buffer);
        student.semester = atoi(buffer);

        // Set student as active
        student.active = 1;
        student.id = user.id;

        // Open student file
        if ((fd = open(STUDENT_FILE, O_RDWR | O_CREAT, 0666)) == -1)
        {
            perror("Failed to open students file");
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to create student record. Please try again.\n");
            return;
        }

        // Write student to file
        if (write(fd, &student, sizeof(Student)) == -1)
        {
            perror("Failed to write student");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to create student record. Please try again.\n");
            return;
        }

        close(fd);
    }
    else if (user.role == FACULTY)
    {
        Faculty faculty;
        int fd;

        // Get faculty details
        send_message(client_socket, "Name: ");
        receive_message(client_socket, faculty.name);

        send_message(client_socket, "Department: ");
        receive_message(client_socket, faculty.department);

        // Set faculty as active
        faculty.active = 1;
        faculty.id = user.id;

        // Open faculty file
        if ((fd = open(FACULTY_FILE, O_RDWR | O_CREAT, 0666)) == -1)
        {
            perror("Failed to open faculty file");
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to create faculty record. Please try again.\n");
            return;
        }

        // Write faculty to file
        if (write(fd, &faculty, sizeof(Faculty)) == -1)
        {
            perror("Failed to write faculty");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            send_message(client_socket, "Failed to create faculty record. Please try again.\n");
            return;
        }

        close(fd);
    }

    // Unlock file
    pthread_mutex_unlock(&file_mutex);

    send_message(client_socket, "Account created successfully. Please login with your new credentials.\n");
}
