#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

std::mutex mtx;
int clientCount = 0;

void handleClient(SOCKET clientSock){

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount++;
        std::cout << "Client Connect | Current Client: " << clientCount << "\n";
    }

    char buffer[2048];

    int received = recv(clientSock, buffer, sizeof(buffer) -1, 0);

    if(received > 0)
    {
        buffer[received] = '\0';

        std::string request(buffer);

        std::cout << "\n ---- New Client ----\n"; 
        std::cout << request << "\n";
        
        const char* body = "Hello World";

        std::string response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            std::string(body);

        send(clientSock, response.c_str(), response.size(), 0);
    }

    closesocket(clientSock);

    {
        std::lock_guard<std::mutex> lock(mtx);
        clientCount--;
        std::cout << "Client Disconnected | Current Client: " << clientCount << "\n";
    }
}

int main(){
    WSADATA wsaData;

    if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
    {
        std::cout << "WSAStarup failed.\n";
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(listenSock == INVALID_SOCKET)
    {
        std::cout << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "Bind failed.\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if(listen(listenSock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "Listen failed.\n";
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port 8080\n";

    while(true)
    {
        SOCKET clientSock = accept(listenSock, nullptr, nullptr);

        if(clientSock == INVALID_SOCKET)
            continue;
        
        std::thread t(handleClient, clientSock);
        t.detach();
    }

    closesocket(listenSock);
    WSACleanup();

    return 0;
}

/*
int main(){
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listenSock == INVALID_SOCKET){
        std::cout << "socket failed\n";
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(bind(listenSock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR){
        std::cout << "bind failed\n";
        return 1;
    }

    listen(listenSock, 1);

    std::cout << "Server listening on 127.0.0.1:8080\n";

    SOCKET clientSock = accept(listenSock, nullptr, nullptr);
    if(clientSock == INVALID_SOCKET){
        std::cout << "accept failed\n";
        return 1;
    }

    char buffer[2048];
    int received = recv(clientSock, buffer, sizeof(buffer) -1, 0);

    if(received > 0){
        buffer[received] = 0;
        std::string request(buffer);

        std::cout << "--- Request ---\n" << request << "\n";

        //요청 경로 판별
        if(request.find("GET /image") != std::string::npos){
            std::ifstream file("test.png", std::ios::binary);
            if(!file){
                std::cout << "Image file doesn't exist\n";
            }else{
                //파일크기
                file.seekg(0, std::ios::end);
                size_t fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                std::vector<char> fileData(fileSize);
                file.read(fileData.data(), fileSize);

                std::cout << "File size: " << fileSize << " bytes\n";

                //HTTP Header
                std::string header =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/png\r\n"
                    "Content-Length: " + std::to_string(fileSize) + "\r\n"
                    "Connection: close\r\n"
                    "\r\n";

                //헤더 전송
                send(clientSock, header.c_str(), header.size(), 0);
                //파일 안전 전송 (이미지 에러가 났었던 부분)
                size_t totalSent = 0;
                while(totalSent < fileSize){
                    int sent = send(
                        clientSock,
                        fileData.data() + totalSent,
                        fileSize - totalSent,
                        0
                    );

                    if(sent <= 0){
                        std::cout << "send error\n";
                        break;
                    }

                    totalSent += sent;
                }

                std::cout << "Total sent: " << totalSent << " bytes\n";
            }
        }
        else{
            //텍스트 응답
            const char* response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 12\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Hello World";

            send(clientSock, response, strlen(response), 0);
        }
    }

    closesocket(clientSock);
    closesocket(listenSock);
    WSACleanup();

    std::cout << "Server Closed\n";
    std::cin.get();
    return 0;
}

*/