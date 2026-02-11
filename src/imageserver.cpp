#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

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