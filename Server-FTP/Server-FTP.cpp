#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <map>
#include <direct.h>
#include <sstream>
#include <io.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 21
#define BUFFER_SIZE 1024
#define BACKLOG 5

// Predefined users and passwords
std::map<std::string, std::string> users = {
    {"user1", "password1"},
    {"user2", "password2"},
    {"user3", "password3"}
};

void handle_client(SOCKET client_sock) {
    char buffer[BUFFER_SIZE];
    bool authenticated = false;
    std::string current_user;
    std::string transfer_mode = "A";
    SOCKET data_sock = INVALID_SOCKET;

    // Send welcome message
    std::string welcome_msg = "220 Welcome to the simple FTP server.\r\n";
    send(client_sock, welcome_msg.c_str(), welcome_msg.size(), 0);

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            std::cerr << "Connection closed by client." << std::endl;
            break;
        }

        std::string command(buffer);
        std::cout << "Command received: " << command;

        if (command.substr(0, 4) == "USER") {
            current_user = command.substr(5);
            current_user.erase(current_user.find_last_not_of(" \r\n") + 1);

            if (users.find(current_user) != users.end()) {
                send(client_sock, "331 User name okay, need password.\r\n", 36, 0);
            }
            else {
                send(client_sock, "530 User not found.\r\n", 22, 0);
                current_user.clear();
            }
        }
        else if (command.substr(0, 4) == "PASS") {
            std::string password = command.substr(5);
            password.erase(password.find_last_not_of(" \r\n") + 1);

            if (!current_user.empty() && users[current_user] == password) {
                authenticated = true;
                send(client_sock, "230 User logged in, proceed.\r\n", 30, 0);
            }
            else {
                send(client_sock, "530 Incorrect password.\r\n", 26, 0);
                current_user.clear();
            }
        }
        else if (command.substr(0, 4) == "QUIT") {
            send(client_sock, "221 Goodbye.\r\n", 14, 0);
            break;
        }
        else if (!authenticated) {
            send(client_sock, "530 Not logged in.\r\n", 21, 0);
        }
        else if (command.substr(0, 4) == "LIST") {
            if (data_sock == INVALID_SOCKET) {
                send(client_sock, "425 Use PASV first.\r\n", 22, 0);
                continue;
            }

            send(client_sock, "150 Here comes the directory listing.\r\n", 40, 0);

            struct _finddata_t file_info;
            intptr_t handle = _findfirst("*", &file_info);

            if (handle != -1) {
                do {
                    std::string entry = file_info.name;
                    entry += "\r\n";
                    send(data_sock, entry.c_str(), entry.size(), 0);
                } while (_findnext(handle, &file_info) == 0);

                _findclose(handle);
            }
            else {
                send(client_sock, "550 Failed to list directory.\r\n", 31, 0);
            }

            send(client_sock, "226 Directory send okay.\r\n", 26, 0);
            closesocket(data_sock);
            data_sock = INVALID_SOCKET;
        }
        else if (command.substr(0, 4) == "RETR") {
            std::string filename = command.substr(5);
            filename.erase(filename.find_last_not_of(" \r\n") + 1);

            if (data_sock == INVALID_SOCKET) {
                send(client_sock, "425 Use PASV first.\r\n", 22, 0);
                continue;
            }

            // Open the file for reading in binary mode
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                send(client_sock, "550 File not found.\r\n", 22, 0);
            }
            else {
                // Send the 150 "Opening binary mode data connection" response
                send(client_sock, "150 Opening binary mode data connection.\r\n", 44, 0);

                // Prepare to send the file content over the data connection
                char buffer[BUFFER_SIZE];
                while (file.read(buffer, BUFFER_SIZE)) {
                    send(data_sock, buffer, file.gcount(), 0); // Send the data
                }
                send(data_sock, buffer, file.gcount(), 0); // Send any remaining data

                file.close();  // Close the file after reading

                // Send the 226 "Transfer complete" response
                send(client_sock, "226 Transfer complete.\r\n", 26, 0);
            }

            closesocket(data_sock);  // Close the data connection
            data_sock = INVALID_SOCKET;  // Reset the data socket
        }
        else if (command.substr(0, 3) == "PWD") {
            char cwd[BUFFER_SIZE];
            if (_getcwd(cwd, sizeof(cwd)) != NULL) {
                std::string response = "257 \"" + std::string(cwd) + "\" is the current directory.\r\n";
                send(client_sock, response.c_str(), response.size(), 0);
            }
            else {
                send(client_sock, "550 Failed to get current directory.\r\n", 40, 0);
            }
        }
        else if (command.substr(0, 4) == "TYPE") {
            std::string type = command.substr(5);
            type.erase(type.find_last_not_of(" \r\n") + 1);

            if (type == "A" || type == "I") {
                transfer_mode = type;
                std::string response = "200 Switching to ";
                response += (type == "A" ? "ASCII" : "Binary");
                response += " mode.\r\n";
                send(client_sock, response.c_str(), response.size(), 0);
            }
            else {
                send(client_sock, "504 Command not implemented for that parameter.\r\n", 51, 0);
            }
        }
        else if (command.substr(0, 4) == "PASV") {
            SOCKET pasv_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (pasv_sock == INVALID_SOCKET) {
                send(client_sock, "425 Cannot open passive connection.\r\n", 38, 0);
                continue;
            }

            sockaddr_in pasv_addr{};
            pasv_addr.sin_family = AF_INET;
            pasv_addr.sin_addr.s_addr = INADDR_ANY;
            pasv_addr.sin_port = 0; // Let the OS assign a port

            if (bind(pasv_sock, (sockaddr*)&pasv_addr, sizeof(pasv_addr)) == SOCKET_ERROR) {
                send(client_sock, "425 Cannot bind passive connection.\r\n", 38, 0);
                closesocket(pasv_sock);
                continue;
            }

            if (listen(pasv_sock, 1) == SOCKET_ERROR) {
                send(client_sock, "425 Cannot listen on passive connection.\r\n", 42, 0);
                closesocket(pasv_sock);
                continue;
            }

            int addr_len = sizeof(pasv_addr);
            if (getsockname(pasv_sock, (sockaddr*)&pasv_addr, &addr_len) == SOCKET_ERROR) {
                send(client_sock, "425 Cannot retrieve passive port.\r\n", 36, 0);
                closesocket(pasv_sock);
                continue;
            }

            int pasv_port = ntohs(pasv_addr.sin_port);
            std::string pasv_response = "227 Entering Passive Mode (127,0,0,1,";
            pasv_response += std::to_string((pasv_port >> 8) & 0xFF) + "," + std::to_string(pasv_port & 0xFF) + ").\r\n";
            send(client_sock, pasv_response.c_str(), pasv_response.size(), 0);

            // Wait for client connection
            data_sock = accept(pasv_sock, NULL, NULL);
            closesocket(pasv_sock);

            if (data_sock == INVALID_SOCKET) {
                send(client_sock, "425 Failed to establish data connection.\r\n", 43, 0);
            }
        }
        else if (command.substr(0, 4) == "STOR") {
            std::string filename = command.substr(5);
            filename.erase(filename.find_last_not_of(" \r\n") + 1);

            if (data_sock == INVALID_SOCKET) {
                send(client_sock, "425 Use PASV first.\r\n", 22, 0);
                continue;
            }

            // Open the file for writing (binary mode)
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                send(client_sock, "550 Failed to open file.\r\n", 25, 0);
            }
            else {
                send(client_sock, "150 Opening binary mode data connection.\r\n", 44, 0);

                char buffer[BUFFER_SIZE];
                int bytes_received;
                while ((bytes_received = recv(data_sock, buffer, BUFFER_SIZE, 0)) > 0) {
                    file.write(buffer, bytes_received); // Write data to file
                }

                if (bytes_received < 0) {
                    send(client_sock, "426 Connection closed; transfer aborted.\r\n", 41, 0);
                }
                else {
                    send(client_sock, "226 Transfer complete.\r\n", 26, 0);
                }

                file.close();  // Close the file after writing
            }

            closesocket(data_sock);  // Close the data connection
            data_sock = INVALID_SOCKET;
        }
        else {
            send(client_sock, "502 Command not implemented.\r\n", 32, 0);
        }
    }
    if (data_sock != INVALID_SOCKET) {
        closesocket(data_sock);
    }
    closesocket(client_sock);
}

int main() {
    WSADATA wsaData;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // Create socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return 1;
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    // Listen for connections
    if (listen(server_sock, BACKLOG) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    std::cout << "FTP server listening on port " << PORT << std::endl;

    while (true) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock == INVALID_SOCKET) {
            std::cerr << "Accept failed." << std::endl;
            continue;
        }

        std::cout << "Connection accepted." << std::endl;

        // Handle the client in the main thread (or spawn a new thread/process if needed)
        handle_client(client_sock);
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}
