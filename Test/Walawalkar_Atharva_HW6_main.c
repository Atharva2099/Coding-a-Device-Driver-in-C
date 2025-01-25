/**************************************************************
* Class: CSC-415-03 Fall 2024
* Name: Atharva
* Student ID: 924254653
* GitHub Name: AtharvaWal2002
* Project: Assignment 6 - Device Driver
*
* File: Walawalkar_Atharva_HW6_main.c
*
* Description: Test program for RLE device driver
*
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>

#define DEVICE_PATH "/dev/rledev"
#define MAX_INPUT 1024

// RLE modes
#define RLE_MODE_COMPRESS 0
#define RLE_MODE_DECOMPRESS 1

// IOCTL commands
#define RLE_IOC_MAGIC 'r'
#define RLE_SET_MODE _IOW(RLE_IOC_MAGIC, 1, int)

// Print data in a readable format
void print_data(const char *data, size_t len) {
    printf("Data (%zu bytes): ", len);
    for (size_t i = 0; i < len; i++) {
        printf("%c", data[i]);
    }
    printf("\n");
}

// Clear input buffer
void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main(int argc, char *argv[]) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    char input[MAX_INPUT];
    char output[MAX_INPUT];
    ssize_t bytes_read;
    int choice;

    while (1) {
        printf("\nRLE Device Driver Test Menu\n");
        printf("1. Compress string\n");
        printf("2. Decompress data\n");
        printf("3. Exit\n");
        printf("Choose option: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            clear_input_buffer();
            continue;
        }
        clear_input_buffer();

        switch (choice) {
            case 1:
                if (ioctl(fd, RLE_SET_MODE, RLE_MODE_COMPRESS) < 0) {
                    perror("Failed to set compress mode");
                    break;
                }

                printf("Enter string to compress: ");
                if (!fgets(input, sizeof(input), stdin)) {
                    printf("Error reading input\n");
                    break;
                }
                input[strcspn(input, "\n")] = 0;  // Remove newline

                if (write(fd, input, strlen(input)) < 0) {
                    perror("Failed to write to device");
                    break;
                }

                bytes_read = read(fd, output, sizeof(output));
                if (bytes_read < 0) {
                    perror("Failed to read from device");
                    break;
                }

                printf("Original: ");
                print_data(input, strlen(input));
                printf("Compressed (use this format for decompression): ");
                for (size_t i = 0; i < bytes_read; i += 2) {
                    printf("%d %c ", (unsigned char)output[i], output[i+1]);
                }
                printf("\n");
                break;

            case 2:
                if (ioctl(fd, RLE_SET_MODE, RLE_MODE_DECOMPRESS) < 0) {
                    perror("Failed to set decompress mode");
                    break;
                }

                memset(input, 0, sizeof(input));
                size_t input_pos = 0;

                printf("Enter compressed data in format: count char count char\n");
                printf("Example: 3 A 3 B for AAABBB\n");
                printf("Enter input: ");

                char line[MAX_INPUT];
                if (!fgets(line, sizeof(line), stdin)) {
                    printf("Error reading input\n");
                    break;
                }

                // Parse the input string
                char *token = strtok(line, " \n");
                while (token != NULL) {
                    // Read count
                    int count = atoi(token);
                    if (count <= 0 || count > 255) {
                        printf("Invalid count: %s\n", token);
                        break;
                    }

                    // Get next token (character)
                    token = strtok(NULL, " \n");
                    if (!token || strlen(token) != 1) {
                        printf("Invalid character input\n");
                        break;
                    }

                    input[input_pos++] = (char)count;
                    input[input_pos++] = token[0];

                    token = strtok(NULL, " \n");
                }

                if (input_pos == 0) {
                    printf("No valid input received\n");
                    break;
                }

                if (write(fd, input, input_pos) < 0) {
                    perror("Failed to write to device");
                    break;
                }

                bytes_read = read(fd, output, sizeof(output));
                if (bytes_read < 0) {
                    perror("Failed to read from device");
                    break;
                }

                printf("Decompressed: ");
                print_data(output, bytes_read);
                break;

            case 3:
                printf("Exiting program...\n");
                close(fd);
                return 0;

            default:
                printf("Invalid option. Please choose 1-3\n");
        }
    }

    close(fd);
    return 0;
}