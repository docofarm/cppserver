#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <sstream>
#include <map>

#pragma comment(lib, "ws2_32.lib")

std::mutex mtx;
int clientCount = 0;

std::string makeResponse(
    const std::string& status,
    const std::string& contentType,
    const std::string& body
){
    return 
        "HTTP/1.1 "+ status + "\r\n"
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + 
        body;
}

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

        //1. request line parshing
        std::istringstream requestStream(request);
        std::string requestLine;

        std::getline(requestStream, requestLine);

        // remove \r
        if(!requestLine.empty() && requestLine.back() == '\r')
        {
            requestLine.pop_back();
        }
        std::istringstream lineStream(requestLine);

        std::string method;
        std::string path;
        std::string version;

        lineStream >> method >> path >> version;

        std::cout << "Method: " << method << "\n";
        std::cout << "Path: " << path << "\n";
        std::cout << "Version: " << version << "\n";

        //2. header parshing
        std::map<std::string, std::string> headers;
        std::string headerLine;

        while (std::getline(requestStream, headerLine)){
            if(headerLine == "\r" || headerLine.empty()){
                break;
            }

            if(headerLine.back() == '\r')
            {
                headerLine.pop_back();
            }

            size_t colonPos = headerLine.find(":");
            if(colonPos != std::string::npos)
            {
                std::string key = headerLine.substr(0, colonPos);
                std::string value = headerLine.substr(colonPos + 1);

                if(!value.empty() && value[0] == ' '){
                    value.erase(0,1);
                }
                headers[key] = value;
            }
        }

        if(headers.find("Host") != headers.end()){
            std::cout << "Host Header: " << headers["Host"] << "\n";
        }

        std::string body;
        std::string response;

        //3. body parsing
        if(headers.find("Content-Length") != headers.end())
        {
            int contentLength = std::stoi(headers["Content-Length"]);
            body.resize(contentLength);
            requestStream.read(&body[0], contentLength);
        }

        if(!body.empty()){
            std::cout << "Body: "<< body << "\n";
        }

        if(method == "GET" && path == "/"){
            response = makeResponse(
                "200 OK",
                "text/plain",
                "Welcome My Server😊"
            );
        }else if(method == "GET" && path == "/first"){
            response = makeResponse(
                "200 OK",
                "text/plain",
                "This is Page😎"
            );
        }else if(method == "POST" && path == "/login"){

            std::string id;
            std::string pw;

            size_t idPos = body.find("id=");
            size_t pwPos = body.find("pw=");

            if(idPos != std::string::npos && pwPos != std::string::npos)
            {
                size_t idEnd = body.find("&", idPos);
                id = body.substr(idPos + 3, idEnd - (idPos + 3));
                pw = body.substr(pwPos + 3);
            }

            std::string json;

            if(id == "admin" && pw == "1234")
            {
                json = "{\"status\":\"success\",\"message\":\"login success\"}";
                response = makeResponse("200 OK", "application/json", json);
            }else{
                json = "{\"status\":\"fail\",\"message\":\"invaild credential\"}";
                response = makeResponse("401 Unauthorized", "application/json", json);
            }
        }else if(method == "GET" || method == "POST"){
            response = makeResponse(
                "404 Not Found",
                "text/plain",
                "Not Found"
            );
        }else{
            response = makeResponse(
                "405 Method Not Allowed",
                "text/plain",
                ""
            );
        }

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